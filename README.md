# Tundra

[![Hex](https://img.shields.io/hexpm/v/tundra.svg)](https://hex.pm/packages/tundra)
[![Docs](https://img.shields.io/badge/hex-docs-lightgreen.svg)](https://hexdocs.pm/tundra/)
[![License](https://img.shields.io/hexpm/l/tundra.svg)](https://github.com/ausimian/tundra/blob/master/LICENSE.md)
![github actions badge](https://github.com/ausimian/tundra/actions/workflows/build.yaml/badge.svg)

TUN device support for Elixir.

Tundra provides a simple API for creating and using TUN devices on Linux and Darwin.

## Architecture

Tundra consists of two components:

1. **Elixir Library** - Provides the Elixir API and NIF (Native Implemented Function) for communicating with the server
2. **Standalone Server** ([`server/`](server/)) - A privileged C daemon that creates and configures TUN devices

As TUN device creation is a privileged operation on most systems, Tundra uses a
client-server architecture. The server runs with elevated privileges and creates TUN
devices on behalf of unprivileged clients. Communication happens via a Unix domain
socket (`/var/run/tundra.sock`), and file descriptors are passed using `SCM_RIGHTS`.

Once created, the device is represented within the Elixir runtime as a NIF resource
with process-ownership semantics:

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

### Server

The server must be built and run separately with elevated privileges:

```bash
cd server
make
sudo ./tundra_server
```

See [`server/README.md`](server/README.md) for detailed server documentation.

## Platform Support

- **Linux**: TUN devices via `/dev/net/tun`, configured with netlink
- **Darwin/macOS**: utun devices via kernel control API, configured with ifconfig

## Usage

Use `Tundra.create/2` to create a new TUN device with full configuration:

```elixir
{:ok, {dev, name}} = Tundra.create("fd11:b7b7:4360::2",
  dstaddr: "fd11:b7b7:4360::1",
  netmask: "ffff:ffff:ffff:ffff::",
  mtu: 1500)
```

See the module documentation for complete API details.


