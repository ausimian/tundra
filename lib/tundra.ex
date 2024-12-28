defmodule Tundra do
  @moduledoc """
  Documentation for `Tundra`.
  """

  def open do
    alias Tundra.DynamicSupervisor, as: Sup

    with {:ok, pid} <- DynamicSupervisor.start_child(Sup, Tundra.Client) do
      Tundra.Client.create_tun_device(pid)
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
end
