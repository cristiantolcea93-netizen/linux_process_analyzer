#!/bin/bash

set -e

source "$(dirname "$0")/common.sh"

echo "Test: log compression"

setup_test_dir

cat > "$TEST_ROOT/configuration.config" <<EOF
#output directory (where all the output files are generated)
#default /tmp/ptime
output_dir=/tmp/ptime

#output for raw data 
#default true
raw_log_enabled=true
#default true
raw_jsonl_enabled=true
#default false
raw_console_enabled=false

#compression for raw data
#default false
compression_enabled=true

#output for metrics
#default true
metrics_on_console=true
#default true
metrics_on_json=true

#.log and .jsonl file rotation settings 
#default 5m
max_file_size=1k
#default 3
max_number_of_files=2

#general settings
#default false
include_self=false
EOF

export PROCESS_ANALYZER_CONFIG="$TEST_ROOT/configuration.config"

$BIN \
    -i 10ms \
    -n 200 \
    -j \
    > /dev/null

unset PROCESS_ANALYZER_CONFIG


COUNT=$(ls "$TEST_ROOT"/ptime.log*.gz 2>/dev/null | wc -l)

if [ "$COUNT" -gt 3 ]; then
    echo "Too many rotated .gz log files: $COUNT"
    exit 1
elif [ "$COUNT" -lt 1 ]; then
    echo "Not enough rotated .gz log files: $COUNT"
    exit 1
fi

COUNT_JSONL=$(ls "$TEST_ROOT"/ptime.jsonl*.gz 2>/dev/null | wc -l)

if [ "$COUNT_JSONL" -gt 3 ]; then
    echo "Too many rotated .gz jsonl files: $COUNT_JSONL"
    exit 1
elif [ "$COUNT_JSONL" -lt 1 ]; then
    echo "Not enough rotated .gz jsonl files: $COUNT_JSONL"
    exit 1
fi


echo "Compression OK"
