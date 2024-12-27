#pragma once

#include <stddef.h>
#include <net/if.h>
#include <sys/socket.h>

#define SVR_PATH "/var/run/tundra.sock"

enum request_type_t {
    REQUEST_TYPE_CREATE_TUN = 0
};

struct create_tun_request_t {
    size_t size;
    struct sockaddr_storage addr;
    struct sockaddr_storage netmask;
    int mtu;
};

struct create_tun_response_t {
    char name[IFNAMSIZ];
};

struct request_t {
    enum request_type_t type;
    union {
        struct create_tun_request_t create_tun;
    } msg;
};

struct response_t {
    enum request_type_t type;
    union {
        struct create_tun_response_t create_tun;
    } msg;
};

