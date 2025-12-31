/*
 * protocol.c - Unix socket protocol implementation
 *
 * Handles reading requests and sending responses with file descriptor passing
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include "server.h"

static void exit_error(const char *msg)
{
    perror(msg);
    exit(1);
}

void read_with_retry(int fd, void *buf, size_t count)
{
    size_t total = 0;
    while (total < count)
    {
        ssize_t n = read(fd, (uint8_t *)buf + total, count - total);
        if (n == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            exit_error("read");
        }
        if (n == 0)
        {
            exit(0);
        }
        total += n;
    }
}

void sendfd_with_retry(int dest, int fd, const void *buf, size_t sz)
{
    char cmsgbuf[CMSG_SPACE(sizeof(int))];
    struct iovec iov = {
        .iov_base = (void *)buf,
        .iov_len = sz
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = cmsgbuf,
        .msg_controllen = sizeof(cmsgbuf)
    };

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));

    *((int *)CMSG_DATA(cmsg)) = fd;

    int ret = sendmsg(dest, &msg, TUNDRA_MSG_NOSIGNAL);
    while (ret == -1 && errno == EINTR)
    {
        ret = sendmsg(dest, &msg, TUNDRA_MSG_NOSIGNAL);
    }
    if (ret == -1)
    {
        exit_error("sendmsg");
    }
}
