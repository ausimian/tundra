defmodule Tundra.Client do
  @behaviour :gen_statem
  use TypedStruct

  @on_load :load_nif

  def child_spec(args) do
    %{
      id: __MODULE__,
      start: {__MODULE__, :start_link, [args]},
      restart: :temporary,
      type: :worker
    }
  end

  def create_tun_device(pid) do
    :gen_statem.call(pid, :create_tun_device)
  end

  def start_link(opts) do
    :gen_statem.start_link(__MODULE__, opts, [])
  end

  typedstruct enforce: true do
    field(:conn, reference())
    field(:caller, GenServer.from() | nil, default: nil)
    field(:blocked, reference() | nil, default: nil)
  end

  @impl true
  def callback_mode, do: :handle_event_function

  @impl true
  def init(_opts) do
    with {:ok, conn} <- connect() do
      {:ok, :connected, %__MODULE__{conn: conn}}
    end
  end

  @impl true
  def handle_event({:call, from}, :create_tun_device, :connected, %__MODULE__{caller: nil} = data) do
    {:next_state, :sending, %__MODULE__{data | caller: from},
     {:next_event, :internal, :send_request}}
  end

  def handle_event(:internal, :send_request, :sending, %__MODULE__{blocked: nil} = data) do
    ref = make_ref()

    case send_request(data.conn, ref) do
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

  @nifs connect: 0, send_request: 2, recv_response: 2, controlling_process: 2
  defp connect, do: :erlang.nif_error(:not_implemented)
  defp send_request(_conn, _ref), do: :erlang.nif_error(:not_implemented)
  defp recv_response(_conn, _ref), do: :erlang.nif_error(:not_implemented)
  defp controlling_process(_ref, _pid), do: :erlang.nif_error(:not_implemented)
end
