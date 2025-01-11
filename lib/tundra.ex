defmodule Tundra do
  @moduledoc """
  Documentation for `Tundra`.
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

  @spec close(reference()) :: :ok | {:error, atom()}
  def close(ref) when is_reference(ref) do
    Tundra.Client.close(ref)
  end

  @spec controlling_process(reference(), pid()) :: :ok | {:error, :not_owner}
  def controlling_process(ref, pid) when is_reference(ref) and is_pid(pid) do
    Tundra.Client.controlling_process(ref, pid)
  end

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
