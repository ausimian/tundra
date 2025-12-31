#!/bin/bash
#
# test.sh - Test the tundra server
#
# This script will:
# 1. Start the server in the background
# 2. Run the test client
# 3. Clean up the server

set -e

echo "=== Tundra Server Test ==="
echo

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run with sudo"
    echo "Usage: sudo ./test.sh"
    exit 1
fi

# Kill any existing server
pkill -f tundra_server || true
sleep 1

# Remove old socket
rm -f /var/run/tundra.sock

# Start server in background
echo "Starting tundra_server..."
./tundra_server &
SERVER_PID=$!
sleep 1

# Check if server started
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "Error: Server failed to start"
    exit 1
fi

echo "Server started (PID: $SERVER_PID)"
echo

# Run test client
echo "Running test client..."
echo "Note: The test will keep the device alive until you press Enter"
echo
./test_client

# Cleanup
echo
echo "Stopping server..."
kill $SERVER_PID
wait $SERVER_PID 2>/dev/null || true

echo "Test complete!"
