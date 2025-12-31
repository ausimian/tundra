#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#define HAVE_SYS_UIO_H 1
#include <sys/un.h>
#include <unistd.h>
#include <erl_nif.h>
#include <erl_driver.h>
#include "../server/src/protocol.h"

// Platform-specific MSG_NOSIGNAL flag
#ifdef __APPLE__
#define TUNDRA_MSG_NOSIGNAL 0
#elif __linux__
#define TUNDRA_MSG_NOSIGNAL MSG_NOSIGNAL
#endif

static ErlNifResourceType *s_fdrt;

static ERL_NIF_TERM s_ok;
static ERL_NIF_TERM s_error;
static ERL_NIF_TERM s_eagain;
static ERL_NIF_TERM s_not_owner;
static ERL_NIF_TERM s_addr;
static ERL_NIF_TERM s_dstaddr;
static ERL_NIF_TERM s_netmask;
static ERL_NIF_TERM s_mtu;
static ERL_NIF_TERM s_recv;
static ERL_NIF_TERM s_send;
static ERL_NIF_TERM s_select;
static ERL_NIF_TERM s_select_info;
static ERL_NIF_TERM s_socket;
static ERL_NIF_TERM s_tundra;

struct fd_object_t
{
    int fd;
    ErlNifPid cp;
    ErlNifMonitor mon;
};

static void fdrt_dtor(ErlNifEnv *env, void *obj)
{
    (void)env;
    struct fd_object_t *fd_obj = obj;
    int s = fd_obj->fd;
    if (s != -1 && atomic_compare_exchange_strong((atomic_int *)&fd_obj->fd, &s, -1))
    {
        close(s);
    }
}

static void fdrt_stop(ErlNifEnv *env, void *obj, ErlNifEvent event, int is_direct_call)
{
    (void)event;
    (void)is_direct_call;
    fdrt_dtor(env, obj);
}

static void fdrt_down(ErlNifEnv *env, void *obj, ErlNifPid *pid, ErlNifMonitor *mon)
{
    (void)mon;
    struct fd_object_t *fd_obj = obj;
    if (enif_compare_pids(&fd_obj->cp, pid) == 0)
    {
        enif_select(env, fd_obj->fd, ERL_NIF_SELECT_STOP, fd_obj, NULL, enif_make_ref(env));
    }
}

static const ErlNifResourceTypeInit s_fdrt_init = {
    .dtor = fdrt_dtor,
    .stop = fdrt_stop,
    .down = fdrt_down,
    .members = 3,
    .dyncall = NULL};

static struct fd_object_t *alloc_fd_object(ErlNifEnv *env)
{
    struct fd_object_t *fd_obj = enif_alloc_resource(s_fdrt, sizeof(*fd_obj));
    if (fd_obj != NULL)
    {
        fd_obj->fd = -1;
        if (NULL == enif_self(env, &fd_obj->cp) || enif_monitor_process(env, fd_obj, &fd_obj->cp, &fd_obj->mon) != 0)
        {
            enif_release_resource(fd_obj);
            fd_obj = NULL;
        }
    }
    return fd_obj;
}

static ERL_NIF_TERM make_error(ErlNifEnv *env, int err)
{
    return enif_make_tuple2(env, s_error, enif_make_atom(env, erl_errno_id(err)));
}

static int load(ErlNifEnv *env, void **priv_data, ERL_NIF_TERM load_info)
{
    (void)priv_data;
    (void)load_info;
    s_ok = enif_make_atom(env, "ok");
    s_error = enif_make_atom(env, "error");
    s_eagain = enif_make_atom(env, "eagain");
    s_not_owner = enif_make_atom(env, "not_owner");
    s_addr = enif_make_atom(env, "addr");
    s_dstaddr = enif_make_atom(env, "dstaddr");
    s_netmask = enif_make_atom(env, "netmask");
    s_mtu = enif_make_atom(env, "mtu");
    s_recv = enif_make_atom(env, "recv");
    s_send = enif_make_atom(env, "send");
    s_select = enif_make_atom(env, "select");
    s_select_info = enif_make_atom(env, "select_info");
    s_socket = enif_make_atom(env, "$socket");
    s_tundra = enif_make_atom(env, "$tundra");
    s_fdrt = enif_init_resource_type(env, "fdrt", &s_fdrt_init, ERL_NIF_RT_CREATE, NULL);
    return s_fdrt ? 0 : -1;
}

