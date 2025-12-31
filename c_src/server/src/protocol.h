/*
 * protocol.h - Tundra client/server protocol definitions
 *
 * This file defines the core protocol structures used for communication
 * between the Elixir NIF client and the tundra_server daemon.
 */

#pragma once

#include <stddef.h>
#include <net/if.h>
#include <netinet/in.h>

// Unix domain socket path for client/server communication
#define SVR_PATH "/var/run/tundra.sock"

// Request types
enum request_type_t
{
    REQUEST_TYPE_CREATE_TUN = 0
};

// CREATE_TUN request payload
struct create_tun_request_t
{
    size_t size;
    char addr[INET6_ADDRSTRLEN];
    char dstaddr[INET6_ADDRSTRLEN];
    char netmask[INET6_ADDRSTRLEN];
    int mtu;
};

// CREATE_TUN response payload
struct create_tun_response_t
{
    size_t size;
    char name[IF_NAMESIZE];
};

// Request message (sent from client to server)
struct request_t
{
    enum request_type_t type;
    union
    {
        struct create_tun_request_t create_tun;
    } msg;
};

// Response message (sent from server to client, includes FD via SCM_RIGHTS)
struct response_t
{
    enum request_type_t type;
    union
    {
        struct create_tun_response_t create_tun;
    } msg;
};
