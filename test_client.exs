#!/usr/bin/env elixir

# Test client for Tundra TUN device creation
#
# This script demonstrates creating a TUN device using the Tundra library.
# Assumes tundra_server is already running as root at /var/run/tundra.sock
#
# Usage:
#   ./test_client.exs

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

  def run do
    Logger.info("Starting Tundra test client...")

    # Start the Tundra application
    {:ok, _} = Application.ensure_all_started(:tundra)

    # Create a TUN device
    Logger.info("Creating TUN device with address #{@addr}")
    Logger.info("  Destination: #{@dstaddr}")
    Logger.info("  Netmask: #{@netmask}")
    Logger.info("  MTU: #{@mtu}")

    case Tundra.create(@addr, dstaddr: @dstaddr, netmask: @netmask, mtu: @mtu) do
      {:ok, {dev, name}} ->
        Logger.info("Successfully created TUN device: #{name}")
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

      {:error, reason} ->
        Logger.error("Failed to create TUN device: #{inspect(reason)}")
        System.halt(1)
    end
  end
end

# Run the test
TundraTestClient.run()
