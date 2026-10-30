#!/usr/bin/env python3
"""Bounded, one-second process sampling without command-line arguments/secrets.

RSS is a resident-set estimate, not unique or compressed memory. Lumen has a
custom engine; unrelated WebKit XPC processes and UI harnesses are recorded
separately. Use explicit scenario labels for comparisons.
"""
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import time


def parse_cputime(value):
    days = 0
    if "-" in value:
        days, value = value.split("-", 1)
        days = int(days)
    parts = [float(p) for p in value.split(":")]
    seconds = 0.0
    for part in parts:
        seconds = seconds * 60 + part
    return days * 86400 + seconds


def add_cpu_deltas(rows, previous, elapsed):
    for row in rows:
        prior = previous.get(row["pid"])
        row["cpu_interval_percent"] = (max(0.0, row["cpu_seconds"] - prior["cpu_seconds"]) / elapsed * 100
            if prior and elapsed > 0 and row["cpu_seconds"] >= prior["cpu_seconds"] else None)


def sample_processes():
    output = subprocess.check_output(
        ["ps", "-axo", "pid=,ppid=,pcpu=,rss=,time=,comm="], text=True
    )
    rows = []
    for line in output.splitlines():
        fields = line.strip().split(None, 5)
        if len(fields) != 6:
            continue
        try:
            rows.append({"pid": int(fields[0]), "ppid": int(fields[1]),
                         "cpu_percent": float(fields[2]), "rss_kib": int(fields[3]),
                         "cpu_seconds": parse_cputime(fields[4]), "name": Path(fields[5]).name})
        except ValueError:
            continue
    return rows


def classify(rows):
    lumen = [r for r in rows if r["name"].lower() in ("lumen", "lumen-jit", "lumen-jitless")]
    helpers = [r for r in rows if "lumen" in r["name"].lower() and r not in lumen]
    webkit = [r for r in rows if "webkit" in r["name"].lower()]
    chrome = [r for r in rows if "chrome" in r["name"].lower() or "chromium" in r["name"].lower()]
    safari = [r for r in rows if r["name"] == "Safari"]
    excluded = {r["pid"] for r in lumen + helpers + webkit + chrome + safari}
    background = sorted((r for r in rows if r["pid"] not in excluded),
                        key=lambda r: r["cpu_percent"], reverse=True)[:6]
    return {"lumen_hosts": lumen, "lumen_test_helpers": helpers, "webkit_unattributed": webkit,
            "chrome_all_profiles": chrome, "safari_hosts": safari,
            "top_background": background}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--mode-file", required=True)
    parser.add_argument("--seconds", type=int, default=7200)
    args = parser.parse_args()
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    due = start
    with output.open("a", buffering=1) as stream:
        stream.write(json.dumps({"type": "metadata", "pid": os.getpid(), "interval_seconds": 1,
            "version": 2, "max_seconds": args.seconds,
            "cpu_unit": "cpu_interval_percent: delta of ps cumulative CPU time / wall interval; cpu_percent: ps reported rolling/lifetime value",
            "rss_unit": "KiB, summed RSS may double-count shared pages",
            "webkit_attribution": "Unattributed; not charged to Lumen or Safari", "clock": "UTC"}) + "\n")
        previous = {}
        previous_time = None
        while time.monotonic() - start < args.seconds:
            try:
                mode = Path(args.mode_file).read_text().strip()[:100]
            except OSError:
                mode = "development-uncontrolled"
            try:
                rows = sample_processes()
                current_time = time.monotonic()
                add_cpu_deltas(rows, previous, current_time - previous_time if previous_time else 0)
                previous = {r["pid"]: r for r in rows}
                previous_time = current_time
                groups = classify(rows)
                record = {"type": "sample", "time": dt.datetime.now(dt.timezone.utc).isoformat(),
                          "elapsed_seconds": round(time.monotonic() - start, 3), "scenario": mode,
                          "load_average": os.getloadavg(), **groups}
                record["lumen_host_cpu_percent"] = sum(p["cpu_percent"] for p in groups["lumen_hosts"])
                intervals = [p["cpu_interval_percent"] for p in groups["lumen_hosts"] if p["cpu_interval_percent"] is not None]
                record["lumen_host_cpu_interval_percent"] = sum(intervals) if intervals else None
                record["lumen_host_rss_kib"] = sum(p["rss_kib"] for p in groups["lumen_hosts"])
                stream.write(json.dumps(record) + "\n")
            except Exception as error:
                stream.write(json.dumps({"type": "error", "message": str(error)}) + "\n")
            due += 1
            time.sleep(max(0, due - time.monotonic()))


if __name__ == "__main__":
    main()

