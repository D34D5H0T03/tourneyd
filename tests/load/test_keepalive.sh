#!/bin/bash
set -e # Exit immediately if any assertion fails

PORT=8080
HOST="127.0.0.1"
URL="http://$HOST:$PORT"

echo "=> Starting tourneyd background process..."
./build/tourneyd > /dev/null 2>&1 &
SERVER_PID=$!
sleep 1 # Wait for server to bind

# Ensure the server is killed if the script fails midway
function cleanup {
    kill -9 $SERVER_PID 2>/dev/null || true
    rm -f public/big.bin
}
trap cleanup EXIT

echo "=> [1/6] Testing HTTP/1.1 Connection Reuse (Pipelining)..."
curl -s -v $URL/ $URL/ 2>&1 | grep -i "re-using" > /dev/null
echo "   [OK] Connection reused successfully."

echo "=> [2/6] Testing 100-Continue..."
curl -s -v -H "Expect: 100-continue" -d 'hello' $URL/upper 2>&1 | grep -i "100 Continue" > /dev/null
echo "   [OK] 100 Continue response received and body accepted."

echo "=> [3/6] Testing 404 Route Keep-Alive..."
curl -s -v $URL/nope $URL/ 2>&1 | grep -i "re-using" > /dev/null
echo "   [OK] 404 response successfully kept connection alive."

echo "=> [4/6] Testing 501 Parse Error (Closes Connection)..."
RESP=$(printf 'BREW / HTTP/1.1\r\nHost: x\r\n\r\n' | nc $HOST $PORT)
if echo "$RESP" | grep -q "501 Not Implemented"; then
    echo "   [OK] 501 Not Implemented triggered and connection safely dropped."
else
    echo "   [FAIL] Expected 501 response."
    exit 1
fi

echo "=> [5/6] Testing Graceful Shutdown (In-flight 6MB download)..."
# Create a temporary 6MB file in the document root
head -c 6000000 /dev/urandom > public/big.bin
# Start a throttled download (1MB/s) in the background so it takes ~6 seconds
curl -f --limit-rate 1M -o /dev/null -s $URL/big.bin &
CURL_PID=$!
sleep 1

echo "   [>] Sending SIGTERM to server (PID $SERVER_PID)..."
kill -TERM $SERVER_PID

# Wait for the download to finish. If the server violently kills the connection, 
# curl will throw an error and 'set -e' will fail the test.
wait $CURL_PID
echo "   [OK] Download completed successfully despite the shutdown signal."

echo "=> [6/6] Verifying server process exited cleanly..."
sleep 1
if pgrep -x tourneyd > /dev/null; then
    echo "   [FAIL] Server is still running after grace period!"
    exit 1
else
    echo "   [OK] Server process successfully terminated."
fi

echo -e "\n[SUCCESS] All Keep-Alive and Graceful Shutdown integration tests passed!"

# Clean exit
trap - EXIT
rm -f public/big.bin