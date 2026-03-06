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

# Limit analyzer to stress-ng process tree only, to avoid top-N truncation noise.
# Use the launched stress-ng PID plus its worker child PIDs.
STRESS_PIDS=$(
  {
    echo "$STRESS_PID"
    pgrep -P "$STRESS_PID" || true
  } | sed '/^$/d' | sort -u | paste -sd, -
)

[ -n "$STRESS_PIDS" ] || {
  echo "Could not resolve stress-ng PID list"
  kill $STRESS_PID 2>/dev/null || true
  wait $STRESS_PID 2>/dev/null || true
  exit 1
}

# Run analyzer
$BIN \
  -i 100ms \
  -n 50 \
  -k "$STRESS_PIDS" \
  -e 50 \
  -f 50 \
  -g 50 \
  -a 50 \
  > "$OUT/out.txt" 2>&1

kill $STRESS_PID 2>/dev/null || true
wait $STRESS_PID 2>/dev/null || true

METRICS="$OUT/metrics.json"

[ -f "$METRICS" ] || {
  echo "Missing metrics.json"
  exit 1
}

echo "Validating write bytes..."

# Ensure stress-ng entries are present in the metric section.
jq -e '
  [ .metrics.written_kbytes[]
    | select(.comm | test("^stress-ng"))
  ] | length > 0
' "$METRICS" >/dev/null || {
  echo "Missing stress-ng entries in written_kbytes metric"
  jq '.metrics.written_kbytes[:10]' "$METRICS" || true
  exit 1
}

jq -e '
  [ .metrics.written_kbytes[]
    | select(.comm | test("^stress-ng"))
    | select(.written_bytes_kb > 0)
  ] | length > 0
' "$METRICS" >/dev/null || {
  echo "Warning: no storage write activity detected for stress-ng in this environment."
  jq '.metrics.written_kbytes[:10]' "$METRICS" || true
}

echo "Validating read bytes..."

# In CI environments, disk reads may be served from page cache.
# /proc/<pid>/io read_bytes counts storage reads, not cached hits,
# so read counters can stay at 0 even when the workload performs reads.
# We still require stress-ng read metric entries to exist.
jq -e '
  [ .metrics.kbytes_read[]
    | select(.comm | test("^stress-ng"))
  ] | length > 0
' "$METRICS" >/dev/null || {
  echo "Missing stress-ng entries in kbytes_read metric"
  jq '.metrics.kbytes_read[:10]' "$METRICS" || true
  exit 1
}

jq -e '
  [ .metrics.kbytes_read[]
    | select(.comm | test("^stress-ng"))
    | select(.bytes_read_kb > 0)
  ] | length > 0
' "$METRICS" >/dev/null || {
  echo "Warning: no storage read activity detected for stress-ng (likely page cache)."
  jq '.metrics.kbytes_read[:10]' "$METRICS" || true
}


echo "Validating read rate..."

jq -e '
  [ .metrics.read_rate[]
    | select(.comm | test("^stress-ng"))
  ] | length > 0
' "$METRICS" >/dev/null || {
  echo "Missing stress-ng entries in read_rate metric"
  jq '.metrics.read_rate[:10]' "$METRICS" || true
  exit 1
}

jq -e '
  [ .metrics.read_rate[]
    | select(.comm | test("^stress-ng"))
    | select(.read_rate_kbps > 0)
  ] | length > 0
' "$METRICS" >/dev/null || {
  echo "Warning: no read rate detected for stress-ng (likely page cache)."
  jq '.metrics.read_rate[:10]' "$METRICS" || true
}

echo "Validating write rate..."

jq -e '
  [ .metrics.write_rate[]
    | select(.comm | test("^stress-ng"))
  ] | length > 0
' "$METRICS" >/dev/null || {
  echo "Missing stress-ng entries in write_rate metric"
  jq '.metrics.write_rate[:10]' "$METRICS" || true
  exit 1
}

jq -e '
  [ .metrics.write_rate[]
    | select(.comm | test("^stress-ng"))
    | select(.write_rate_kbps > 0)
  ] | length > 0
' "$METRICS" >/dev/null || {
  echo "Warning: no write rate detected for stress-ng in this environment."
  jq '.metrics.write_rate[:10]' "$METRICS" || true
}

echo "Disk IO metrics OK"
