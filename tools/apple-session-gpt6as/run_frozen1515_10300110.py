#!/usr/bin/env python3
"""Bounded complete frozen1515 replay with unchanged schedules and 1Hz monitoring."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--max-seconds', type=float, default=2100)
    parser.add_argument('--require-url-all-pass', action='store_true',
        help='Run the unchanged77 URL variants first and stop before other groups if any fails')
    args = parser.parse_args()
    root = Path.cwd().resolve()
    binary, out = args.binary.resolve(), args.output.resolve()
    if not out.is_relative_to(root):
        raise RuntimeError('Output must remain in this repository')
    sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
    if sha(binary) != args.sha256:
        raise RuntimeError('Selected binary hash does not match')
    out.mkdir(parents=True, exist_ok=False)
    started, utc = time.monotonic(), lambda: datetime.now(timezone.utc).isoformat()
    deadline = started + args.max_seconds
    receipt = {'schema': 1, 'task': '10.30-0110', 'startedUTC': utc(),
        'scope': 'Complete unchanged frozen1515 priority WPT inventory; not the entire official WPT suite',
        'binary': str(binary), 'binarySHA256': args.sha256, 'maxSeconds': args.max_seconds,
        'orchestratorSHA256': sha(__file__), 'stages': [], 'complete': False,
        'latestSummary': {'scheduled': 1515, 'run': 0, 'fullyPassing': 0, 'unrun': 1515},
        'urlAcceptanceGate': {'required': args.require_url_all_pass, 'expectedFullyPassing': 77,
            'evaluated': False, 'passed': None}}
    groups = ['dom', 'events', 'storage', 'url', 'encoding-streams', 'encoding-core',
        'microtasks', 'headers', 'encoding-rest']
    if args.require_url_all_pass:
        groups.remove('url')
        groups.insert(0, 'url')
    receipt['executionOrder'] = groups

    def save():
        temporary = out / 'summary.json.tmp'
        with temporary.open('w') as stream:
            json.dump(receipt, stream, indent=2)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(out / 'summary.json')
        if json.loads((out / 'summary.json').read_text()) != receipt:
            raise RuntimeError('Atomic full-cohort summary readback mismatch')

    def measured(label, command, limit):
        row = {'label': label, 'command': command, 'startedUTC': utc()}
        receipt['stages'].append(row)
        save()
        start = time.monotonic()
        monitor = [sys.executable, 'tools/monitor_process.py', '--duration', str(limit),
            '--settle', '0', '--output', str(out / label), '--', *command]
        with (out / (label + '-driver.log')).open('w') as log:
            process = subprocess.Popen(monitor, stdout=log, stderr=subprocess.STDOUT)
            try:
                code = process.wait(timeout=limit + 15)
            except subprocess.TimeoutExpired:
                row['outerTimeout'] = True
                process.terminate()
                try:
                    code = process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    process.kill()
                    code = process.wait(timeout=5)
        row.update(finishedUTC=utc(), durationSeconds=round(time.monotonic() - start, 6), exitCode=code)
        path = out / (label + '.json')
        if path.exists():
            metric = json.loads(path.read_text())
            row.update(monitorSHA256=sha(path), naturalExitCode=metric.get('natural_exit_code'),
                collectorError=metric.get('collector_error'), endedByMonitor=metric.get('ended_by_monitor'),
                cleanupError=(metric.get('owned_group_cleanup') or {}).get('error'),
                intervalCpuSamples=metric.get('interval_cpu_samples'),
                intervalCpuP95Percent=metric.get('steady_interval_cpu_p95_percent'),
                intervalCpuPeakPercent=metric.get('interval_cpu_peak_percent'), rssPeakMiB=metric.get('rss_peak_mb'),
                maximumSamplingGapSeconds=metric.get('max_sampling_gap_s'))
        save()
        if code != 0 or row.get('naturalExitCode') != 0 or row.get('collectorError') or row.get('cleanupError') or row.get('endedByMonitor') or row.get('outerTimeout'):
            raise RuntimeError('Measurement stage failed: ' + label)

    runner = 'tools/apple-session-gpt6as/run_task_wpt_102900614.py'
    plan, summary = out / 'cohort/cohort.json', out / 'cohort/priority-summary.json'
    save()
    try:
        measured('prepare', [sys.executable, runner, 'prepare', '--binary', str(binary),
            '--sha256', args.sha256, '--output', str(plan.parent)], 60)
        for group in groups:
            attempt, prior_count = 0, -1
            while True:
                remaining = deadline - time.monotonic()
                if remaining < 130:
                    raise TimeoutError('Bounded full-cohort budget reached; remaining variants stay unrun')
                attempt += 1
                # The unchanged runner may start a65-second long case just
                # before its budget expires. Allow that case and cleanup to
                # finish instead of introducing a spurious timeout.
                budget = min(240, int(remaining - 105))
                measured(group + '-' + str(attempt), [sys.executable, runner, 'run', '--plan', str(plan),
                    '--group', group, '--max-seconds', str(budget)], budget + 90)
                results = json.loads(summary.read_text())
                receipt['latestSummary'] = {key: results[key] for key in
                    ['scheduled', 'run', 'fullyPassing', 'unrun', 'assertions', 'harnessStatuses', 'categories', 'groups']}
                save()
                current = results['groups'][group]
                if current['unrun'] == 0:
                    if group == 'url' and args.require_url_all_pass:
                        accepted = current['scheduled'] == current['run'] == current['fullyPassing'] == 77
                        receipt['urlAcceptanceGate'].update(evaluated=True, passed=accepted,
                            observed=dict(current), evaluatedUTC=utc())
                        save()
                        if not accepted:
                            raise RuntimeError('Frozen77 URL acceptance failed; preserve all failures and remaining unrun variants')
                    break
                if current['run'] <= prior_count:
                    raise RuntimeError('No new immutable case records in ' + group)
                prior_count = current['run']
        results = json.loads(summary.read_text())
        receipt['complete'] = results['scheduled'] == results['run'] == 1515 and results['unrun'] == 0
        receipt['prioritySummarySHA256'] = sha(summary)
        receipt['cohortSHA256'] = sha(plan)
    except Exception as error:
        receipt['error'] = type(error).__name__ + ': ' + str(error)
    finally:
        receipt.update(finishedUTC=utc(), durationSeconds=round(time.monotonic() - started, 6),
            binarySHA256After=sha(binary))
        receipt['sameArtifact'] = receipt['binarySHA256After'] == args.sha256
        save()
    print(json.dumps({key: value for key, value in receipt.items() if key not in ('stages', 'latestSummary')}), flush=True)
    return 0 if receipt['complete'] and receipt['sameArtifact'] and not receipt.get('error') else 1


if __name__ == '__main__':
    raise SystemExit(main())
