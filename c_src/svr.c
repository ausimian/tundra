#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include "srv.h"
#if __APPLE__
#include <sys/sys_domain.h>
#include <sys/kern_control.h>
#include <net/if_utun.h>
#elif __linux__
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <linux/ipv6.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#endif

static int exit_error(char *msg)
{
    perror(msg);
    exit(1);
}

static void read_with_retry(int fd, void *buf, size_t count)
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

static void sendfd_with_retry(int dest, int fd, const void *buf, size_t sz)
{
    char cmsgbuf[CMSG_SPACE(sizeof fd)];
    struct iovec iov = {
        .iov_base = (void *)buf,
        .iov_len = sz};
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = cmsgbuf,
        .msg_controllen = sizeof cmsgbuf};

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof fd);

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

static int create_tun(struct response_t *resp)
{
#if __APPLE__
    int tun = socket(AF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (tun == -1)
    {
        exit_error("socket");
    }

    struct ctl_info info = {0};
    strncpy(info.ctl_name, UTUN_CONTROL_NAME, sizeof(info.ctl_name));
    if (ioctl(tun, CTLIOCGINFO, &info) == -1)
    {
        exit_error("ioctl");
    }

    struct sockaddr_sys addr = {
        .ss_len = sizeof(addr),
        .ss_family = AF_SYSTEM,
        .ss_sysaddr = SYSPROTO_CONTROL,
        .ss_reserved = {info.ctl_id, 0},
    };

    if (-1 == connect(tun, (const struct sockaddr *)&addr, sizeof(addr)))
    {
        exit_error("connect");
    }

    socklen_t ifname_len = (int)sizeof(resp->msg.create_tun.name);
    if (-1 == getsockopt(tun, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, resp->msg.create_tun.name, &ifname_len))
    {
        exit_error("getsockopt");
    }
#elif __linux__
    int tun = open("/dev/net/tun", O_RDWR);
    if (tun == -1)
    {
        exit_error("open");
    }

    struct ifreq ifr = {0};
    ifr.ifr_flags = IFF_TUN; // | IFF_NO_PI;

    if (-1 == ioctl(tun, TUNSETIFF, (void *)&ifr))
    {
        exit_error("ioctl");
    }

    if (-1 == fcntl(tun, F_SETFL, fcntl(tun, F_GETFL) | O_NONBLOCK))
    {
        exit_error("fcntl");
    }

    strncpy(resp->msg.create_tun.name, ifr.ifr_name, sizeof(resp->msg.create_tun.name));
#endif
    resp->type = REQUEST_TYPE_CREATE_TUN;

    return tun;
}

#if __linux__
static unsigned char netmask_to_prefixlen(const struct in6_addr *netmask)
{
    unsigned char prefixlen = 0;
    int i = 0;
    for (; i < 16 && 0xFF == netmask->__in6_u.__u6_addr8[i]; i++)
    {
        prefixlen += 8;
    }
    if (i < 16)
    {
        switch (netmask->__in6_u.__u6_addr8[i])
        {
        case 0x80:
            prefixlen += 1;
            break;
        case 0xC0:
            prefixlen += 2;
            break;
        case 0xE0:
            prefixlen += 3;
            break;
        case 0xF0:
            prefixlen += 4;
            break;
        case 0xF8:
            prefixlen += 5;
            break;
        case 0xFC:
            prefixlen += 6;
            break;
        case 0xFE:
            prefixlen += 7;
            break;
        default:
            break;
        }
    }
    return prefixlen;
}
#endif

static void configure_tun(const char *name, struct create_tun_request_t *msg)
{
    struct in6_addr addr = {0};
    if (!inet_pton(AF_INET6, msg->addr, &addr))
    {
        exit_error("inet_pton (addr)");
    }
    struct in6_addr dstaddr = {0};
    if (!inet_pton(AF_INET6, msg->dstaddr, &dstaddr))
    {
        exit_error("inet_pton (addr)");
    }
    struct in6_addr netmask = {0};
    if (!inet_pton(AF_INET6, msg->netmask, &netmask))
    {
        exit_error("inet_pton (netmask)");
    }

#if __APPLE__
    char *cmd = NULL;
    if (asprintf(&cmd, "ifconfig %s inet6 %s netmask %s mtu %d", name, msg->addr, msg->netmask, msg->mtu) >= 0)
    {
        if (0 != system(cmd))
        {
            exit_error("system");
        }
        free(cmd);
    }
#elif __linux__
    static const struct in6_addr zero_addr = {0};

    int netlink_fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (netlink_fd == -1)
    {
        exit_error("socket(netlink)");
    }

    struct sockaddr_nl sockaddr = {.nl_family = AF_NETLINK};
    if (-1 == bind(netlink_fd, (struct sockaddr *)&sockaddr, sizeof sockaddr))
    {
        exit_error("bind(netlink)");
    }

    if (setsockopt(netlink_fd, SOL_NETLINK, NETLINK_CAP_ACK, &(int){1}, sizeof(int)) == -1)
    {
        exit_error("setsockopt(netlink)");
    }

    struct
    {
        struct nlmsghdr header;
        struct nlmsgerr content;
    } response = {0};
    struct rtattr *request_attr;

    struct
    {
        struct nlmsghdr header;
        struct ifaddrmsg content;
        char attributes_buf[RTA_LENGTH(sizeof addr) + RTA_LENGTH(sizeof dstaddr)];
    } set_addr = {0};

    size_t attributes_buf_avail = sizeof set_addr.attributes_buf;

    set_addr.header.nlmsg_len = NLMSG_LENGTH(sizeof set_addr.content);
    set_addr.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    set_addr.header.nlmsg_type = RTM_NEWADDR;
    set_addr.header.nlmsg_seq = 1;

    set_addr.content.ifa_index = if_nametoindex(name);
    set_addr.content.ifa_family = AF_INET6;
    set_addr.content.ifa_prefixlen = netmask_to_prefixlen(&netmask);

    request_attr = IFA_RTA(&set_addr.content);
    request_attr->rta_type = IFA_LOCAL;
    request_attr->rta_len = RTA_LENGTH(sizeof(addr));
    set_addr.header.nlmsg_len += request_attr->rta_len;
    memcpy(RTA_DATA(request_attr), &addr, sizeof addr);

    if (memcmp(&dstaddr, &zero_addr, sizeof dstaddr) != 0)
    {
        request_attr = RTA_NEXT(request_attr, attributes_buf_avail);
        request_attr->rta_type = IFA_ADDRESS;
        request_attr->rta_len = RTA_LENGTH(sizeof dstaddr);
        set_addr.header.nlmsg_len += request_attr->rta_len;
        memcpy(RTA_DATA(request_attr), &dstaddr, sizeof dstaddr);
    }

    if (send(netlink_fd, &set_addr, set_addr.header.nlmsg_len, 0) != set_addr.header.nlmsg_len)
    {
        exit_error("send(netlink,addr)");
    }

    if (recv(netlink_fd, &response, sizeof response, 0) != sizeof response)
    {
        exit_error("recv(netlink,addr)");
    }
    if (response.content.error != 0)
    {
        exit_error("response(addr)");
    }

    struct
    {
        struct nlmsghdr header;
        struct ifinfomsg content;
        char attributes_buf[RTA_LENGTH(sizeof msg->mtu)];
    } set_mtu = {0};

    set_mtu.header.nlmsg_len = NLMSG_LENGTH(sizeof set_mtu.content);
    set_mtu.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    set_mtu.header.nlmsg_type = RTM_SETLINK;
    set_mtu.header.nlmsg_seq = 2;

    set_mtu.content.ifi_index = if_nametoindex(name);
    set_mtu.content.ifi_family = AF_UNSPEC;
    set_mtu.content.ifi_change = IFF_UP;
    set_mtu.content.ifi_flags = IFF_UP;

    request_attr = (struct rtattr *)(((char *)&set_mtu) + NLMSG_ALIGN(set_mtu.header.nlmsg_len));
    request_attr->rta_type = IFLA_MTU;
    request_attr->rta_len = RTA_LENGTH(sizeof(msg->mtu));
    set_mtu.header.nlmsg_len += request_attr->rta_len;
    memcpy(RTA_DATA(request_attr), &msg->mtu, sizeof(msg->mtu));

    if (send(netlink_fd, &set_mtu, set_mtu.header.nlmsg_len, 0) != set_mtu.header.nlmsg_len)
    {
        exit_error("send(netlink,mtu)");
    }
    if (recv(netlink_fd, &response, sizeof response, 0) != sizeof response)
    {
        exit_error("recv(netlink,mtu)");
    }
    if (response.content.error != 0)
    {
        exit_error("response(mtu)");
    }
#endif
}

static void run_child(int c)
{
    struct request_t req;
    struct response_t resp;
    read_with_retry(c, &req, sizeof(req));
    if (req.type == REQUEST_TYPE_CREATE_TUN && req.msg.create_tun.size == sizeof(req.msg.create_tun))
    {
        int tun = create_tun(&resp);
        configure_tun(resp.msg.create_tun.name, &req.msg.create_tun);
        sendfd_with_retry(c, tun, &resp, sizeof(resp));
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

    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s == -1)
    {
        exit_error("socket");
    }

    if (remove(socket_path) == -1 && errno != ENOENT)
    {
        exit_error("remove");
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        exit_error("bind");
    }

    if (chmod(socket_path, 0777) == -1)
    {
        exit_error("chmod");
    }

    if (listen(s, SOMAXCONN) == -1)
    {
        exit_error("listen");
    }

    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
    {
        exit_error("signal");
    }

    for (;;)
    {
        // Accept a connection
        int c = accept(s, NULL, NULL);
        while (c == -1 && errno == EINTR)
        {
            c = accept(s, NULL, NULL);
        }
        if (c == -1)
        {
            exit_error("accept");
        }

        // Fork a child process to handle the connection
        pid_t pid = fork();
        if (pid > 0)
        {
            // Parent process
            close(c);
        }
        else if (pid == 0)
        {
            // Child process
            close(s);
            run_child(c);
            exit(0);
        }
        else
        {
            exit_error("fork");
        }
    }

    return 0;
}
