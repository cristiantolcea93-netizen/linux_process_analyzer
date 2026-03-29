#!/bin/bash
set -e

source "$(dirname "$0")/common.sh"

echo "Test: FD metrics"

command -v jq >/dev/null || exit 0

setup_test_dir

FD_SOURCE_FILE="$TEST_ROOT/fd_source.txt"
METRICS_FILE="$TEST_ROOT/metrics.json"

printf 'fd-test\n' > "$FD_SOURCE_FILE"

bash -c '
file="$1"

sleep 1
exec 10<"$file"
sleep 0.2
exec 11<"$file"
sleep 0.2
exec 12<"$file"
sleep 0.2
exec 13<"$file"
sleep 0.2
exec 14<"$file"
sleep 0.2
exec 15<"$file"
sleep 0.2
exec 16<"$file"
sleep 0.2
exec 17<"$file"
sleep 3
' _ "$FD_SOURCE_FILE" &
TARGET_PID=$!

cleanup() {
    kill "$TARGET_PID" 2>/dev/null || true
    wait "$TARGET_PID" 2>/dev/null || true
}

trap cleanup EXIT

$BIN \
    -i 100ms \
    -n 50 \
    -k "$TARGET_PID" \
    -p 5 \
    -m 5 \
    -o 5 \
    > "$TEST_ROOT/out.txt" 2>&1

assert_file_exists "$METRICS_FILE"

jq -e --argjson pid "$TARGET_PID" '
  any(.metrics.opened_fds[]?;
      .pid == $pid and .no_of_opened_fds >= 10)
' "$METRICS_FILE" >/dev/null || {
    echo "Missing or invalid opened_fds metric for PID $TARGET_PID"
    jq '.metrics.opened_fds' "$METRICS_FILE" || true
    exit 1
}

jq -e --argjson pid "$TARGET_PID" '
  any(.metrics.fds_increase[]?;
      .pid == $pid and .no_of_fds_increase >= 4)
' "$METRICS_FILE" >/dev/null || {
    echo "Missing or invalid fds_increase metric for PID $TARGET_PID"
    jq '.metrics.fds_increase' "$METRICS_FILE" || true
    exit 1
}

jq -e --argjson pid "$TARGET_PID" '
  any(.metrics.fds_delta[]?;
      .pid == $pid and .fds_delta >= 4)
' "$METRICS_FILE" >/dev/null || {
    echo "Missing or invalid fds_delta metric for PID $TARGET_PID"
    jq '.metrics.fds_delta' "$METRICS_FILE" || true
    exit 1
}

echo "FD metrics OK"
