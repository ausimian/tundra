#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <erl_nif.h>
#include <erl_driver.h>
#include "srv.h"

static ErlNifResourceType * s_fdrt;

static ERL_NIF_TERM s_ok;
static ERL_NIF_TERM s_error;
static ERL_NIF_TERM s_eagain;
static ERL_NIF_TERM s_not_owner;

struct fd_object_t {
    int fd;
    ErlNifPid cp;
    ErlNifMonitor mon;
};

static void fdrt_dtor(ErlNifEnv* env, void* obj)
{
    (void)env;
    struct fd_object_t * fd_obj = obj;
    int s = fd_obj->fd;
    if( s != -1 && atomic_compare_exchange_strong((atomic_int*)&fd_obj->fd, &s, -1) ) {
        close(s);
    }
}

static void fdrt_stop(ErlNifEnv* env, void* obj, ErlNifEvent event, int is_direct_call)
{
    (void)event;
    (void)is_direct_call;
    fdrt_dtor(env, obj);
}

static void fdrt_down(ErlNifEnv* env, void* obj, ErlNifPid* pid, ErlNifMonitor* mon)
{
    (void)mon;
    struct fd_object_t * fd_obj = obj;
    if( enif_compare_pids(&fd_obj->cp, pid) == 0 ) {
        enif_select(env, fd_obj->fd, ERL_NIF_SELECT_STOP, fd_obj, NULL, enif_make_ref(env));
    }
}

static const ErlNifResourceTypeInit s_fdrt_init = {
    .dtor = fdrt_dtor,
    .stop = fdrt_stop,
    .down = fdrt_down,
    .members = 3,
    .dyncall = NULL
};

static struct fd_object_t * alloc_fd_object(ErlNifEnv* env)
{
    struct fd_object_t * fd_obj = enif_alloc_resource(s_fdrt, sizeof(*fd_obj));
    if(fd_obj != NULL) {
        fd_obj->fd = -1;
        if( NULL == enif_self(env, &fd_obj->cp) || enif_monitor_process(env, fd_obj, &fd_obj->cp, &fd_obj->mon) != 0 ) {
            enif_release_resource(fd_obj);
            fd_obj = NULL;
        }
    }
    return fd_obj;
}

static ERL_NIF_TERM make_error(ErlNifEnv* env, int err)
{
    return enif_make_tuple2(env, s_error, enif_make_atom(env, erl_errno_id(err)));
}

static int load(ErlNifEnv* env, void** priv_data, ERL_NIF_TERM load_info)
{
    (void)priv_data;
    (void)load_info;
    s_ok = enif_make_atom(env, "ok");
    s_error = enif_make_atom(env, "error");
    s_eagain = enif_make_atom(env, "eagain");
    s_not_owner = enif_make_atom(env, "not_owner");
    s_fdrt = enif_init_resource_type(env, "fdrt", &s_fdrt_init, ERL_NIF_RT_CREATE, NULL);
    return s_fdrt ? 0 : -1;
}

static ERL_NIF_TERM recv_response(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    void *obj;
    if( argc != 2 ||!enif_get_resource(env, argv[0], s_fdrt, &obj) || !enif_is_ref(env, argv[1]) ) {
        return enif_make_badarg(env);
    }
    struct fd_object_t * fd_obj = obj;
    ErlNifPid self;
    if( enif_compare_pids(&fd_obj->cp, enif_self(env, &self)) != 0 ) {
        return enif_make_tuple2(env, s_error, s_not_owner);
    }
    int s = fd_obj->fd;

    struct response_t resp = { 0 };
    char cmsgbuf[CMSG_SPACE(sizeof(int))];
    struct iovec iov = {
        .iov_base = &resp,
        .iov_len = sizeof(resp)
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = cmsgbuf,
        .msg_controllen = sizeof cmsgbuf,
    };

    int ret = recvmsg(s, &msg, 0);
    if(ret == -1 && errno == EINTR) {
        return enif_schedule_nif(env, "recv_response", 0, recv_response, 2, argv);
    }
    if( ret == -1 && ( errno == EAGAIN || errno == EWOULDBLOCK ) ) {
        if(enif_select(env, s, ERL_NIF_SELECT_READ, obj, NULL, argv[1]) < 0) {
            return enif_make_badarg(env);
        }
        return enif_make_tuple2(env, s_error, s_eagain);
    }

    if(ret == -1) {
        return make_error(env, errno);
    }

    // Read the aux data and check for the file descriptor
    struct cmsghdr * cmsg = CMSG_FIRSTHDR(&msg);
    if( cmsg == NULL || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS || cmsg->cmsg_len != CMSG_LEN(sizeof(int)) ) {
        return make_error(env, EINVAL);
    }
    // Extract the file descriptor
    int fd;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));

    ERL_NIF_TERM result;

    // Allocate a resource for the file descriptor so that it becomes owned by the calling process
    struct fd_object_t * res_fd = alloc_fd_object(env);
    if( res_fd ) {
        res_fd->fd = fd;
        // Allocate a binary to hold the name of the tun device
        ErlNifBinary name_bin;
        if(enif_alloc_binary(strlen(resp.msg.create_tun.name), &name_bin)) {
            strcpy((char*)name_bin.data, resp.msg.create_tun.name);

            ERL_NIF_TERM info = enif_make_tuple2(env, enif_make_resource(env, res_fd), enif_make_binary(env, &name_bin));
            enif_release_binary(&name_bin);

            fd = -1; // Ownership has moved
            result = enif_make_tuple2(env, s_ok, info);
        }
        else
        {
            result = make_error(env, ENOMEM);
        }

        enif_release_resource(res_fd);
    }
    else
    {
        result = make_error(env, ENOMEM);
    }

    if( fd >= 0 ) {
        close(fd);
    }

    return result;
}

