defmodule TundraTest do
  use ExUnit.Case
  doctest Tundra

  test "greets the world" do
    assert Tundra.hello() == :world
  end
end
