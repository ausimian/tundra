#pragma once

#include <stddef.h>
#include <net/if.h>
#include <sys/socket.h>

#define SVR_PATH "/var/run/tundra.sock"

#ifdef __APPLE__
#define TUNDRA_MSG_NOSIGNAL 0
#elif __linux__
#define TUNDRA_MSG_NOSIGNAL MSG_NOSIGNAL
#endif

enum request_type_t
{
    REQUEST_TYPE_CREATE_TUN = 0
};

struct create_tun_request_t
{
    size_t size;
    char addr[INET6_ADDRSTRLEN];
    char dstaddr[INET6_ADDRSTRLEN];
    char netmask[INET6_ADDRSTRLEN];
    int mtu;
};

struct create_tun_response_t
{
    size_t size;
    char name[IF_NAMESIZE];
};

struct request_t
{
    enum request_type_t type;
    union
    {
        struct create_tun_request_t create_tun;
    } msg;
};

struct response_t
{
    enum request_type_t type;
    union
    {
        struct create_tun_response_t create_tun;
    } msg;
};
