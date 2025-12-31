/*
 * main.c - Tundra privileged server
 *
 * A Unix domain socket server that creates and configures TUN devices
 * on behalf of unprivileged clients.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include "server.h"

static void exit_error(const char *msg)
{
    perror(msg);
    exit(1);
}

static void run_child(int client_fd)
{
    struct request_t req;
    struct response_t resp;

    read_with_retry(client_fd, &req, sizeof(req));

    if (req.type == REQUEST_TYPE_CREATE_TUN &&
        req.msg.create_tun.size == sizeof(req.msg.create_tun))
    {
        int tun_fd = tun_create(&resp);
        tun_configure(resp.msg.create_tun.name, &req.msg.create_tun);
        sendfd_with_retry(client_fd, tun_fd, &resp, sizeof(resp));
        close(tun_fd);
    }
    else
    {
        exit_error("unknown request type");
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    static const char socket_path[] = SVR_PATH;

    // Create Unix domain socket
    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd == -1)
    {
        exit_error("socket(AF_UNIX)");
    }

    // Remove stale socket file
    if (remove(socket_path) == -1 && errno != ENOENT)
    {
        exit_error("remove");
    }

    // Bind to socket path
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        exit_error("bind");
    }

    // Make socket accessible to all users
    if (chmod(socket_path, 0777) == -1)
    {
        exit_error("chmod");
    }

    // Listen for connections
    if (listen(listen_fd, SOMAXCONN) == -1)
    {
        exit_error("listen");
    }

    // Ignore SIGCHLD to prevent zombie processes
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
    {
        exit_error("signal");
    }

    fprintf(stderr, "Tundra server listening on %s\n", socket_path);

    // Main accept loop
    for (;;)
    {
        int client_fd = accept(listen_fd, NULL, NULL);
        while (client_fd == -1 && errno == EINTR)
        {
            client_fd = accept(listen_fd, NULL, NULL);
        }
        if (client_fd == -1)
        {
            exit_error("accept");
        }

        // Fork child process to handle request
        pid_t pid = fork();
        if (pid > 0)
        {
            // Parent process
            close(client_fd);
        }
        else if (pid == 0)
        {
            // Child process
            close(listen_fd);
            run_child(client_fd);
            close(client_fd);
            exit(0);
        }
        else
        {
            exit_error("fork");
        }
    }

    return 0;
}
