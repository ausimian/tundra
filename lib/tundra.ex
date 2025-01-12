defmodule Tundra do
  @moduledoc """
  Documentation for `Tundra`.
  """

  @type tun_device() :: :socket.socket() | {:"$tundra", reference()}

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

  @spec controlling_process(tun_device(), pid()) :: :ok | {:error, any()}
  def controlling_process({:"$socket", _} = sock, pid) when is_pid(pid) do
    :socket.setopt(sock, {:otp, :controlling_process}, pid)
  end
  def controlling_process({:"$tundra", ref}, pid) when is_pid(pid) do
    Tundra.Client.controlling_process(ref, pid)
  end

  @spec recv(tun_device(), non_neg_integer(), :nowait) :: {:ok, binary()} | {:error, any()}
  def recv({:"$socket", _} = sock, length, :nowait) when is_integer(length) do
    :socket.recv(sock, length, [], :nowait)
  end
  def recv({:"$tundra", ref}, length, :nowait) when is_integer(length) do
    Tundra.Client.recv(ref, length, [], :nowait)
  end

  @spec send(tun_device(), iodata(), :nowait) :: :ok | {:error, any()}
  def send({:"$socket", _} = sock, data, :nowait) do
    :socket.send(sock, data, [], :nowait)
  end
  def send({:"$tundra", ref}, data, :nowait) do
    Tundra.Client.send(ref, data, [], :nowait)
  end

  @spec cancel(tun_device(), :socket.select_info()) :: :ok | {:error, any()}
  def cancel({:"$socket", _} = sock, select_info), do: :socket.cancel(sock, select_info)
  def cancel({:"$tundra", ref}, select_info), do: Tundra.Client.cancel(ref, select_info)

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
