#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Running integration tests..."

FAILED=0

for test in "$SCRIPT_DIR"/test_*.sh; do
    echo "-----------------------------"
    echo "Running: $(basename "$test")"
    echo "-----------------------------"

    if ! bash "$test"; then
        echo "❌ FAILED: $test"
        FAILED=1
    else
        echo "✅ PASSED: $test"
    fi
done

if [ $FAILED -ne 0 ]; then
    echo "Some integration tests FAILED"
    exit 1
fi

echo "All integration tests PASSED"
