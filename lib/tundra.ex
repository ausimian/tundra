defmodule Tundra do
  @moduledoc """
  Documentation for `Tundra`.
  """

  def open do
    with {:ok, pid} <-
           DynamicSupervisor.start_child(Tundra.DynamicSupervisor, Tundra.Client.child_spec([])) do
      Tundra.Client.create_tun_device(pid)
    end
  end
end
