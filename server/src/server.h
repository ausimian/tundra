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

// Platform-specific TUN device functions
int tun_create(struct response_t *resp);
void tun_configure(const char *name, struct create_tun_request_t *msg);

// Protocol helpers
void read_with_retry(int fd, void *buf, size_t count);
void sendfd_with_retry(int dest, int fd, const void *buf, size_t sz);
