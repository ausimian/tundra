# Tundra Server

Privileged server component for creating and managing TUN devices on Linux and Darwin.

## Overview

The Tundra server is a standalone C daemon that runs with elevated privileges and creates TUN devices on behalf of unprivileged clients. It communicates via a Unix domain socket and passes file descriptors using `SCM_RIGHTS`.

## Architecture

```
Client (Elixir/NIF)  →  [Unix Socket]  →  Server (C, privileged)
                         /var/run/tundra.sock

Client sends: Request (create/open TUN)
Server sends: Response + TUN device file descriptor
```

## Building

```bash
make
```

This produces `tundra_server` binary.

## Running

The server must run as root (or with `CAP_NET_ADMIN` on Linux):

```bash
sudo ./tundra_server
```

## Protocol

The server implements a simple request/response protocol:

### Request Types
- `REQUEST_TYPE_CREATE_TUN` - Create new TUN device with configuration

### Response
- Returns TUN device file descriptor via `SCM_RIGHTS`
- Returns device name and configuration details

## Platform Support

### Linux
- Opens `/dev/net/tun`
- Uses `ioctl(TUNSETIFF)` to create device
- Configures via netlink

### Darwin/macOS
- Uses utun kernel control API
- Connects to `com.apple.net.utun_control`
- Configures via `ifconfig` (system call)

## Security

- Socket permissions: 0777 (world-writable)
- Access control via Unix socket filesystem permissions
- Each request handled in forked child process
- Server validates all requests before processing
