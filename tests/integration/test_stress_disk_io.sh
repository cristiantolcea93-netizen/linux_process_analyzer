#!/bin/bash
set -e

echo "Test: disk IO (read + write + rates)"

command -v stress-ng >/dev/null || exit 0
command -v jq >/dev/null || exit 0

BIN=./build/bin/process_analyzer
OUT=/tmp/ptime

rm -rf "$OUT"
mkdir -p "$OUT"

# Run IO stress: read + write
stress-ng \
  --iomix 2 \
  --hdd 1 \
  --iomix-bytes 300M \
  --hdd-bytes 200M \
  --timeout 6s &

STRESS_PID=$!

# let stress-ng run for 1 second before starting the analyzer
sleep 1

# Run analyzer
$BIN \
  -i 100ms \
  -n 50 \
  -e 5 \
  -f 5 \
  -g 5 \
  -a 5 \
  > "$OUT/out.txt" 2>&1

kill $STRESS_PID 2>/dev/null || true
wait $STRESS_PID 2>/dev/null || true

METRICS="$OUT/metrics.json"

[ -f "$METRICS" ] || {
  echo "Missing metrics.json"
  exit 1
}

echo "Validating write bytes..."

jq -e '
  [ .metrics.written_kbytes[]
    | select(.comm | test("^stress-ng"))
    | select(.written_bytes_kb > 0)
  ] | length > 0
' "$METRICS" >/dev/null || {
  echo "No write activity detected"
  exit 1
}

echo "Validating read bytes..."

jq -e '
  [ .metrics.kbytes_read[]
    | select(.comm | test("^stress-ng"))
    | select(.bytes_read_kb > 0)
  ] | length > 0
' "$METRICS" >/dev/null || {
  echo "No read activity detected"
  exit 1
}


echo "Validating read rate..."

jq -e '
  [ .metrics.read_rate[]
    | select(.comm | test("^stress-ng"))
    | select(.read_rate_kbps > 0)
  ] | length > 0
' "$METRICS" >/dev/null || {
  echo "No read rate detected"
  exit 1
}

echo "Validating write rate..."

jq -e '
  [ .metrics.write_rate[]
    | select(.comm | test("^stress-ng"))
    | select(.write_rate_kbps > 0)
  ] | length > 0
' "$METRICS" >/dev/null || {
  echo "No write rate detected"
  exit 1
}

echo "Disk IO metrics OK"