static ERL_NIF_TERM recv_response(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    void *obj;
    if (argc != 2 || !enif_get_resource(env, argv[0], s_fdrt, &obj) || !enif_is_ref(env, argv[1]))
    {
        return enif_make_badarg(env);
    }
    struct fd_object_t *fd_obj = obj;
    ErlNifPid self;
    if (enif_compare_pids(&fd_obj->cp, enif_self(env, &self)) != 0)
    {
        return enif_make_tuple2(env, s_error, s_not_owner);
    }
    int s = fd_obj->fd;

    struct response_t resp = {0};
    char cmsgbuf[CMSG_SPACE(sizeof(int))];
    struct iovec iov = {
        .iov_base = &resp,
        .iov_len = sizeof(resp)};
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = cmsgbuf,
        .msg_controllen = sizeof cmsgbuf,
    };

    int ret = recvmsg(s, &msg, 0);
    if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
        if (enif_select(env, s, ERL_NIF_SELECT_READ, obj, NULL, argv[1]) < 0)
        {
            return enif_make_badarg(env);
        }
        return enif_make_tuple2(env, s_error, s_eagain);
    }

    if (ret == -1)
    {
        return make_error(env, errno);
    }

    // Read the aux data and check for the file descriptor
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == NULL || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS || cmsg->cmsg_len != CMSG_LEN(sizeof(int)))
    {
        return make_error(env, EINVAL);
    }
    // Extract the file descriptor
    int fd;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));

    ERL_NIF_TERM result;

    // Allocate a resource for the file descriptor so that it becomes owned by the calling process
    struct fd_object_t *res_fd = alloc_fd_object(env);
    if (res_fd)
    {
        res_fd->fd = fd;
        // Allocate a binary to hold the name of the tun device
        ErlNifBinary name_bin;
        if (enif_alloc_binary(strlen(resp.msg.create_tun.name), &name_bin))
        {
            strcpy((char *)name_bin.data, resp.msg.create_tun.name);

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

    if (fd >= 0)
    {
        close(fd);
    }

    return result;
}

static ERL_NIF_TERM send_request(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    void *obj;
    if (argc != 3 || !enif_get_resource(env, argv[0], s_fdrt, &obj) || !enif_is_ref(env, argv[1]) || !enif_is_map(env, argv[2]))
    {
        return enif_make_badarg(env);
    }
    struct fd_object_t *fd_obj = obj;
    ErlNifPid self;
    if (enif_compare_pids(&fd_obj->cp, enif_self(env, &self)) != 0)
    {
        return enif_make_tuple2(env, s_error, s_not_owner);
    }
    int s = fd_obj->fd;

    struct request_t req = {
        .type = REQUEST_TYPE_CREATE_TUN,
        .msg.create_tun = {
            .size = sizeof(struct create_tun_request_t)}};

    ErlNifMapIterator iter;
    if (!enif_map_iterator_create(env, argv[2], &iter, ERL_NIF_MAP_ITERATOR_FIRST))
    {
        return enif_make_badarg(env);
    }
    ERL_NIF_TERM key, value;
    bool ok = true;
    while (ok && enif_map_iterator_get_pair(env, &iter, &key, &value))
    {
        if (0 == enif_compare(key, s_addr))
        {
            ok = !!enif_get_string(env, value, req.msg.create_tun.addr, sizeof(req.msg.create_tun.addr), ERL_NIF_UTF8);
        }
        else if (0 == enif_compare(key, s_dstaddr))
        {
            ok = !!enif_get_string(env, value, req.msg.create_tun.dstaddr, sizeof(req.msg.create_tun.dstaddr), ERL_NIF_UTF8);
        }
        else if (0 == enif_compare(key, s_netmask))
        {
            ok = !!enif_get_string(env, value, req.msg.create_tun.netmask, sizeof(req.msg.create_tun.netmask), ERL_NIF_UTF8);
        }
        else if (0 == enif_compare(key, s_mtu))
        {
            ok = !!enif_get_int(env, value, &req.msg.create_tun.mtu);
        }

        enif_map_iterator_next(env, &iter);
    }
    enif_map_iterator_destroy(env, &iter);
    if (!ok)
    {
        return enif_make_badarg(env);
    }

    int rc = send(s, &req, sizeof(req), TUNDRA_MSG_NOSIGNAL);
    if (rc == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
        if (enif_select(env, s, ERL_NIF_SELECT_WRITE, obj, NULL, argv[1]) < 0)
        {
            return enif_make_badarg(env);
        }
        return enif_make_tuple2(env, s_error, s_eagain);
    }
    if (rc == -1)
    {
        return make_error(env, errno);
    }

    return s_ok;
}

static ERL_NIF_TERM try_connect(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    void *obj;
    if (argc != 1 || !enif_get_resource(env, argv[0], s_fdrt, &obj))
    {
        return enif_make_badarg(env);
    }
    struct fd_object_t *fd_obj = obj;
    int s = fd_obj->fd;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SVR_PATH, sizeof(addr.sun_path) - 1);

    int ret = connect(s, (struct sockaddr *)&addr, sizeof(addr));
    if (ret == 0)
    {
        return enif_make_tuple2(env, s_ok, argv[0]);
    }
    if (errno == EINPROGRESS)
    {
        return enif_make_tuple2(env, s_ok, argv[0]);
    }
    else if (errno == EINTR)
    {
        return enif_schedule_nif(env, "try_connect", 0, try_connect, argc, argv);
    }
    else
    {
        return make_error(env, errno);
    }
}

