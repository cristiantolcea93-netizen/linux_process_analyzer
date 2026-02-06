#!/bin/bash
set -e

echo "Test: stress CPU"

command -v stress-ng >/dev/null || {
    echo "stress-ng not installed, skipping"
    exit 0
}

BIN=./build/bin/process_analyzer

stress-ng --cpu 4 --timeout 5s &
STRESS_PID=$!

$BIN -i 100ms -n 50 -c 5 > /tmp/ptime/out.txt 2>&1

kill $STRESS_PID 2>/dev/null || true

jq -e '
  [ .metrics.cpu_average[]
    | select(.comm | test("^stress-ng"))
    | select(.cpu_avg_pct != 0)
  ] | length > 0
' /tmp/ptime/metrics.json >/dev/null || {
	echo "No high CPU detected for stress-ng"
	exit 1
}

echo "Stress CPU OK"
