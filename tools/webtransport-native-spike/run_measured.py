#!/usr/bin/env python3
"""Run an owned build/probe process with one-second process-tree CPU/RSS samples.

No browser profiles are accessed. Logs are for this public loopback experiment.
Percent CPU is the operating system's ps value, summed over the owned tree;
it is not a benchmark of page rendering or a claim about system-wide load.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import time


def owned_sample(root_pid):
    rows = subprocess.run(['ps', '-axo', 'pid=,ppid=,pcpu=,rss='], capture_output=True, text=True, timeout=3, cwd='/')
    rows.check_returncode()
    parsed = {}
    for row in rows.stdout.splitlines():
        fields = row.split()
        if len(fields) != 4:
            continue
        try:
            pid, parent = int(fields[0]), int(fields[1])
            parsed[pid] = (parent, float(fields[2]), int(fields[3]))
        except ValueError:
            continue
    selected = {root_pid}
    while True:
        additional = {pid for pid, (parent, _, _) in parsed.items() if parent in selected}
        if additional <= selected:
            break
        selected |= additional
    owned = [parsed[pid] for pid in selected if pid in parsed]
    return {'cpuPercentPS': round(sum(row[1] for row in owned), 2), 'rssKiB': sum(row[2] for row in owned), 'processCount': len(owned)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--label', required=True)
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('--max-rss-mib', type=int, default=4096)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command or not args.label.replace('-', '').replace('_', '').isalnum():
        raise SystemExit('Invalid measured command.')
    base = Path(__file__).resolve().parent
    output = base / 'output'
    output.mkdir(exist_ok=True)
    started = time.monotonic()
    report = {'schemaVersion': 1, 'label': args.label, 'kind': 'standalone-native-experiment', 'lumenBrowserSupport': False, 'sampleIntervalSeconds': 1, 'cpuMetric': 'sum of OS ps percent CPU for the owned process tree', 'samples': [], 'timedOut': False, 'memoryLimitExceeded': False}
    environment = os.environ.copy()
    environment.update({'CARGO_BUILD_JOBS': '2', 'CARGO_NET_RETRY': '1', 'CARGO_HTTP_TIMEOUT': '30'})
    process = None
    report['observerFailed'] = False
    with (output / (args.label + '.log')).open('wb') as log:
        try:
            process = subprocess.Popen(command, cwd=base, env=environment, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            report['pid'] = process.pid
            while process.poll() is None:
                sample = owned_sample(process.pid)
                sample['elapsedSeconds'] = round(time.monotonic() - started, 3)
                report['samples'].append(sample)
                report['timedOut'] = sample['elapsedSeconds'] > args.timeout
                report['memoryLimitExceeded'] = sample['rssKiB'] > args.max_rss_mib * 1024
                if report['timedOut'] or report['memoryLimitExceeded']:
                    break
                time.sleep(1)
        except (OSError, subprocess.SubprocessError, ValueError):
            # Keep error text and process arguments out of the result schema.
            report['observerFailed'] = True
        finally:
            if process is not None and process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    process.wait(timeout=5)
            report['exitCode'] = process.wait() if process is not None else None
    report['elapsedSeconds'] = round(time.monotonic() - started, 3)
    report['peakRSSMiB'] = round(max((item['rssKiB'] for item in report['samples']), default=0) / 1024, 2)
    report['peakCPUPercentPS'] = max((item['cpuPercentPS'] for item in report['samples']), default=0)
    report['processExited'] = process is None or process.poll() is not None
    report_path = output / (args.label + '.measurement.json')
    report_path.write_text(json.dumps(report, indent=2))
    print(json.dumps({key: value for key, value in report.items() if key != 'samples'}))
    return 0 if report['exitCode'] == 0 and not report['timedOut'] and not report['memoryLimitExceeded'] and not report['observerFailed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
