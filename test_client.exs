#!/usr/bin/env elixir

# Test client for Tundra TUN device creation
#
# This script demonstrates creating a TUN device using the Tundra library.
# Assumes tundra_server is already running as root at /var/run/tundra.sock
#
# Usage:
#   ./test_client.exs              # Create a new TUN device
#   ./test_client.exs <name>       # Open an existing TUN device (e.g., tun0)

Mix.install([{:tundra, path: "."}])

defmodule TundraTestClient do
  @moduledoc """
  Simple test client that creates a TUN device and performs basic I/O.
  """

  require Logger

  @mtu 1500
  # IPv6 addresses for the TUN device
  @addr "fd11:b7b7:4360::2"
  @dstaddr "fd11:b7b7:4360::1"
  @netmask "ffff:ffff:ffff:ffff::"

  def run(args \\ []) do
    Logger.info("Starting Tundra test client...")

    # Start the Tundra application
    {:ok, _} = Application.ensure_all_started(:tundra)

    # Either open an existing device or create a new one
    result =
      case args do
        [name | _] ->
          Logger.info("Opening existing TUN device: #{name}")
          case Tundra.open(name) do
            {:ok, dev} -> {:ok, {dev, name}}
            {:error, reason} -> {:error, reason}
          end

        [] ->
          Logger.info("Creating TUN device with address #{@addr}")
          Logger.info("  Destination: #{@dstaddr}")
          Logger.info("  Netmask: #{@netmask}")
          Logger.info("  MTU: #{@mtu}")
          Tundra.create(@addr, dstaddr: @dstaddr, netmask: @netmask, mtu: @mtu)
      end

    case result do
      {:ok, {dev, name}} ->
        Logger.info("Successfully opened TUN device: #{name}")
        Logger.info("Device handle: #{inspect(dev)}")

        # Try to read from the device (non-blocking)
        Logger.info("Attempting to read from device (should return {:select, ...} if no data)...")

        case Tundra.recv(dev, @mtu + 4, :nowait) do
          {:ok, data} ->
            Logger.info("Received #{byte_size(data)} bytes: #{inspect(data, limit: 50)}")

          {:select, select_info} ->
            Logger.info("No data available (would block), select_info: #{inspect(select_info)}")

          {:error, reason} ->
            Logger.error("Error reading from device: #{inspect(reason)}")
        end

        # Keep the device open for a bit
        Logger.info("Keeping device open for 5 seconds...")
        Logger.info("You can inspect the device with: ip link show #{name}")
        Logger.info("Or: ip addr show #{name}")

        Process.sleep(5000)

        # Close the device
        Logger.info("Closing device...")

        case Tundra.close(dev) do
          :ok ->
            Logger.info("Device closed successfully")

          {:error, reason} ->
            Logger.error("Error closing device: #{inspect(reason)}")
        end

      {:error, reason} when reason in [:eacces, :eperm] ->
        Logger.error("Failed to create TUN device: #{inspect(reason)}")
        Logger.error("Permission denied. Check that you are a member of the 'tundra' group.")
        Logger.error("  Linux: sudo usermod -aG tundra $USER")
        Logger.error("  macOS: sudo dseditgroup -o edit -a $USER -t user tundra")
        Logger.error("Log out and back in after adding yourself to the group.")
        System.stop(1)

      {:error, reason} ->
        Logger.error("Failed to create TUN device: #{inspect(reason)}")
        System.stop(1)
    end
  end
end

# Run the test with command-line arguments
TundraTestClient.run(System.argv())