static ERL_NIF_TERM connect_svr(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    (void)argc;
    (void)argv;

    struct fd_object_t *fd_obj = alloc_fd_object(env);
    if (fd_obj == NULL)
    {
        return make_error(env, ENOMEM);
    }

    ERL_NIF_TERM ret;

    fd_obj->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_obj->fd == -1)
    {
        goto error;
    }
    if (-1 == fcntl(fd_obj->fd, F_SETFL, fcntl(fd_obj->fd, F_GETFL) | O_NONBLOCK))
    {
        goto error;
    }
#ifdef __APPLE__
    if (-1 == setsockopt(fd_obj->fd, SOL_SOCKET, SO_NOSIGPIPE, &(int){1}, sizeof(int)))
    {
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

static ERL_NIF_TERM controlling_process(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    void *obj;
    ErlNifPid pid;
    if (argc != 2 || !enif_get_resource(env, argv[0], s_fdrt, &obj) || !enif_get_local_pid(env, argv[1], &pid))
    {
        return enif_make_badarg(env);
    }
    struct fd_object_t *fd_obj = obj;

    ErlNifPid self;
    if (enif_compare_pids(&fd_obj->cp, enif_self(env, &self)) != 0)
    {
        return enif_make_tuple2(env, s_error, s_not_owner);
    }

    if (enif_compare_pids(&fd_obj->cp, &pid) != 0)
    {
        fd_obj->cp = pid;
        enif_demonitor_process(env, fd_obj, &fd_obj->mon);
        if (enif_monitor_process(env, fd_obj, &pid, &fd_obj->mon) != 0)
        {
            enif_select(env, fd_obj->fd, ERL_NIF_SELECT_STOP, fd_obj, NULL, enif_make_ref(env));
        }
    }

    return s_ok;
}

static ERL_NIF_TERM close_fd(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    void *obj;
    if (argc != 1 || !enif_get_resource(env, argv[0], s_fdrt, &obj))
    {
        return enif_make_badarg(env);
    }

    struct fd_object_t *fd_obj = obj;
    ErlNifPid self;
    if (enif_compare_pids(&fd_obj->cp, enif_self(env, &self)) != 0)
    {
        return enif_make_tuple2(env, s_error, s_not_owner);
    }

    int ret = enif_select(env, fd_obj->fd, ERL_NIF_SELECT_STOP, fd_obj, NULL, enif_make_ref(env));
    if (ret < 0)
    {
        if (ret & ERL_NIF_SELECT_FAILED)
        {
            return make_error(env, errno);
        }
        else
        {
            return make_error(env, EINVAL);
        }
    }

    return s_ok;
}

static ERL_NIF_TERM get_fd(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    void *obj;
    if (argc != 1 || !enif_get_resource(env, argv[0], s_fdrt, &obj))
    {
        return enif_make_badarg(env);
    }

    struct fd_object_t *fd_obj = obj;
    return enif_make_int(env, fd_obj->fd);
}

static ERL_NIF_TERM recv_data(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    void *obj;
    if (argc != 2 || !enif_get_resource(env, argv[0], s_fdrt, &obj))
    {
        return enif_make_badarg(env);
    }
    struct fd_object_t *fd_obj = obj;

    ErlNifPid self;
    if (enif_compare_pids(&fd_obj->cp, enif_self(env, &self)) != 0)
    {
        return enif_make_tuple2(env, s_error, s_not_owner);
    }

    int length;
    if (!enif_get_int(env, argv[1], &length) || length <= 0)
    {
        return enif_make_badarg(env);
    }

    ErlNifBinary buf;
    if (!enif_alloc_binary(length, &buf))
    {
        return make_error(env, ENOMEM);
    }

    ssize_t n = read(fd_obj->fd, buf.data, buf.size);

    ERL_NIF_TERM ret;
    if (n == -1)
    {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK)
        {
            ERL_NIF_TERM ref = enif_make_ref(env);
            ERL_NIF_TERM obj = enif_make_tuple2(env, s_tundra, argv[0]);
            ERL_NIF_TERM msg = enif_make_tuple4(env, s_socket, obj, s_select, ref);
            if (enif_select_read(env, fd_obj->fd, fd_obj, NULL, msg, NULL) >= 0)
            {
                ret = enif_make_tuple2(env, s_select, enif_make_tuple3(env, s_select_info, s_recv, ref));
            }
            else
            {
                ret = make_error(env, errno);
            }
        }
        else
        {
            ret = make_error(env, err);
        }
    }
    else
    {
        ERL_NIF_TERM bin = enif_make_binary(env, &buf);
        if (n < length)
        {
            bin = enif_make_sub_binary(env, bin, 0, n);
        }

        ret = enif_make_tuple2(env, s_ok, bin);
    }

    enif_release_binary(&buf);
    return ret;
}

