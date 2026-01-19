# Linux Process Analyzer

`process_analyzer` is a lightweight Linux command-line tool written in C that periodically samples process information from `/proc` and provides aggregated statistics at the end of execution.

The tool is designed for **low-overhead monitoring**, accurate **time-based calculations**, and **post-run analysis** of CPU, memory (RSS), and disk I/O usage per process.

---

## Features

- Periodic process sampling with configurable interval
- Accurate CPU usage calculation using monotonic time
- Memory (RSS) tracking:
  - Average RSS
  - RSS increase since startup
  - RSS delta
- Disk I/O s per process:
  - Total bytes read / written
  - Average disk read/write rate (KB/s)
- Snapshot logging with log rotation
- Graceful shutdown on `CTRL+C` or `SIGTERM`
- Supports infinite runtime mode
- Minimal runtime dependencies (glibc only)

---

## Typical Use Cases

- Investigating long-running background processes
- Identifying disk-heavy applications over time
- Post-mortem analysis after performance degradation

## What this tool is NOT

- Not a real-time interactive monitor (like `top` or `htop`)
- Not a system-wide resource accounting tool
- Not intended for per-thread profiling


## How It Works

At each sampling interval, the tool:
- Reads process data from `/proc/[pid]/stat`, `/proc/[pid]/status`, and `/proc/[pid]/io`
- Stores a snapshot of all running processes (in text and jsonl format)
- Accumulates statistics over time using **monotonic timestamps**
- Designed to minimize per-sample overhead even at small intervals (tens of milliseconds)

At the end of execution (or when interrupted), the requested s are calculated and displayed on the console + a metrics.json file is generated.

---

## Time Handling

- **`CLOCK_MONOTONIC`** is used internally for all delta and rate calculations  
  (CPU usage, disk I/O rates, averages)
- **`CLOCK_REALTIME`** is used only for human-readable timestamps in logs

This guarantees stable measurements even if the system clock changes.

---

## Usage

```bash
./process_analyzer [options]
```

### Mandatory options

The following options are **required** for the tool to run and collect snapshots:

- `-i`, `--interval <dur>` — sampling interval  
- `-n`, `--count <N>` — number of snapshots (`infinity` is allowed)

If either of these is missing, argument validation will fail.

---

## Options

```
-i, --interval <dur>        Interval (e.g. 500ms, 1s, 2m)
-n, --count <N>             Number of snapshots.
                            Use "infinity" to collect snapshots until interrupted

-c, --cpu_usage <N>         Display top N processes by average CPU usage
-r, --rss_usage <N>         Display top N processes by average RSS usage
-s, --rss_increase <N>      Display top N processes by RSS increase since startup
-d, --rss_delta <N>         Display top N processes by RSS delta

-e, --bytes_read <N>        Display top N processes by total disk read (KB)
-f, --bytes_write <N>       Display top N processes by total disk write (KB)
-g, --read_rate <N>         Display top N processes by disk read rate (KB/s)
-a, --write_rate <N>        Display top N processes by disk write rate (KB/s)

-j, --delete_old_files      Delete log files from previous executions
-v, --version               Display tool version and exit
-h, --help                  Show help message
```

---

## Example

```bash
./process_analyzer \
    -i 50ms \
    -n infinity \
    -c 10 -r 10 -s 10 -d 10 \
    -e 10 -f 10 \
    -g 15 -a 15 \
    -j
```

Stop the program using `CTRL+C`.  
The tool will print the aggregated statistics and generate a json file (metrics.json) before exiting. For now, the metrics.json file is generated at the end of the execution as long as any metric is requested via at least one of the following CLI parameters:

- `--cpu_usage`
- `--rss_usage`
- `--rss_increase`
- `--rss_delta`
- `--bytes_read`
- `--bytes_write`
- `--read_rate`
- `--write_rate`

---

## Snapshot Logs

Each sampling iteration is logged as a snapshot.

Example snapshot entries:

1) Text format
```
[2026-01-11 19:19:49.611] SNAPSHOT START #################
PID=1548 COMM=systemd STATE=S PPID=1 UTIME=71 STIME=21 RSS(KB)=12592 IOR(KB)=0 IOW(KB)=0 THREADS=1
PID=1558 COMM=pipewire STATE=S PPID=1548 UTIME=2514 STIME=2346 RSS(KB)=16776 IOR(KB)=1180 IOW(KB)=0 THREADS=3
...
SNAPSHOT END #################
```
2) jsonl format
```
{"timestamp":"2026-01-19 21:47:41.677","pid":2603,"comm":"eclipse","state":"S","ppid":1800,"utime":7,"stime":1,"rss_kb":23376,"io_read_kb":640,"io_write_kb":0,"threads":5}
{"timestamp":"2026-01-19 21:47:41.677","pid":2618,"comm":"java","state":"S","ppid":2603,"utime":19907,"stime":1161,"rss_kb":1129624,"io_read_kb":212404,"io_write_kb":16744,"threads":70}
{"timestamp":"2026-01-19 21:47:41.677","pid":2677,"comm":"nautilus","state":"S","ppid":1515,"utime":204,"stime":32,"rss_kb":178828,"io_read_kb":6132,"io_write_kb":80,"threads":19}
```

---

## Log Storage & Rotation

- Snapshots (text + jsonl)
- s.json file generated at the end are stored in:

```
/tmp/ptime/
```

Log rotation is enabled for both text and jsonl files:
- Maximum of **4 log files** of each type
- Maximum size per file: **5 MB**

---

## JSON Output

- `s.json` follows a versioned schema
- Schema documentation is available in `SCHEMA.md`
- Snapshot data is written in JSON Lines format (`.jsonl`)

## Build Instructions

```bash
cd code
./make.sh
```

Or:

```bash
mkdir build
cd build
cmake ..
ninja
```

---

## Project Structure

```
code/
├── process_analyzer.c          # main entry point
├── args_parser/
│   ├── args_parser.c
│   └── args_parser.h
├── config/
│   ├── config.h
├── process_snapshot/
│   ├── process_snapshot.c
│   └── process_snapshot.h
├── process_stats/
│   ├── process_stats.c
│   └── process_stats.h
├── third_party/
│   └── uthash/
│       └── uthash.h
├── CMakeLists.txt
└── make.sh

```

---

## Limitations & Notes
- Requires Linux with /proc filesystem
- Disk I/O statistics depend on kernel support for /proc/[pid]/io
- Short-lived processes may appear with fewer samples
- Tested on modern Linux distributions using systemd

---

## Future improvements
- configuration file support (log directory, rotation size, count)
- python utilities for graph generation
---

## Author
Developed as learning and analysis tool for Linux Process monitoring.


## License
MIT License
