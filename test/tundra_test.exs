defmodule TundraTest do
  use ExUnit.Case, async: true

  describe "adopt/1" do
    test "rejects negative file descriptors" do
      assert_raise FunctionClauseError, fn -> Tundra.adopt(-1) end
    end

    test "rejects non-integer file descriptors" do
      assert_raise FunctionClauseError, fn -> Tundra.adopt(:not_an_fd) end
    end

    test "returns an error for an invalid file descriptor" do
      assert {:error, :ebadf} = Tundra.adopt(9_999)
    end

    test "returns an error for a descriptor that is not a TUN device" do
      # stderr is a valid descriptor but not a TUN/utun device. adopt/1 only
      # inspects the descriptor before failing and leaves it open.
      assert {:error, _reason} = Tundra.adopt(2)
    end
  end
end
