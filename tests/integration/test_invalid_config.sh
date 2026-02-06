#!/bin/bash

set -e

source "$(dirname "$0")/common.sh"

echo "Test: invalid config"

setup_test_dir

cat > "$TEST_ROOT/configuration.config" <<EOF
invalid_option=123
EOF


export PROCESS_ANALYZER_CONFIG="$TEST_ROOT/configuration.config"

if $BIN -i 100ms -n 1 2>/dev/null; then
	unset PROCESS_ANALYZER_CONFIG
    echo "Expected failure"
    exit 1
fi

unset PROCESS_ANALYZER_CONFIG

echo "Invalid config OK"
