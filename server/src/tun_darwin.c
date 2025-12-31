/*
 * tun_darwin.c - Darwin/macOS utun device implementation
 *
 * Creates utun devices using the kernel control API
 */

#ifdef __APPLE__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/socket.h>
#include <sys/sys_domain.h>
#include <net/if_utun.h>
#include "server.h"

#define UTUN_CONTROL_NAME "com.apple.net.utun_control"

static void exit_error(const char *msg)
{
    perror(msg);
    exit(1);
}

int tun_create(struct response_t *resp)
{
    int tun = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (tun == -1)
    {
        exit_error("socket(PF_SYSTEM)");
    }

    struct ctl_info info = {0};
    strncpy(info.ctl_name, UTUN_CONTROL_NAME, sizeof(info.ctl_name));
    if (ioctl(tun, CTLIOCGINFO, &info) == -1)
    {
        exit_error("ioctl(CTLIOCGINFO)");
    }

    struct sockaddr_ctl addr = {
        .sc_len = sizeof(addr),
        .sc_family = AF_SYSTEM,
        .ss_sysaddr = AF_SYS_CONTROL,
        .sc_id = info.ctl_id,
        .sc_unit = 0  // Let system allocate unit number
    };

    if (connect(tun, (const struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        exit_error("connect(AF_SYS_CONTROL)");
    }

    // Get the actual interface name that was assigned
    socklen_t ifname_len = sizeof(resp->msg.create_tun.name);
    if (getsockopt(tun, SYSPROTO_CONTROL, UTUN_OPT_IFNAME,
                   resp->msg.create_tun.name, &ifname_len) == -1)
    {
        exit_error("getsockopt(UTUN_OPT_IFNAME)");
    }

    resp->type = REQUEST_TYPE_CREATE_TUN;
    return tun;
}

void tun_configure(const char *name, struct create_tun_request_t *msg)
{
    // On Darwin, use ifconfig to configure the interface
    // This requires spawning a shell command
    char *cmd = NULL;
    if (asprintf(&cmd, "ifconfig %s inet6 %s netmask %s mtu %d up",
                 name, msg->addr, msg->netmask, msg->mtu) >= 0)
    {
        if (system(cmd) != 0)
        {
            free(cmd);
            exit_error("system(ifconfig)");
        }
        free(cmd);
    }
    else
    {
        exit_error("asprintf");
    }
}

#endif // __APPLE__
