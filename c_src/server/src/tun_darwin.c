/*
 * tun_darwin.c - Darwin/macOS utun device implementation
 *
 * Creates utun devices using the kernel control API
 */

#ifdef __APPLE__

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/socket.h>
#include <sys/sys_domain.h>
#include <unistd.h>
#include <net/if_utun.h>
#include "server.h"

#define UTUN_CONTROL_NAME "com.apple.net.utun_control"

static void exit_error(const char *msg)
{
    perror(msg);
    exit(1);
}

/*
 * Create utun device - error-returning version
 * Returns: fd on success, -errno on error
 * Fills in resp->name with device name
 */
int tun_create_safe(struct create_tun_response_t *resp)
{
    int tun = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (tun == -1)
    {
        return -errno;
    }

    struct ctl_info info = {0};
    strncpy(info.ctl_name, UTUN_CONTROL_NAME, sizeof(info.ctl_name));
    if (ioctl(tun, CTLIOCGINFO, &info) == -1)
    {
        int err = errno;
        close(tun);
        return -err;
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
        int err = errno;
        close(tun);
        return -err;
    }

    // Get the actual interface name that was assigned
    socklen_t ifname_len = sizeof(resp->name);
    if (getsockopt(tun, SYSPROTO_CONTROL, UTUN_OPT_IFNAME,
                   resp->name, &ifname_len) == -1)
    {
        int err = errno;
        close(tun);
        return -err;
    }

    resp->name[sizeof(resp->name) - 1] = '\0';

    // Set non-blocking mode for NIF use
    int flags = fcntl(tun, F_GETFL, 0);
    if (flags != -1)
    {
        fcntl(tun, F_SETFL, flags | O_NONBLOCK);
    }

    return tun;
}

/*
 * Configure utun device using ioctl - error-returning version
 * Returns: 0 on success, -errno on error
 */
int tun_configure_safe(const char *name, const struct create_tun_request_t *msg)
{
    int fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd == -1)
    {
        return -errno;
    }

    struct in6_aliasreq ifr6;
    memset(&ifr6, 0, sizeof(ifr6));
    strncpy(ifr6.ifra_name, name, sizeof(ifr6.ifra_name));

    // Set the address
    ifr6.ifra_addr.sin6_len = sizeof(ifr6.ifra_addr);
    ifr6.ifra_addr.sin6_family = AF_INET6;
    if (inet_pton(AF_INET6, msg->addr, &ifr6.ifra_addr.sin6_addr) != 1)
    {
        close(fd);
        return -EINVAL;
    }

    // Set the prefix mask from netmask
    ifr6.ifra_prefixmask.sin6_len = sizeof(ifr6.ifra_prefixmask);
    ifr6.ifra_prefixmask.sin6_family = AF_INET6;
    if (inet_pton(AF_INET6, msg->netmask, &ifr6.ifra_prefixmask.sin6_addr) != 1)
    {
        close(fd);
        return -EINVAL;
    }

    // Set destination address for point-to-point interface (if provided)
    if (msg->dstaddr[0] != '\0')
    {
        ifr6.ifra_dstaddr.sin6_len = sizeof(ifr6.ifra_dstaddr);
        ifr6.ifra_dstaddr.sin6_family = AF_INET6;
        if (inet_pton(AF_INET6, msg->dstaddr, &ifr6.ifra_dstaddr.sin6_addr) != 1)
        {
            close(fd);
            return -EINVAL;
        }
    }

    // Set lifetime to infinite
    ifr6.ifra_lifetime.ia6t_vltime = ND6_INFINITE_LIFETIME;
    ifr6.ifra_lifetime.ia6t_pltime = ND6_INFINITE_LIFETIME;

    // Add the IPv6 address
    if (ioctl(fd, SIOCAIFADDR_IN6, &ifr6) == -1)
    {
        int err = errno;
        close(fd);
        return -err;
    }

    // Set MTU
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
    ifr.ifr_mtu = msg->mtu;
    if (ioctl(fd, SIOCSIFMTU, &ifr) == -1)
    {
        int err = errno;
        close(fd);
        return -err;
    }

    // Bring interface up
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
    if (ioctl(fd, SIOCGIFFLAGS, &ifr) == -1)
    {
        int err = errno;
        close(fd);
        return -err;
    }

    ifr.ifr_flags |= IFF_UP;
    if (ioctl(fd, SIOCSIFFLAGS, &ifr) == -1)
    {
        int err = errno;
        close(fd);
        return -err;
    }

    close(fd);
    return 0;
}

// Server-facing wrapper that exits on error
int tun_create(struct response_t *resp)
{
    int result = tun_create_safe(&resp->msg.create_tun);
    if (result < 0)
    {
        errno = -result;
        exit_error("tun_create_safe");
    }
    resp->type = REQUEST_TYPE_CREATE_TUN;
    return result;
}

// Server-facing wrapper that exits on error
void tun_configure(const char *name, struct create_tun_request_t *msg)
{
    int result = tun_configure_safe(name, msg);
    if (result < 0)
    {
        errno = -result;
        exit_error("tun_configure_safe");
    }
}

#endif // __APPLE__
