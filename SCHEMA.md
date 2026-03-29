# metrics.json Schema

This document describes the JSON schema used by `process_analyzer` for the aggregated metrics output (`metrics.json`).

The schema is **versioned**, forward-compatible, and designed for post-run analysis and automated processing.

---

## File Overview

```json
{
  "meta": { ... },
  "metrics": { ... }
}
```

---

## Root Object

| Field   | Type   | Required | Description |
|--------|--------|----------|-------------|
| meta   | object | yes      | Metadata describing the execution context |
| metrics| object | yes      | Aggregated per-process metrics |

---

## `meta` Object

Execution and environment metadata.

```json
"meta": {
  "tool": "./process_analyzer",
  "version": "process_analyzer V1.0",
  "schema_version": "1.0",
  "hostname": "host-name",
  "interval_ms": 20,
  "start_time": "YYYY-MM-DD HH:MM:SS.mmm",
  "end_time": "YYYY-MM-DD HH:MM:SS.mmm",
  "duration_sec": 1897.140,
  "snapshots": 94858
}
```

### Fields

| Field | Type | Description |
|-----|-----|-------------|
| tool | string | Executable name or invocation path |
| version | string | Tool version |
| schema_version | string | Schema version (semantic compatibility) |
| hostname | string | System hostname |
| interval_ms | number | Sampling interval in milliseconds |
| start_time | string | Human-readable start timestamp (CLOCK_REALTIME) |
| end_time | string | Human-readable end timestamp (CLOCK_REALTIME) |
| duration_sec | number | Total runtime duration in seconds |
| snapshots | integer | Total number of snapshots collected |

---

## `metrics` Object

Contains aggregated metrics per category.  
Each metric is represented as an **array of process entries**, typically sorted descending by metric value.

```json
"metrics": {
  "cpu_average": [ ... ],
  "rss_average": [ ... ],
  "rss_increase": [ ... ],
  "rss_delta": [ ... ],
  "kbytes_read": [ ... ],
  "written_kbytes": [ ... ],
  "read_rate": [ ... ],
  "write_rate": [ ... ],
  "fds_increase": [ ... ],
  "opened_fds": [ ... ],
  "fds_delta": [ ... ]
}
```

Only metrics explicitly requested via CLI options will appear in this object.

---

## Common Process Fields

All metric entries share the following common fields:

| Field | Type | Description |
|-----|-----|-------------|
| pid | integer | Process ID |
| comm | string | Process command name |
| state | string | Process state (`R`, `S`, `D`, etc.) |
| threads | integer | Number of threads |
| records | integer | Number of samples collected for this process |

---

## Metric Definitions

### `cpu_average`

Average CPU usage per process over its lifetime.

| Field | Type | Description |
|-----|-----|-------------|
| cpu_avg_pct | number | Average CPU usage percentage |

---

### `rss_average`

Average resident memory usage (RSS).

| Field | Type | Description |
|-----|-----|-------------|
| rss_avg_kb | integer | Average RSS in kilobytes |

---

### `rss_increase`

Increase in RSS relative to the first observed sample.

| Field | Type | Description |
|-----|-----|-------------|
| rss_incr_kb | integer | RSS growth since first sample (KB) |

---

### `rss_delta`

Difference between the last and first RSS sample.

| Field | Type | Description |
|-----|-----|-------------|
| rss_delta_kb | integer | RSS delta (can be negative) |

---

### `kbytes_read`

Total disk read per process.

| Field | Type | Description |
|-----|-----|-------------|
| bytes_read_kb | integer | Total bytes read (KB) |

---

### `written_kbytes`

Total disk write per process.

| Field | Type | Description |
|-----|-----|-------------|
| written_bytes_kb | integer | Total bytes written (KB) |

---

### `read_rate`

Average disk read rate.

| Field | Type | Description |
|-----|-----|-------------|
| read_rate_kbps | number | Average read rate (KB/s) |

---

### `write_rate`

Average disk write rate.

| Field | Type | Description |
|-----|-----|-------------|
| write_rate_kbps | number | Average write rate (KB/s) |

---

### `fds_increase`

Increase in opened file descriptors relative to the first observed sample.

| Field | Type | Description |
|-----|-----|-------------|
| no_of_fds_increase | integer | Opened file descriptor increase since first sample |

---

### `opened_fds`

Number of currently opened file descriptors for the process.

| Field | Type | Description |
|-----|-----|-------------|
| no_of_opened_fds | integer | Opened file descriptor count in the latest sample |

---

### `fds_delta`

Difference between the latest and first observed file descriptor count.

| Field | Type | Description |
|-----|-----|-------------|
| fds_delta | integer | File descriptor delta (can be negative) |

---

## Notes

- All rate and delta calculations use **monotonic time**
- Human-readable timestamps use `CLOCK_REALTIME`
- Short-lived processes may have lower `records` values
- Metrics are aggregated per PID for the lifetime of the process

---

## Compatibility

- Schema version: **1.0**
- Backward-compatible extensions may add new metric sections
- Existing fields will not change semantics across minor versions

---

## Related Files

- Snapshot logs (`.log`, `.jsonl`)
- `metrics.json` (this schema)
- Source: `process_stats/`
