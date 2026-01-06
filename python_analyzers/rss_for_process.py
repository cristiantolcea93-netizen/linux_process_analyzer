#!/usr/bin/env python3

import argparse
import glob
import re
from datetime import datetime
import matplotlib.pyplot as plt

#  PID lines regex
PID_RE = re.compile(
    r"PID=(?P<pid>\d+)\s+COMM=(?P<comm>\S+).*RSS\(KB\)=(?P<rss>\d+)"
)

# Snapshot start regex
SNAPSHOT_RE = re.compile(
    r"\[(?P<ts>.*?)\]\s+SNAPSHOT START"
)

def parse_args():
    parser = argparse.ArgumentParser(
        description="Analyze process usage from process_analyzer logs."
    )
    parser.add_argument(
        "-d", "--dir", required=True,
        help="Directory containing snapshot log files"
    )
    parser.add_argument(
        "-p", "--process", required=True, nargs="+",
        help="Process name(s) or PID(s) to analyze (can pass multiple)"
    )
    parser.add_argument(
        "--delta", action="store_true",
        help="Plot delta RSS between snapshots"
    )
    parser.add_argument(
        "--out", default="rss_plot.png",
        help="Output PNG file name"
    )
    return parser.parse_args()

def parse_logs(log_dir, processes):
    """
    Return a dict of time series per process.
    {
        process_identifier: ([timestamps], [rss_values])
    }
    """
    data = {p: ([], []) for p in processes}
    current_ts = None

    for fname in sorted(glob.glob(f"{log_dir}/*")):
        with open(fname) as f:
            for line in f:
                line = line.strip()
                # detect snapshot start
                m_snap = SNAPSHOT_RE.match(line)
                if m_snap:
                    current_ts = datetime.strptime(m_snap.group("ts"), "%Y-%m-%d %H:%M:%S.%f")
                    continue
                # detect snapshot end
                if line.startswith("SNAPSHOT END"):
                    current_ts = None
                    continue
                # parse PID line
                if current_ts:
                    m_pid = PID_RE.match(line)
                    if m_pid:
                        pid = m_pid.group("pid")
                        comm = m_pid.group("comm")
                        rss = int(m_pid.group("rss"))

                        for p in processes:
                            if p == pid or p == comm:
                                data[p][0].append(current_ts)
                                data[p][1].append(rss)
    return data

def compute_delta(series):
    """Compute delta between consecutive RSS values"""
    times, values = series
    if len(values) < 2:
        return times, values
    delta = [0]  # first point has 0 delta
    for i in range(1, len(values)):
        delta.append(values[i] - values[i-1])
    return times, delta

def plot_data(data, delta=False, out_file="rss_plot.png"):
    plt.figure(figsize=(14, 7))

    for proc, series in data.items():
        times, values = series
        if delta:
            times, values = compute_delta(series)
        plt.plot(times, values, marker='o', label=proc)

    plt.xlabel("Time")
    plt.ylabel("Delta RSS (KB)" if delta else "RSS (KB)")
    plt.title("Process RSS over time")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    #plt.savefig(out_file)
    #print(f"Plot saved to {out_file}")
    plt.show()

def main():
    args = parse_args()
    data = parse_logs(args.dir, args.process)

    if all(len(series[0]) == 0 for series in data.values()):
        print("No data found for given process(es)")
        return

    plot_data(data, delta=args.delta, out_file=args.out)

if __name__ == "__main__":
    main()

