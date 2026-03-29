#!/bin/bash

set -euo pipefail

source "$(dirname "$0")/common.sh"

echo "Test: CPU load benchmark (informative)"

require_command awk
require_command date
require_command getconf

setup_test_dir

SAMPLE_DURATION_SEC=5
MAX_COMPRESSION_WAIT_SEC=12
CLK_TCK=$(getconf CLK_TCK)
CPU_COUNT=$(getconf _NPROCESSORS_ONLN)
REPORT_FILE="$TEST_ROOT/cpu_load_benchmark.txt"
ALL_METRICS_ARGS="-c 10 -r 10 -s 10 -d 10 -e 10 -f 10 -g 10 -a 10 -p 10 -m 10 -o 10"

write_config() {
    local config_path="$1"
    local output_dir="$2"
    local compression_enabled="$3"

    cat > "$config_path" <<EOF
output_dir=$output_dir
raw_log_enabled=true
raw_jsonl_enabled=true
raw_console_enabled=false
compression_enabled=$compression_enabled
metrics_on_console=true
metrics_on_json=true
max_file_size=1k
max_number_of_files=3
include_self=false
EOF
}

read_proc_cpu_ticks() {
    local pid="$1"
    local stat_line
    local stat_tail

    if [ ! -r "/proc/$pid/stat" ]; then
        return 1
    fi

    IFS= read -r stat_line < "/proc/$pid/stat" || return 1
    stat_tail="${stat_line#*) }"

    set -- $stat_tail

    echo $(( ${12} + ${13} ))
}

count_compressed_files() {
    local scenario_dir="$1"

    find "$scenario_dir" -maxdepth 1 -type f \( -name "ptime.log*.gz" -o -name "ptime.jsonl*.gz" \) | wc -l
}

run_benchmark_case() {
    local interval="$1"
    local compression_enabled="$2"
    local scenario_name="${interval}_${compression_enabled}"
    local scenario_dir="$TEST_ROOT/$scenario_name"
    local config_path="$scenario_dir/configuration.config"
    local metrics_file="$scenario_dir/metrics.json"
    local analyzer_pid
    local start_ticks
    local end_ticks
    local start_ns
    local end_ns
    local cpu_seconds
    local wall_seconds
    local cpu_percent
    local sample_duration_sec="$SAMPLE_DURATION_SEC"
    local compressed_count

    mkdir -p "$scenario_dir"
    write_config "$config_path" "$scenario_dir" "$compression_enabled"

    if [ "$interval" = "1s" ] && [ "$compression_enabled" = "true" ]; then
        sample_duration_sec="$MAX_COMPRESSION_WAIT_SEC"
    fi

    PROCESS_ANALYZER_CONFIG="$config_path" \
        "$BIN" \
        -i "$interval" \
        -n infinity \
        $ALL_METRICS_ARGS \
        > /dev/null 2>&1 &
    analyzer_pid=$!

    start_ticks=$(read_proc_cpu_ticks "$analyzer_pid") || {
        echo "Failed to read /proc/$analyzer_pid/stat at benchmark start"
        wait "$analyzer_pid" || true
        exit 1
    }
    start_ns=$(date +%s%N)

    sleep "$sample_duration_sec"

    if ! kill -0 "$analyzer_pid" 2>/dev/null; then
        echo "process_analyzer exited too early for scenario $scenario_name"
        wait "$analyzer_pid" || true
        exit 1
    fi

    end_ticks=$(read_proc_cpu_ticks "$analyzer_pid") || {
        echo "Failed to read /proc/$analyzer_pid/stat at benchmark end"
        kill -TERM "$analyzer_pid" 2>/dev/null || true
        wait "$analyzer_pid" || true
        exit 1
    }
    end_ns=$(date +%s%N)

    kill -TERM "$analyzer_pid" 2>/dev/null || true
    wait "$analyzer_pid"

    assert_file_exists "$metrics_file"

    cpu_seconds=$(awk -v start="$start_ticks" -v end="$end_ticks" -v hz="$CLK_TCK" \
        'BEGIN { printf "%.4f", (end - start) / hz }')
    wall_seconds=$(awk -v start="$start_ns" -v end="$end_ns" \
        'BEGIN { printf "%.4f", (end - start) / 1000000000.0 }')
    cpu_percent=$(awk -v start="$start_ticks" -v end="$end_ticks" -v hz="$CLK_TCK" -v start_ns="$start_ns" -v end_ns="$end_ns" -v cpus="$CPU_COUNT" \
        'BEGIN {
            wall = (end_ns - start_ns) / 1000000000.0;
            if (wall <= 0 || cpus <= 0) {
                print "0.00";
            } else {
                cpu = ((end - start) / hz) / wall / cpus * 100.0;
                printf "%.2f", cpu;
            }
        }')

    printf "%7s | %11s | wall=%6.2f s | cpu=%6.3f s | cpu=%6.2f%%\n" \
        "$interval" "$compression_enabled" "$wall_seconds" "$cpu_seconds" "$cpu_percent" \
        | tee -a "$REPORT_FILE"

    if [ "$compression_enabled" = "true" ]; then
        compressed_count=$(count_compressed_files "$scenario_dir")
        if [ "$compressed_count" -eq 0 ]; then
            echo "Note: no rotated .gz files were produced for $scenario_name" | tee -a "$REPORT_FILE"
        fi
    fi
}

{
    echo "CPU LOAD BENCHMARK"
    echo "measurement source: /proc/[pid]/stat"
    echo "sample duration: ${SAMPLE_DURATION_SEC}s"
    echo "1s+compression sample duration: ${MAX_COMPRESSION_WAIT_SEC}s"
    echo "interval | compression | timing | cpu"
} > "$REPORT_FILE"

cat "$REPORT_FILE"

run_benchmark_case "15ms" "false"
run_benchmark_case "15ms" "true"
run_benchmark_case "30ms" "false"
run_benchmark_case "30ms" "true"
run_benchmark_case "100ms" "false"
run_benchmark_case "100ms" "true"
run_benchmark_case "1s" "false"
run_benchmark_case "1s" "true"

echo
cat "$REPORT_FILE"
echo "Benchmark report saved to $REPORT_FILE"
echo "CPU load benchmark completed (informative only)"
