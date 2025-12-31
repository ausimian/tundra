/*
 * tun_linux.c - Linux TUN device implementation
 *
 * Creates TUN devices using /dev/net/tun and configures via netlink
 */

#ifdef __linux__

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include "server.h"

static void exit_error(const char *msg)
{
    perror(msg);
    exit(1);
}

static unsigned char netmask_to_prefixlen(const struct in6_addr *netmask)
{
    unsigned char prefixlen = 0;
    int i = 0;
    for (; i < 16 && 0xFF == netmask->s6_addr[i]; i++)
    {
        prefixlen += 8;
    }
    if (i < 16)
    {
        switch (netmask->s6_addr[i])
        {
        case 0x80: prefixlen += 1; break;
        case 0xC0: prefixlen += 2; break;
        case 0xE0: prefixlen += 3; break;
        case 0xF0: prefixlen += 4; break;
        case 0xF8: prefixlen += 5; break;
        case 0xFC: prefixlen += 6; break;
        case 0xFE: prefixlen += 7; break;
        default: break;
        }
    }
    return prefixlen;
}

/*
 * Create TUN device - error-returning version
 * Returns: fd on success, -errno on error
 * Fills in resp->msg.create_tun.name with device name
 */
int tun_create_safe(struct create_tun_response_t *resp)
{
    int tun = open("/dev/net/tun", O_RDWR);
    if (tun == -1)
    {
        return -errno;
    }

    struct ifreq ifr = {0};
    ifr.ifr_flags = IFF_TUN;  // | IFF_NO_PI

    if (ioctl(tun, TUNSETIFF, (void *)&ifr) == -1)
    {
        int err = errno;
        close(tun);
        return -err;
    }

    if (fcntl(tun, F_SETFL, fcntl(tun, F_GETFL) | O_NONBLOCK) == -1)
    {
        int err = errno;
        close(tun);
        return -err;
    }

    strncpy(resp->name, ifr.ifr_name, sizeof(resp->name) - 1);
    resp->name[sizeof(resp->name) - 1] = '\0';

    return tun;
}

/*
 * Configure TUN device - error-returning version
 * Returns: 0 on success, -errno on error
 */
int tun_configure_safe(const char *name, const struct create_tun_request_t *msg)
{
    struct in6_addr addr, dstaddr, netmask;
    static const struct in6_addr zero_addr = {0};

    if (inet_pton(AF_INET6, msg->addr, &addr) != 1)
    {
        return -EINVAL;
    }
    if (inet_pton(AF_INET6, msg->dstaddr, &dstaddr) != 1)
    {
        return -EINVAL;
    }
    if (inet_pton(AF_INET6, msg->netmask, &netmask) != 1)
    {
        return -EINVAL;
    }

    int netlink_fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (netlink_fd == -1)
    {
        return -errno;
    }

    struct sockaddr_nl sockaddr = {.nl_family = AF_NETLINK};
    if (bind(netlink_fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) == -1)
    {
        int err = errno;
        close(netlink_fd);
        return -err;
    }

    if (setsockopt(netlink_fd, SOL_NETLINK, NETLINK_CAP_ACK,
                   &(int){1}, sizeof(int)) == -1)
    {
        int err = errno;
        close(netlink_fd);
        return -err;
    }

    struct
    {
        struct nlmsghdr header;
        struct nlmsgerr content;
    } response = {0};

    // Set IPv6 address
    struct
    {
        struct nlmsghdr header;
        struct ifaddrmsg content;
        char attributes_buf[RTA_LENGTH(sizeof(addr)) + RTA_LENGTH(sizeof(dstaddr))];
    } set_addr = {0};

    size_t attributes_buf_avail = sizeof(set_addr.attributes_buf);

    set_addr.header.nlmsg_len = NLMSG_LENGTH(sizeof(set_addr.content));
    set_addr.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    set_addr.header.nlmsg_type = RTM_NEWADDR;
    set_addr.header.nlmsg_seq = 1;

    set_addr.content.ifa_index = if_nametoindex(name);
    set_addr.content.ifa_family = AF_INET6;
    set_addr.content.ifa_prefixlen = netmask_to_prefixlen(&netmask);

    struct rtattr *request_attr = IFA_RTA(&set_addr.content);
    request_attr->rta_type = IFA_LOCAL;
    request_attr->rta_len = RTA_LENGTH(sizeof(addr));
    set_addr.header.nlmsg_len += request_attr->rta_len;
    memcpy(RTA_DATA(request_attr), &addr, sizeof(addr));

    if (memcmp(&dstaddr, &zero_addr, sizeof(dstaddr)) != 0)
    {
        request_attr = RTA_NEXT(request_attr, attributes_buf_avail);
        request_attr->rta_type = IFA_ADDRESS;
        request_attr->rta_len = RTA_LENGTH(sizeof(dstaddr));
        set_addr.header.nlmsg_len += request_attr->rta_len;
        memcpy(RTA_DATA(request_attr), &dstaddr, sizeof(dstaddr));
    }

    if (send(netlink_fd, &set_addr, set_addr.header.nlmsg_len, 0) !=
        (ssize_t)set_addr.header.nlmsg_len)
    {
        int err = errno;
        close(netlink_fd);
        return -err;
    }

    if (recv(netlink_fd, &response, sizeof(response), 0) != sizeof(response))
    {
        int err = errno;
        close(netlink_fd);
        return -err;
    }
    if (response.content.error != 0)
    {
        close(netlink_fd);
        return response.content.error;
    }

    // Set MTU and bring interface up
    struct
    {
        struct nlmsghdr header;
        struct ifinfomsg content;
        char attributes_buf[RTA_LENGTH(sizeof(msg->mtu))];
    } set_mtu = {0};

    set_mtu.header.nlmsg_len = NLMSG_LENGTH(sizeof(set_mtu.content));
    set_mtu.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    set_mtu.header.nlmsg_type = RTM_SETLINK;
    set_mtu.header.nlmsg_seq = 2;

    set_mtu.content.ifi_index = if_nametoindex(name);
    set_mtu.content.ifi_family = AF_UNSPEC;
    set_mtu.content.ifi_change = IFF_UP;
    set_mtu.content.ifi_flags = IFF_UP;

    request_attr = (struct rtattr *)(((char *)&set_mtu) +
                                     NLMSG_ALIGN(set_mtu.header.nlmsg_len));
    request_attr->rta_type = IFLA_MTU;
    request_attr->rta_len = RTA_LENGTH(sizeof(msg->mtu));
    set_mtu.header.nlmsg_len += request_attr->rta_len;
    memcpy(RTA_DATA(request_attr), &msg->mtu, sizeof(msg->mtu));

    if (send(netlink_fd, &set_mtu, set_mtu.header.nlmsg_len, 0) !=
        (ssize_t)set_mtu.header.nlmsg_len)
    {
        int err = errno;
        close(netlink_fd);
        return -err;
    }
    if (recv(netlink_fd, &response, sizeof(response), 0) != sizeof(response))
    {
        int err = errno;
        close(netlink_fd);
        return -err;
    }
    if (response.content.error != 0)
    {
        close(netlink_fd);
        return response.content.error;
    }

    close(netlink_fd);
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

#endif // __linux__
