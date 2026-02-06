#!/bin/bash

set -e

source "$(dirname "$0")/common.sh"

echo "Test: basic run"

setup_test_dir

echo $BIN

$BIN \
    -i 100ms \
    -n 5 \
    -j \
    -c 10 \
    > "$TEST_ROOT/out.txt" 2>&1

assert_file_exists "$TEST_ROOT/ptime.log"
assert_file_exists "$TEST_ROOT/ptime.jsonl"
assert_file_exists "$TEST_ROOT/metrics.json"

echo "Basic run OK"
