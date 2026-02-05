#!/bin/bash

TEST_ROOT="/tmp/ptime"

BIN="./build/bin/process_analyzer"

setup_test_dir() {
    rm -rf "$TEST_ROOT"
    mkdir -p "$TEST_ROOT"
}

assert_file_exists() {
    if [ ! -f "$1" ]; then
        echo "Expected file not found: $1"
        exit 1
    fi
}

assert_contains() {
    local file="$1"
    local pattern="$2"

    if ! grep -q "$pattern" "$file"; then
        echo "Pattern '$pattern' not found in $file"
        exit 1
    fi
}
