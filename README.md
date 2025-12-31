# Tundra

[![Hex](https://img.shields.io/hexpm/v/tundra.svg)](https://hex.pm/packages/tundra)
[![Docs](https://img.shields.io/badge/hex-docs-lightgreen.svg)](https://hexdocs.pm/tundra/)
[![License](https://img.shields.io/hexpm/l/tundra.svg)](https://github.com/ausimian/tundra/blob/master/LICENSE.md)
![github actions badge](https://github.com/ausimian/tundra/actions/workflows/build.yaml/badge.svg)

TUN device support for Elixir.

Tundra provides a simple API for creating and using TUN devices on Linux and Darwin.

## Architecture

TUN device creation is a privileged operation requiring root on both platforms.
Tundra supports two modes of operation:

1. **Direct creation**: When the BEAM VM runs with sufficient privileges (root or
   `CAP_NET_ADMIN` on Linux), Tundra creates TUN devices directly via the NIF
   without requiring a server process.

2. **Server-based creation**: When the BEAM VM lacks privileges, Tundra uses a
   client-server architecture with a separate privileged daemon ([`tundra_server`](c_src/server/))
   that creates and configures TUN devices on behalf of unprivileged clients.

Tundra automatically attempts direct creation first and falls back to the server if
privileges are insufficient. Communication with the server happens via a Unix domain
socket (`/var/run/tundra.sock`), and file descriptors are passed using `SCM_RIGHTS`.

Once created, the device is represented within the Elixir runtime with process-ownership
semantics:

- Only the owning process can read from or write to the device and receive i/o notifications
- The TUN device is automatically removed when the owning process exits

## Installation

### Elixir Library

Add `tundra` to your list of dependencies in `mix.exs`:

```elixir
def deps do
  [
    {:tundra, "~> 0.1"}
  ]
end
```

### Server (Optional)

The server is only required for unprivileged operation. If your application runs
with root privileges (or `CAP_NET_ADMIN` on Linux), Tundra will create devices
directly and the server is not needed.

For unprivileged operation, build and run the server with elevated privileges:

```bash
cd c_src/server
make
sudo ./tundra_server
```

See [`c_src/server/README.md`](c_src/server/README.md) for detailed server documentation.

## Platform Support

- **Linux**: TUN devices via `/dev/net/tun`, configured with netlink. Supports both direct creation (when privileged) and server-based creation.
- **Darwin/macOS**: utun devices via kernel control API, configured with ioctl. Supports both direct creation (when privileged) and server-based creation.

## Usage

Use `Tundra.create/2` to create a new TUN device with full configuration:

```elixir
{:ok, {dev, name}} = Tundra.create("fd11:b7b7:4360::2",
  dstaddr: "fd11:b7b7:4360::1",
  netmask: "ffff:ffff:ffff:ffff::",
  mtu: 1500)
```

See the module documentation for complete API details.


