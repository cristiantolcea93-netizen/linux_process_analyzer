#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
TEST_ROOT="/tmp/ptime"

BIN="$REPO_ROOT/build/bin/process_analyzer"

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

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Required command not found: $1"
        exit 1
    fi
}