static ERL_NIF_TERM send_data(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    void *obj;
    if (argc != 2 || !enif_get_resource(env, argv[0], s_fdrt, &obj))
    {
        return enif_make_badarg(env);
    }
    struct fd_object_t *fd_obj = obj;

    ErlNifPid self;
    if (enif_compare_pids(&fd_obj->cp, enif_self(env, &self)) != 0)
    {
        return enif_make_tuple2(env, s_error, s_not_owner);
    }

    ErlNifIOVec *iovec = NULL;
    ERL_NIF_TERM tail;
    if (!enif_inspect_iovec(env, 0, argv[1], &tail, &iovec))
    {
        return enif_make_badarg(env);
    }
    ssize_t n = writev(fd_obj->fd, iovec->iov, iovec->iovcnt);

    if (n < 0)
    {
        int err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK)
        {
            // Use a notifaction msg similar to the one used by the erlang socket support
            ERL_NIF_TERM ref = enif_make_ref(env);
            ERL_NIF_TERM obj = enif_make_tuple2(env, s_tundra, argv[0]);
            ERL_NIF_TERM msg = enif_make_tuple4(env, s_socket, obj, s_select, ref);
            if (enif_select_write(env, fd_obj->fd, fd_obj, NULL, msg, NULL) >= 0)
            {
                return enif_make_tuple2(env, s_select, enif_make_tuple3(env, s_select_info, s_send, ref));
            }
            else
            {
                return make_error(env, errno);
            }
        }
        else
        {
            return make_error(env, err);
        }
    }
    else if ((size_t)n == iovec->size)
    {
        return s_ok;
    }
    else
    {
        return make_error(env, ENOBUFS);
    }
}

static ERL_NIF_TERM cancel_select(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    void *obj;
    if (argc != 2 || !enif_get_resource(env, argv[0], s_fdrt, &obj))
    {
        return enif_make_badarg(env);
    }

    struct fd_object_t *fd_obj = obj;
    ErlNifPid self;
    if (enif_compare_pids(&fd_obj->cp, enif_self(env, &self)) != 0)
    {
        return enif_make_tuple2(env, s_error, s_not_owner);
    }

    const ERL_NIF_TERM *select_info;
    int arity;
    if (!enif_get_tuple(env, argv[1], &arity, &select_info) || arity != 3)
    {
        return enif_make_badarg(env);
    }
    if (enif_compare(select_info[0], s_select_info) != 0)
    {
        return enif_make_badarg(env);
    }
    if (!enif_is_ref(env, select_info[2]))
    {
        return enif_make_badarg(env);
    }

    if (enif_compare(select_info[1], s_send) == 0 || enif_compare(select_info[1], s_recv) == 0)
    {
        enum ErlNifSelectFlags flags = enif_compare(select_info[1], s_send) == 0
                                           ? ERL_NIF_SELECT_WRITE
                                           : ERL_NIF_SELECT_READ;

        int res = enif_select(env, fd_obj->fd, flags | ERL_NIF_SELECT_STOP, fd_obj, NULL, select_info[2]);
        if (res < 0)
        {
            return make_error(env, errno);
        }
        else
        {
            return s_ok;
        }
    }
    else
    {
        return enif_make_badarg(env);
    }
}

static ErlNifFunc nif_funcs[] =
    {
        {"connect", 0, connect_svr, 0},
        {"close", 1, close_fd, 0},
        {"send_request", 3, send_request, 0},
        {"recv_response", 2, recv_response, 0},
        {"get_fd", 1, get_fd, 0},
        {"recv_data", 2, recv_data, 0},
        {"send_data", 2, send_data, 0},
        {"cancel_select", 2, cancel_select, 0},
        {"controlling_process", 2, controlling_process, 0}};

ERL_NIF_INIT(Elixir.Tundra.Client, nif_funcs, load, NULL, NULL, NULL)
