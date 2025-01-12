defmodule Tundra do
  @moduledoc """
  TUN device support for Elixir.

  Tundra provides a simple API for creating and using TUN devices on Linux and Darwin.

  As TUN device creation is a privileged operation on most systems, Tundra uses a
  server process to create and configure TUN devices. Once created, the device is
  represented within the runtime as a socket on Darwin and a NIF resource on Linux,
  with process-ownership semantics i.e.

  - Only the owning process can read from or write to the device and receive i/o
    notifications from it.
  - The TUN device is removed when the owning process exits.

  ## Server Process

  The server program is called `tundra_svr` and is located under the `priv` directory
  of the Tundra application. The `:tundra` application expects that this server is
  already running as a privileged user when it starts.

  The server listens on a Unix domain socket at `/var/run/tundra.sock` and accepts
  requests from the NIF to create and configure the tun device. The resulting
  file descriptor is sent back to the NIF, which then creates a socket from it (on
  Darwin) or wraps it in a NIF resource (on Linux).

  ## Non-blocking I/O

  Tundra only supports non-blocking I/O on TUN devices. The `recv/3` and `send/3`
  functions currently require that `:nowait` is pssed as the last argument. If the
  operation would block, the function will return `{:select, select_info}` and
  a notification of the following form will be sent to the owning process when
  the device is ready:

      {:"$socket", dev, :select, select_handle}

  Note that, although on Linux the underlying TUN device is not technically a
  socket, the same notification is used to reduce platform specific code. Only
  the contents of `dev` differ.

  ## IPv6

  Tundra is designed to work with IPv6 and has only been tested with IPv6.

  ## Example

  The following GenServer creates a TUN device and then relays packets by

  1. Reading a frame from the device
  2. Swapping the source and destination addresses
  3. Writing the modified frame back to the device

  ```elixir
  defmodule Reflector do
    use GenServer

    @mtu 1500

    def start_link(_), do: GenServer.start_link(__MODULE__, [], name: __MODULE__)

    @impl true
    def init(_args) do
      # Create a TUN device with the given address and options
      {:ok, state} =
        Tundra.create("fd11:b7b7:4360::2",
          dstaddr: "fd11:b7b7:4360::1",
          netmask: "ffff:ffff:ffff:ffff::",
          mtu: @mtu)
      {:ok, state, {:continue, :read}}
    end

    @impl true
    def handle_continue(:read, {dev, _} = state) do
      # Read a frame from the device
      case Tundra.recv(dev, @mtu, :nowait) do
        {:ok, data} ->
          {:noreply, state, {:continue, {:reflect, data}}}
        {:select, _} ->
          {:noreply, state}
      end
    end

    def handle_continue({:reflect, data}, {dev, _} = state) do
      # Swap the source and destination addresses of the ipv6 frame
      <<hdr::binary-size(4),
        pre::binary-size(8),
        src::binary-size(16),
        dst::binary-size(16),
        rest::binary>> = data
      reflected = [hdr, pre, dst, src, rest]

      # Write the frame back to the device, assume it won't block
      :ok = Tundra.send(dev, reflected, :nowait)
      {:noreply, state, {:continue, :read}}
    end

    @impl true
    def handle_info({:"$socket", dev, :select, _}, {dev, _} = state) do
      # Handle 'input ready' notifications
      {:noreply, state, {:continue, :read}}
    end

  end
  ```

  """

  @typedoc """
  A TUN device.

  On Darwin, this is a socket. On Linux, this is a reference to a NIF resource.
  """
  @type tun_device() :: :socket.socket() | {:"$tundra", reference()}

  @typedoc """
  A TUN device address. May be represented either as tuple or a
  string containing a dotted IP address.
  """
  @type tun_address() :: String.t() | tuple()

  @typedoc """
  A TUN device creation option.
  """
  @type tun_option() ::
          {:dstaddr, tun_address()} |
          {:netmask, tun_address()} |
          {:mtu, non_neg_integer()}

  @spec create(tun_address(), list(tun_option())) :: {:ok, {tun_device(), String.t()}} | {:error, any()}
  @doc """
  Create a TUN device.

  Creates a new TUN device with the given address and options. The options currently
  supported are:

  - `:netmask` - The netmask of the device.
  - `:dstaddr` - The destination address of the device.
  - `:mtu` - The maximum transmission unit of the device.

  On success returns a tuple containing a device tuple and the name of the device.

  ## Examples

      iex> Tundra.create("fd11:b7b7:4360::2",
              dstaddr: "fd11:b7b7:4360::1",
              netmask: "ffff:ffff:ffff:ffff::",
              mtu: 16000)
      {:ok, {{:"$socket", #Reference<0.2990923237.3512074243.109526>}, "utun6"}} # Darwin
      {:ok, {{:"$tundra", #Reference<0.2990923237.3512074243.109526>}, "tun0"}}  # Linux
  """
  def create(address, opts \\ []) do
    alias Tundra.DynamicSupervisor, as: Sup

    case convert_opts(Keyword.put(opts, :addr, address)) do
      params when is_map(params) ->
        with {:ok, pid} <- DynamicSupervisor.start_child(Sup, Tundra.Client) do
          Tundra.Client.create_tun_device(pid, params)
        end

      error ->
        error
    end
  end

  @doc """
  Transfer control of a TUN device to another process.

  Must be called by the current owner of the device.
  """
  @spec controlling_process(tun_device(), pid()) :: :ok | {:error, any()}
  def controlling_process({:"$socket", _} = sock, pid) when is_pid(pid) do
    :socket.setopt(sock, {:otp, :controlling_process}, pid)
  end
  def controlling_process({:"$tundra", ref}, pid) when is_pid(pid) do
    Tundra.Client.controlling_process(ref, pid)
  end

  @doc """
  Receive data from a TUN device.

  The `length` argument specifies the maximum number of bytes to read. The caller
  is responsible for ensuring this length is at least as large at the MTU of the
  device, plus the size of the TUN header (4 bytes).

  The `:nowait` option specifies that the operation should not block if no data is
  available. If data is available, it will be returned immediately. If no data is
  available, the function will return `{:select, select_info}`.
  """
  @spec recv(tun_device(), non_neg_integer(), :nowait) :: {:ok, binary()} | {:select, :socket.select_info()}, {:error, any()}
  def recv({:"$socket", _} = sock, length, :nowait) when is_integer(length) do
    :socket.recv(sock, length, [], :nowait)
  end
  def recv({:"$tundra", ref}, length, :nowait) when is_integer(length) do
    Tundra.Client.recv(ref, length, [], :nowait)
  end

  @doc """
  Send data to a TUN device.

  The `data` argument is an iodata that will be written to the device. The data should
  include the 4-byte header that is expected by the TUN device.

  The `:nowait` option specifies that the operation should not block if the device's
  output buffer is full. If the buffer is full, the function will return
  `{:select, select_info}`.
  """
  @spec send(tun_device(), iodata(), :nowait) :: :ok | {:select, :socket.select_info()}, {:error, any()}
  def send({:"$socket", _} = sock, data, :nowait) do
    :socket.send(sock, data, [], :nowait)
  end
  def send({:"$tundra", ref}, data, :nowait) do
    Tundra.Client.send(ref, data, [], :nowait)
  end

  @doc """
  Cancel a pending operation on a TUN device.
  """
  @spec cancel(tun_device(), :socket.select_info()) :: :ok | {:error, any()}
  def cancel({:"$socket", _} = sock, select_info), do: :socket.cancel(sock, select_info)
  def cancel({:"$tundra", ref}, select_info), do: Tundra.Client.cancel(ref, select_info)

  @doc """
  Close a TUN device.
  """
  @spec close(tun_device()) :: :ok | {:error, atom()}
  def close({:"$socket", _} = sock), do: :socket.close(sock)
  def close({:"$tundra", ref}), do: Tundra.Client.close(ref)

  defp convert_opts(opts) do
    Enum.reduce_while(opts, %{}, fn
      {key, val}, acc when key in [:addr, :dstaddr, :netmask] ->
        case convert_addr(val) do
          {:ok, addr} ->
            {:cont, Map.put(acc, key, addr)}

          error ->
            {:halt, error}
        end

      {:mtu, n}, acc when is_integer(n) and n >= 0 ->
        {:cont, Map.put(acc, :mtu, n)}

      {:mtu, _}, _ ->
        {:halt, {:error, :einval}}

      _, acc ->
        {:cont, acc}
    end)
  end

  defp convert_addr(str) when is_binary(str) do
    chars = to_charlist(str)

    with {:ok, _} <- :inet.parse_ipv6_address(chars) do
      {:ok, chars}
    end
  end

  defp convert_addr(addr) when is_tuple(addr) do
    case :inet.ntoa(addr) do
      {:error, _} = error ->
        error

      str ->
        {:ok, str}
    end
  end
end
