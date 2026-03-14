#!/bin/bash
set -e

source "$(dirname "$0")/common.sh"

echo "Test: filter by process name"

command -v jq >/dev/null || {
    echo "jq not found, skipping test"
    exit 0
}

setup_test_dir

sleep 8 &
SLEEP_PID=$!

cleanup() {
    kill "$SLEEP_PID" 2>/dev/null || true
    wait "$SLEEP_PID" 2>/dev/null || true
}
trap cleanup EXIT

$BIN \
    -i 100ms \
    -n 15 \
    -l sleep \
    -c 20 \
    > "$TEST_ROOT/out.txt" 2>&1

JSONL="$TEST_ROOT/ptime.jsonl"
METRICS="$TEST_ROOT/metrics.json"

assert_file_exists "$JSONL"
assert_file_exists "$METRICS"

jq -s -e '
  length > 0 and
  all(.[]; .comm == "sleep")
' "$JSONL" >/dev/null || {
    echo "JSONL contains processes outside --filter_by_name list"
    head -n 20 "$JSONL" || true
    exit 1
}

jq -e '
  [ .metrics.cpu_average[] | select(.comm == "sleep") ] | length > 0
' "$METRICS" >/dev/null || {
    echo "No sleep process found in cpu_average metric"
    jq '.metrics.cpu_average[:10]' "$METRICS" || true
    exit 1
}

echo "Filter by process name OK"
