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
- Disk I/O metrics per process:
  - Total bytes read / written
  - Average disk read/write rate (KB/s)
- Snapshot logging with log rotation
- Graceful shutdown on `CTRL+C` or `SIGTERM`
- Supports infinite runtime mode
- Minimal runtime dependencies (glibc only)

---

## How It Works

At each sampling interval, the tool:
- Reads process data from `/proc/[pid]/stat`, `/proc/[pid]/status`, and `/proc/[pid]/io`
- Stores a snapshot of all running processes
- Accumulates statistics over time using **monotonic timestamps**

At the end of execution (or when interrupted), the requested metrics are calculated and displayed.

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

The following options are **required** for any analysis to run:

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
The tool will print the aggregated statistics before exiting.

---

## Snapshot Logs

Each sampling iteration is logged as a snapshot.

Example snapshot entry:

```
[2026-01-11 19:19:49.611] SNAPSHOT START #################
PID=1548 COMM=systemd STATE=S PPID=1 UTIME=71 STIME=21 RSS(KB)=12592 IOR(KB)=0 IOW(KB)=0 THREADS=1
PID=1558 COMM=pipewire STATE=S PPID=1548 UTIME=2514 STIME=2346 RSS(KB)=16776 IOR(KB)=1180 IOW(KB)=0 THREADS=3
...
SNAPSHOT END #################
```

---

## Log Storage & Rotation

Snapshots are stored in:

```
/tmp/ptime/
```

Log rotation is enabled:
- Maximum of **4 log files**
- Maximum size per file: **5 MB**

---

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
- Short-lived processes may appear appear with fewer samples
---

## Future improvements
- configuration file support (log directory, rotation size, count)
- python utilities for graph generation
--

## Author
Developed as learning and analysis tool for Linux Process monitoring

--
## License
MIT License
