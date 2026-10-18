#!/usr/bin/env python3
"""Measure an owned command or an existing PID at one-second intervals.

RSS is the sum of resident sets (not physical footprint); shared pages can be
counted more than once. CPU uses ps's per-process percentage and can exceed 100
for multithreaded work. Only the selected process tree is recorded.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import signal
import statistics
import subprocess
import sys
import time


def process_tree(raw, root):
    processes = {}
    for line in raw.splitlines():
        values = line.split()
        if len(values) != 5 or values[4].startswith('Z'):
            continue
        try:
            pid, parent = int(values[0]), int(values[1])
            processes[pid] = (parent, float(values[2]), int(values[3]))
        except ValueError:
            continue
    selected = {root} if root in processes else set()
    while selected:
        more = {pid for pid, data in processes.items() if data[0] in selected}
        if more <= selected:
            break
        selected |= more
    return [processes[pid] for pid in selected]


def summarize(rows, settle):
    steady = [r for r in rows if r['elapsed_s'] >= settle]
    def average(values):
        return round(statistics.mean(values), 3) if values else None
    return {
        'samples': len(rows),
        'steady_samples': len(steady),
        'cpu_mean_percent': average([r['cpu_percent'] for r in rows]),
        'steady_cpu_mean_percent': average([r['cpu_percent'] for r in steady]),
        'steady_cpu_median_percent': round(statistics.median([r['cpu_percent'] for r in steady]), 3) if steady else None,
        'cpu_peak_percent': max((r['cpu_percent'] for r in rows), default=None),
        'rss_peak_mb': round(max((r['rss_kb'] for r in rows), default=0) / 1024, 3),
        'rss_final_mb': round(rows[-1]['rss_kb'] / 1024, 3) if rows else None,
        'rss_steady_growth_mb': round((steady[-1]['rss_kb'] - steady[0]['rss_kb']) / 1024, 3) if steady else None,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, help='observe an existing process; never terminate it')
    parser.add_argument('--duration', type=float, default=35)
    parser.add_argument('--settle', type=float, default=15)
    parser.add_argument('--output', type=Path, required=True, help='output prefix for .csv/.json/.log')
    parser.add_argument('--max-steady-cpu', type=float)
    parser.add_argument('--max-rss-mb', type=float)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if bool(args.pid) == bool(command):
        parser.error('provide exactly one of --pid or a command after --')
    if args.duration <= 0 or args.settle < 0:
        parser.error('duration must be positive and settle cannot be negative')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    process = None
    log = None
    ended_by_monitor = False
    if command:
        log = args.output.with_suffix('.log').open('w')
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        root = process.pid
    else:
        root = args.pid
    start = time.monotonic()
    deadline = start
    try:
        with args.output.with_suffix('.csv').open('w', newline='') as csvfile:
            writer = csv.DictWriter(csvfile, fieldnames=['elapsed_s', 'cpu_percent', 'rss_kb', 'processes'])
            writer.writeheader()
            while time.monotonic() - start < args.duration:
                raw = subprocess.check_output(['ps', '-A', '-o', 'pid=,ppid=,%cpu=,rss=,stat='], text=True)
                members = process_tree(raw, root)
                if not members:
                    break
                row = {'elapsed_s': round(time.monotonic() - start, 3),
                       'cpu_percent': round(sum(p[1] for p in members), 3),
                       'rss_kb': sum(p[2] for p in members), 'processes': len(members)}
                rows.append(row)
                writer.writerow(row)
                csvfile.flush()
                if process is not None and process.poll() is not None:
                    break
                deadline += 1
                time.sleep(max(0, min(deadline - time.monotonic(), args.duration - (time.monotonic() - start))))
    finally:
        if process is not None and process.poll() is None:
            ended_by_monitor = True
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
        if log is not None:
            log.close()
    result = summarize(rows, args.settle)
    result.update(pid=root, duration_s=round(time.monotonic() - start, 3),
                  settle_s=args.settle, ended_by_monitor=ended_by_monitor,
                  exit_code=process.returncode if process is not None else None,
                  memory_metric='sum of process RSS; shared pages may be counted more than once')
    violations = []
    if not rows:
        violations.append('no process samples')
    if process is not None and not ended_by_monitor and process.returncode != 0:
        violations.append('command failed with exit code ' + str(process.returncode))
    if args.max_steady_cpu is not None:
        if result['steady_cpu_mean_percent'] is None:
            violations.append('no samples after the settling period')
        elif result['steady_cpu_mean_percent'] > args.max_steady_cpu:
            violations.append('steady CPU exceeded the requested ceiling')
    if args.max_rss_mb is not None and result['rss_peak_mb'] > args.max_rss_mb:
        violations.append('peak RSS exceeded the requested ceiling')
    result['violations'] = violations
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return 1 if violations else 0


if __name__ == '__main__':
    sys.exit(main())
