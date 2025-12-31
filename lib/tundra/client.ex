defmodule Tundra.Client do
  @moduledoc false
  @behaviour :gen_statem
  use TypedStruct

  @on_load :load_nif
  if Version.match?(System.version(), ">= 1.16.0") do
    @nifs connect: 0,
          send_request: 3,
          recv_response: 2,
          controlling_process: 2,
          close: 1,
          get_fd: 1,
          recv_data: 2,
          send_data: 2,
          cancel_select: 2,
          create_tun_direct: 1
  end

  def child_spec(args) do
    %{
      id: __MODULE__,
      start: {__MODULE__, :start_link, [args]},
      restart: :temporary,
      type: :worker
    }
  end

  def create_tun_device(pid, params) when is_pid(pid) and is_map(params) do
    # Try direct creation first (requires privileges)
    case create_tun_direct(params) do
      {:ok, {ref, name}} ->
        # Direct creation succeeded - both Linux and Darwin use $tundra refs
        {:ok, {{:"$tundra", ref}, name}}

      {:error, :eperm} ->
        # No privileges, fall back to server
        create_via_server(pid, params)

      {:error, :eacces} ->
        # No access, fall back to server
        create_via_server(pid, params)

      {:error, :enotsup} ->
        # Platform not supported for direct creation, try server
        create_via_server(pid, params)

      {:error, _other} = error ->
        # Other error, return it
        error
    end
  end

  defp create_via_server(pid, params) do
    with {:ok, {ref, name}} <- :gen_statem.call(pid, {:create_tun_device, params}) do
      case :os.type() do
        {:unix, :darwin} ->
          {:ok, s} = :socket.open(get_fd(ref), %{domain: 32, type: 2, protocol: 2})
          close(ref)
          {:ok, {s, name}}

        {:unix, :linux} ->
          {:ok, {{:"$tundra", ref}, name}}

        _ ->
          {:error, :not_supported}
      end
    end
  end

  @spec recv(reference(), non_neg_integer(), list(), :nowait) ::
          {:ok, binary()} | {:error, any()} | {:select, :socket.select_info()}
  def recv(ref, length, _flags, :nowait) do
    recv_data(ref, length)
  end

  @spec send(reference(), iodata(), list(), :nowait) ::
          :ok | {:ok, binary()} | {:select, :socket.select_info()} | {:error, any()}
  def send(ref, data, _flags, :nowait) do
    send_data(ref, :erlang.iolist_to_iovec(data))
  end

  @spec cancel(reference(), :socket.select_info()) :: :ok | {:error, any()}
  def cancel(ref, select_info) do
    cancel_select(ref, select_info)
  end

  @spec start_link(any()) :: :ignore | {:error, any()} | {:ok, pid()}
  def start_link(opts) do
    :gen_statem.start_link(__MODULE__, opts, [])
  end

  typedstruct do
    field(:conn, reference() | nil)
    field(:caller, GenServer.from() | nil, default: nil)
    field(:blocked, reference() | nil, default: nil)
  end

  @impl true
  def callback_mode, do: :handle_event_function

  @impl true
  def init(_opts) do
    # Try to connect to server, but allow it to fail since we might use direct creation
    case connect() do
      {:ok, conn} ->
        {:ok, :connected, %__MODULE__{conn: conn}}

      {:error, reason} ->
        # Server not available, we'll try direct creation
        {:ok, {:disconnected, reason}, %__MODULE__{conn: nil}}
    end
  end

  @impl true
  def handle_event(
        {:call, from},
        {:create_tun_device, _params},
        {:disconnected, reason},
        data
      ) do
    # Server not available, return the connection error
    {:keep_state, data, {:reply, from, {:error, reason}}}
  end

  def handle_event(
        {:call, from},
        {:create_tun_device, params},
        :connected,
        %__MODULE__{caller: nil} = data
      ) do
    event = {:next_event, :internal, {:send_request, params}}
    {:next_state, :sending, %__MODULE__{data | caller: from}, event}
  end

  def handle_event(:internal, {:send_request, params}, :sending, %__MODULE__{blocked: nil} = data) do
    ref = make_ref()

    case send_request(data.conn, ref, params) do
      :ok ->
        {:next_state, :receiving, data, {:next_event, :internal, :recv_response}}

      {:error, :eagain} ->
        {:keep_state, %__MODULE__{data | blocked: ref}}

      {:error, reason} = error ->
        {:stop_and_reply, reason, %__MODULE__{data | caller: nil}, {:reply, data.caller, error}}
    end
  end

  def handle_event(
        :info,
        {:select, conn, ref, :ready_output},
        :sending,
        %__MODULE__{conn: conn, blocked: ref} = data
      ) do
    {:keep_state, %__MODULE__{data | blocked: nil}, {:next_event, :internal, :send_request}}
  end

  def handle_event(:internal, :recv_response, :receiving, %__MODULE__{blocked: nil} = data) do
    ref = make_ref()

    case recv_response(data.conn, ref) do
      {:ok, {ref, _}} = reply ->
        {pid, _} = data.caller
        :ok = controlling_process(ref, pid)
        {:stop_and_reply, :normal, {:reply, data.caller, reply}, %__MODULE__{data | caller: nil}}

      {:error, :eagain} ->
        {:keep_state, %__MODULE__{data | blocked: ref}}

      {:error, reason} = error ->
        {:stop_and_reply, reason, {:reply, data.caller, error}, %__MODULE__{data | caller: nil}}
    end
  end

  def handle_event(
        :info,
        {:select, conn, ref, :ready_input},
        :receiving,
        %__MODULE__{conn: conn, blocked: ref} = data
      ) do
    {:keep_state, %__MODULE__{data | blocked: nil}, {:next_event, :internal, :recv_response}}
  end

  defp load_nif do
    path = Path.join(:code.priv_dir(:tundra), "tundra_nif")
    :erlang.load_nif(to_charlist(path), 0)
  end

  defp connect, do: :erlang.nif_error(:not_implemented)
  defp send_request(_conn, _ref, _params), do: :erlang.nif_error(:not_implemented)
  defp recv_response(_conn, _ref), do: :erlang.nif_error(:not_implemented)
  defp get_fd(_conn), do: :erlang.nif_error(:not_implemented)

  defp recv_data(_ref, _length), do: :erlang.nif_error(:not_implemented)
  defp send_data(_ref, _data), do: :erlang.nif_error(:not_implemented)
  defp cancel_select(_ref, _select_info), do: :erlang.nif_error(:not_implemented)
  defp create_tun_direct(_params), do: :erlang.nif_error(:not_implemented)

  def close(_ref), do: :erlang.nif_error(:not_implemented)
  def controlling_process(_ref, _pid), do: :erlang.nif_error(:not_implemented)
end
