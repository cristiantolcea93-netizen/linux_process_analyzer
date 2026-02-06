#!/bin/bash
set -e

echo "Test: double instance lock"

BIN=./build/bin/process_analyzer

$BIN -i 100ms -n infinity > /tmp/ptime/out1.txt 2>&1 &
PID1=$!

sleep 1

# Second instance
if $BIN -i 100ms -n 5 > /tmp/ptime/out2.txt 2>&1; then
    echo "Second instance should not start"
    kill $PID1
    exit 1
fi

grep -q "already running" /tmp/ptime/out2.txt || {
    echo "Missing lock error"
    kill $PID1
    exit 1
}

kill $PID1
wait $PID1 2>/dev/null || true

echo "Double instance OK"
