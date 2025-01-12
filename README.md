# Tundra

[![Hex](https://img.shields.io/hexpm/v/tundra.svg)](https://hex.pm/packages/tundra)
[![Docs](https://img.shields.io/badge/hex-docs-lightgreen.svg)](https://hexdocs.pm/tundra/)
[![License](https://img.shields.io/hexpm/l/tundra.svg)](https://github.com/ausimian/tundra/blob/master/LICENSE.md)
![github actions badge](https://github.com/ausimian/tundra/actions/workflows/build.yaml/badge.svg)

TUN device support for Elixir.

Tundra provides a simple API for creating and using TUN devices on Linux and Darwin.

As TUN device creation is a privileged operation on most systems, Tundra uses a
server process to create and configure TUN devices. Once created, the device is
represented within the runtime as a socket on Darwin and a NIF resource on Linux,
with process-ownership semantics i.e.

- Only the owning process can read from or write to the device and receive i/o
  notifications from it.
- The TUN device is removed when the owning process exits.

## Installation

If [available in Hex](https://hex.pm/docs/publish), the package can be installed
by adding `tundra` to your list of dependencies in `mix.exs`:

```elixir
def deps do
  [
    {:tundra, "~> 0.1"}
  ]
end
```


