#pragma once

#include <stddef.h>
#include <sys/socket.h>
#include "protocol.h"

// Platform-specific MSG_NOSIGNAL flag
#ifdef __APPLE__
#define TUNDRA_MSG_NOSIGNAL 0
#elif __linux__
#define TUNDRA_MSG_NOSIGNAL MSG_NOSIGNAL
#endif

// Platform-specific TUN device functions (server-facing, exit on error)
int tun_create(struct response_t *resp);
void tun_configure(const char *name, struct create_tun_request_t *msg);

// Safe versions that return error codes (for NIF use)
int tun_create_safe(struct create_tun_response_t *resp);
int tun_configure_safe(const char *name, const struct create_tun_request_t *msg);

// Protocol helpers
void read_with_retry(int fd, void *buf, size_t count);
void sendfd_with_retry(int dest, int fd, const void *buf, size_t sz);
