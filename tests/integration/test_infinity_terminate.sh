#!/bin/bash

# Check metrics arrays
check_array() {
    jq -e ".metrics.$1 | length > 0" "$METRICS" >/dev/null || {
        echo "Missing $1"
        exit 1
    }
}


set -e

source "$(dirname "$0")/common.sh"

echo "Test: infinity + terminate"

setup_test_dir

# Check jq exists
if ! command -v jq >/dev/null 2>&1; then
    echo "jq not found, skipping test"
    exit 0
fi

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
# Run analyzer in background (infinity mode)
$BIN \
    -i 50ms \
    -n infinity \
    -c 10 \
    -r 10 \
    -s 10 \
    -d 10 \
    -f 10 \
    -g 10 \
    -a 10 \
    -j \
    > "$TEST_ROOT/out.txt" 2>&1 &

PID=$!

echo "Started analyzer with PID $PID"

# Let it run a bit
sleep 2


# Send SIGTERM
kill -TERM "$PID"

# Wait for graceful exit
wait "$PID" || true

unset PROCESS_ANALYZER_CONFIG


# ----------------------------
# Validate output files
# ----------------------------

LOG="$TEST_ROOT/ptime.log"
JSONL="$TEST_ROOT/ptime.jsonl"
METRICS="$TEST_ROOT/metrics.json"


assert_file_exists "$LOG"
assert_file_exists "$JSONL"
assert_file_exists "$METRICS"


# ----------------------------
# Validate jsonl format
# ----------------------------

echo "Validating JSONL..."

LINE_COUNT=$(wc -l < "$JSONL")

if [ "$LINE_COUNT" -eq 0 ]; then
    echo "JSONL is empty"
    exit 1
fi


# Every line must be valid JSON
while read -r line; do
    echo "$line" | jq . >/dev/null || {
        echo "Invalid JSONL line:"
        echo "$line"
        exit 1
    }
done < "$JSONL"


# ----------------------------
# Validate metrics.json
# ----------------------------

echo "Validating metrics.json..."

jq . "$METRICS" >/dev/null || {
    echo "Invalid metrics.json"
    exit 1
}


# Check root
jq -e '.meta and .metrics' "$METRICS" >/dev/null || exit 1

# Meta sanity
jq -e '
  .meta.interval_ms > 0 and
  .meta.snapshots > 0
' "$METRICS" >/dev/null || exit 1



check_array cpu_average
check_array rss_average
check_array rss_increase
check_array rss_delta
check_array written_kbytes
check_array read_rate
check_array write_rate

# Check cpu values sane
jq -e '
  .metrics.cpu_average[]
  | select(.cpu_avg_pct >= 0 and .records > 0)
' "$METRICS" >/dev/null || exit 1

echo "Metrics validation OK"



# ----------------------------
# Validate content
# ----------------------------

echo "Validating content..."

LOG_LINES=$(wc -l < "$LOG")

if [ "$LOG_LINES" -eq 0 ]; then
    echo "Log file empty"
    exit 1
fi


echo "Infinity terminate test OK"