static ERL_NIF_TERM send_request(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    void *obj;
    if( argc != 2 || !enif_get_resource(env, argv[0], s_fdrt, &obj) || !enif_is_ref(env, argv[1]) ) {
        return enif_make_badarg(env);
    }
    struct fd_object_t * fd_obj = obj;
    ErlNifPid self;
    if( enif_compare_pids(&fd_obj->cp, enif_self(env, &self)) != 0 ) {
        return enif_make_tuple2(env, s_error, s_not_owner);
    }
    int s = fd_obj->fd;

    struct request_t req = { .type = REQUEST_TYPE_CREATE_TUN };
    int rc = send(s, &req, sizeof(req), TUNDRA_MSG_NOSIGNAL);
    if( rc == -1 && errno == EINTR ) {
        return enif_schedule_nif(env, "send_request", 0, send_request, 2, argv);
    }
    if( rc == -1 && ( errno == EAGAIN || errno == EWOULDBLOCK ) ) {
        if(enif_select(env, s, ERL_NIF_SELECT_WRITE, obj, NULL, argv[1]) < 0) {
            return enif_make_badarg(env);
        }
        return enif_make_tuple2(env, s_error, s_eagain);
    }
    if( rc == -1 ) {
        return make_error(env, errno);
    }

    return s_ok;
}

static ERL_NIF_TERM try_connect(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    void *obj;
    if( argc != 1 || !enif_get_resource(env, argv[0], s_fdrt, &obj) ) {
        return enif_make_badarg(env);
    }
    struct fd_object_t * fd_obj = obj;
    int s = fd_obj->fd;

    struct sockaddr_un addr = { 0 };
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SVR_PATH, sizeof(addr.sun_path) - 1);

    int ret = connect(s, (struct sockaddr *)&addr, sizeof(addr));
    if(ret == 0) {
        return enif_make_tuple2(env, s_ok, argv[0]);
    }
    if( errno == EINPROGRESS ) {
        return enif_make_tuple2(env, s_ok, argv[0]);
    } else if( errno == EINTR ) {
        return enif_schedule_nif(env, "try_connect", 0, try_connect, 1, argv);
    } else {
        return make_error(env, errno);
    }
}

static ERL_NIF_TERM connect_svr(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    (void)argc;
    (void)argv;

    struct fd_object_t * fd_obj = alloc_fd_object(env);
    if(fd_obj == NULL) {
        return make_error(env, ENOMEM);
    }

    ERL_NIF_TERM ret;

    fd_obj->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd_obj->fd == -1) {
        goto error;
    }
    if(-1 == fcntl(fd_obj->fd, F_SETFL, fcntl(fd_obj->fd, F_GETFL) | O_NONBLOCK)) {
        goto error;
    }
#ifdef __APPLE__
    if(-1 == setsockopt(fd_obj->fd, SOL_SOCKET, SO_NOSIGPIPE, &(int){1}, sizeof(int))) {
        goto error;
    }
#endif

    ERL_NIF_TERM res = enif_make_resource(env, fd_obj);
    ret = enif_schedule_nif(env, "try_connect", 0, try_connect, 1, &res);
    goto exit;

error:
    ret = make_error(env, errno);

exit:
    enif_release_resource(fd_obj);
    return ret;
}

static ERL_NIF_TERM controlling_process(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    void *obj;
    ErlNifPid pid;
    if( argc != 2 || !enif_get_resource(env, argv[0], s_fdrt, &obj) || !enif_get_local_pid(env, argv[1], &pid) ) {
        return enif_make_badarg(env);
    }
    struct fd_object_t * fd_obj = obj;

    ErlNifPid self;
    if( enif_compare_pids(&fd_obj->cp, enif_self(env, &self)) != 0 ) {
        return enif_make_tuple2(env, s_error, s_not_owner);
    }

    if( enif_compare_pids(&fd_obj->cp, &pid) != 0 ) {
        fd_obj->cp = pid;
        enif_demonitor_process(env, fd_obj, &fd_obj->mon);
        if(enif_monitor_process(env, fd_obj, &pid, &fd_obj->mon) != 0) {
            enif_select(env, fd_obj->fd, ERL_NIF_SELECT_STOP, fd_obj, NULL, enif_make_ref(env));
        }
    }

    return s_ok;
}

static ErlNifFunc nif_funcs[] =
{
    {"connect", 0, connect_svr, 0},
    {"send_request", 2, send_request, 0},
    {"recv_response", 2, recv_response, 0},
    {"controlling_process", 2, controlling_process, 0}
};

ERL_NIF_INIT(Elixir.Tundra.Client,nif_funcs,load,NULL,NULL,NULL)
