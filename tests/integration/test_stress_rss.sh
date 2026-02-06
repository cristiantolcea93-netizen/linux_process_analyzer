#!/bin/bash
set -e

echo "Test: RSS stress test"

command -v stress-ng >/dev/null || exit 0

BIN=./build/bin/process_analyzer

stress-ng --vm 2 --vm-bytes 500M --timeout 5s &
PID=$!

$BIN -i 100ms -n 50 -d 5 > /tmp/ptime/out.txt 2>&1

kill $PID 2>/dev/null || true

jq -e '
  [ .metrics.rss_delta[]
    | select(.comm | test("^stress-ng"))
    | select(.rss_delta_kb != 0)
  ] | length > 0
' /tmp/ptime/metrics.json >/dev/null || {
	echo "No RSS variation detected for stress-ng"
	exit 1
}
echo "Disk IO OK"
