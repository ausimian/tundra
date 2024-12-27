#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include "srv.h"
#if __APPLE__
#include <sys/sys_domain.h>
#include <sys/kern_control.h>
#include <net/if_utun.h>
#define SENDFLAGS 0
#else
#endif

static int exit_error(char * msg) {
    perror(msg);
    exit(1);
}

static void read_with_retry(int fd, void * buf, size_t count) {
    size_t total = 0;
    while(total < count) {
        ssize_t n = read(fd, (uint8_t*)buf + total, count - total);
        if(n == -1) {
            if(errno == EINTR) {
                continue;
            }
            exit_error("read");
        }
        if(n == 0) {
            exit(0);
        }
        total += n;
    }
}

static void sendfd_with_retry(int dest, int fd, const void * buf, size_t sz) {
    char cmsgbuf[CMSG_SPACE(sizeof fd)];
    struct iovec iov = {
        .iov_base = (void *)buf,
        .iov_len = sz
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = cmsgbuf,
        .msg_controllen = sizeof cmsgbuf
    };

    struct cmsghdr * cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof fd);

    *((int *) CMSG_DATA(cmsg)) = fd;

    int ret = sendmsg(dest, &msg, SENDFLAGS);
    while(ret == -1 && errno == EINTR) {
        ret = sendmsg(dest, &msg, SENDFLAGS);
    }
    if(ret == -1) {
        exit_error("sendmsg");
    }
}

static void create_tun(int c, const struct request_t * req) {

    (void)req;

    int tun = socket(AF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if(tun == -1) {
        exit_error("socket");
    }

    struct ctl_info info = { 0 };
	strncpy(info.ctl_name, UTUN_CONTROL_NAME, sizeof(info.ctl_name));
    if(ioctl(tun, CTLIOCGINFO, &info) == -1) {
        exit_error("ioctl");
    }

	struct sockaddr_sys addr = {
			.ss_len = sizeof(addr),
			.ss_family = AF_SYSTEM,
			.ss_sysaddr = SYSPROTO_CONTROL,
			.ss_reserved = {info.ctl_id, 0},
	};

	if(-1 == connect(tun, (const struct sockaddr *) &addr, sizeof(addr))) {
        exit_error("connect");
    }

    struct response_t res = { .type = REQUEST_TYPE_CREATE_TUN };
    socklen_t ifname_len = ( int )sizeof(res.msg.create_tun.name);
    if(-1 == getsockopt(tun, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, res.msg.create_tun.name, &ifname_len)) {
        exit_error("getsockopt");
    }

    sendfd_with_retry(c, tun, &res, sizeof(res));
}



static void run_child(int c) {
    struct request_t req;
    read_with_retry(c, &req, sizeof(req));
    switch(req.type) {
        case REQUEST_TYPE_CREATE_TUN:
            create_tun(c, &req);
            break;
        default:
            exit_error("unknown request type");
    }
}

int main(int argc, char ** argv) {

    (void)argc;
    (void)argv;

    static const char socket_path[] = SVR_PATH;

    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if(s == -1) {
        exit_error("socket");
    }

    if(remove(socket_path) == -1 && errno != ENOENT) {
        exit_error("remove");
    }

    struct sockaddr_un addr = { 0 };
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if(bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        exit_error("bind");
    }

    if(chmod(socket_path, 0777) == -1) {
        exit_error("chmod");
    }

    if(listen(s, 5) == -1) {
        exit_error("listen");
    }

    if(signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
        exit_error("signal");
    }

    for(;;) {
        // Accept a connection
        int c = accept(s, NULL, NULL);
        while(c == -1 && errno == EINTR) {
            c = accept(s, NULL, NULL);
        }
        if(c == -1) {
            exit_error("accept");
        }

        // Fork a child process to handle the connection
        pid_t pid = fork();
        if(pid > 0) {
            // Parent process
            close(c);
        } else if(pid == 0) {
            // Child process
            close(s);
            run_child(c);
            exit(0);
        } else {
            exit_error("fork");
        }
    }

    return 0;
}
