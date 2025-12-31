/*
 * test_client.c - Simple test client for tundra server
 *
 * Connects to the server and requests a TUN device
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "src/server.h"

static void exit_error(const char *msg)
{
    perror(msg);
    exit(1);
}

static int recv_fd(int sock, void *buf, size_t sz)
{
    char cmsgbuf[CMSG_SPACE(sizeof(int))];
    struct iovec iov = {
        .iov_base = buf,
        .iov_len = sz
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = cmsgbuf,
        .msg_controllen = sizeof(cmsgbuf)
    };

    ssize_t n = recvmsg(sock, &msg, 0);
    if (n <= 0)
    {
        exit_error("recvmsg");
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == NULL || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
    {
        fprintf(stderr, "Invalid control message\n");
        exit(1);
    }

    int fd;
    memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));
    return fd;
}

int main(void)
{
    // Connect to server
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1)
    {
        exit_error("socket");
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SVR_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        exit_error("connect");
    }

    printf("Connected to tundra server at %s\n", SVR_PATH);

    // Create request
    struct request_t req = {
        .type = REQUEST_TYPE_CREATE_TUN,
        .msg.create_tun = {
            .size = sizeof(struct create_tun_request_t),
        }
    };

    // Set IPv6 addresses (using fd00::/8 for local testing)
    strncpy(req.msg.create_tun.addr, "fd00::1", sizeof(req.msg.create_tun.addr));
    strncpy(req.msg.create_tun.dstaddr, "fd00::2", sizeof(req.msg.create_tun.dstaddr));
    strncpy(req.msg.create_tun.netmask, "ffff:ffff:ffff:ffff::", sizeof(req.msg.create_tun.netmask));
    req.msg.create_tun.mtu = 1500;

    // Send request
    if (send(sock, &req, sizeof(req), 0) != sizeof(req))
    {
        exit_error("send");
    }

    printf("Sent CREATE_TUN request\n");

    // Receive response
    struct response_t resp;
    int tun_fd = recv_fd(sock, &resp, sizeof(resp));

    printf("Success!\n");
    printf("  Device name: %s\n", resp.msg.create_tun.name);
    printf("  File descriptor: %d\n", tun_fd);
    printf("  Local address: %s\n", req.msg.create_tun.addr);
    printf("  Remote address: %s\n", req.msg.create_tun.dstaddr);
    printf("  MTU: %d\n", req.msg.create_tun.mtu);

    printf("\nYou can verify the interface with: ifconfig %s\n", resp.msg.create_tun.name);
    printf("Press Enter to close and destroy the device...");
    getchar();

    close(tun_fd);
    close(sock);

    printf("Device destroyed\n");
    return 0;
}
