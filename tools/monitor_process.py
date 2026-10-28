#!/usr/bin/env python3
"""Measure an owned command or an existing PID at one-second intervals.

RSS is the sum of resident sets (not physical footprint); shared pages can be
counted more than once. Legacy CPU fields use ps's rolling percentage. Interval
CPU fields use changes in cumulative process CPU time; 100% is one logical CPU.
Only the selected process tree is recorded. Processes that begin and exit wholly
between samples cannot be measured by this sampling method.
"""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import signal
import statistics
import subprocess
import sys
import time


def cpu_time_seconds(value):
    """Parse the BSD ps [[days-]hours:]minutes:seconds CPU-time format."""
    days = 0
    if '-' in value:
        day_text, value = value.split('-', 1)
        days = int(day_text)
    pieces = value.split(':')
    if len(pieces) == 2:
        hours, minutes, seconds = 0, int(pieces[0]), float(pieces[1])
    elif len(pieces) == 3:
        hours, minutes, seconds = int(pieces[0]), int(pieces[1]), float(pieces[2])
    else:
        raise ValueError('unrecognized ps CPU time')
    if min(days, hours, minutes, seconds) < 0 or not math.isfinite(seconds):
        raise ValueError('invalid ps CPU time')
    return days * 86400 + hours * 3600 + minutes * 60 + seconds


def process_snapshot(raw, root):
    """Return selected processes; retain ps start identity when supplied."""
    processes = {}
    for line in raw.splitlines():
        values = line.split(None, 6)
        if len(values) not in (5, 6, 7) or values[4].startswith('Z'):
            continue
        try:
            pid, parent = int(values[0]), int(values[1])
            cpu, rss = float(values[2]), int(values[3])
        except ValueError:
            continue
        cumulative = None
        if len(values) >= 6:
            try:
                cumulative = cpu_time_seconds(values[5])
            except ValueError:
                pass
        processes[pid] = {'parent': parent, 'cpu_percent': cpu,
                          'rss_kb': rss, 'cpu_time_s': cumulative,
                          'start_identity': values[6].strip() if len(values) == 7 else None}
    selected = {root} if root in processes else set()
    while selected:
        more = {pid for pid, data in processes.items() if data['parent'] in selected}
        if more <= selected:
            break
        selected |= more
    return {pid: processes[pid] for pid in selected}


def process_tree(raw, root):
    """Compatibility interface used by existing runners."""
    return [(p['parent'], p['cpu_percent'], p['rss_kb'])
            for p in process_snapshot(raw, root).values()]


def interval_cpu_percent(previous, current, elapsed):
    """Measure one interval; return None if a complete baseline is unavailable.

    A newly seen descendant contributes its accumulated CPU time. A known PID
    with a different start identity is treated as a new process (PID reuse). CPU spent
    after an exited process's last observation is unobservable, not fabricated.
    """
    if previous is None or elapsed <= 0 or not current:
        return None
    total = 0.0
    for pid, process in current.items():
        now = process['cpu_time_s']
        prior = previous.get(pid, {})
        before = prior.get('cpu_time_s')
        if now is None or (pid in previous and before is None):
            return None
        identity = process.get('start_identity')
        old_identity = prior.get('start_identity')
        if identity is not None and old_identity is not None and identity != old_identity:
            before = None
        elif pid in previous and (identity is None) != (old_identity is None):
            return None
        total += now - before if before is not None and now >= before else now
    return round(100 * total / elapsed, 3)


def summarize(rows, settle):
    steady = [r for r in rows if r['elapsed_s'] >= settle]

    def values(key, source):
        return [r[key] for r in source if r.get(key) is not None]

    def average(data):
        return round(statistics.mean(data), 3) if data else None

    def median(data):
        return round(statistics.median(data), 3) if data else None

    def p95(data):
        return round(sorted(data)[math.ceil(len(data) * .95) - 1], 3) if data else None

    rolling = values('cpu_percent', rows)
    steady_rolling = values('cpu_percent', steady)
    interval = values('interval_cpu_percent', rows)
    steady_interval = values('interval_cpu_percent', steady)
    steady_rss = [r['rss_kb'] / 1024 for r in steady]
    return {
        'samples': len(rows),
        'steady_samples': len(steady),
        'interval_cpu_samples': len(interval),
        'steady_interval_cpu_samples': len(steady_interval),
        'cpu_mean_percent': average(rolling),
        'steady_cpu_mean_percent': average(steady_rolling),
        'steady_cpu_median_percent': median(steady_rolling),
        'steady_cpu_p95_percent': p95(steady_rolling),
        'cpu_peak_percent': max(rolling, default=None),
        'interval_cpu_mean_percent': average(interval),
        'steady_interval_cpu_mean_percent': average(steady_interval),
        'steady_interval_cpu_median_percent': median(steady_interval),
        'steady_interval_cpu_p95_percent': p95(steady_interval),
        'interval_cpu_peak_percent': max(interval, default=None),
        'rss_peak_mb': round(max((r['rss_kb'] for r in rows), default=0) / 1024, 3),
        'rss_final_mb': round(rows[-1]['rss_kb'] / 1024, 3) if rows else None,
        'rss_steady_median_mb': median(steady_rss),
        'rss_steady_p95_mb': p95(steady_rss),
        'rss_steady_growth_mb': round((steady[-1]['rss_kb'] - steady[0]['rss_kb']) / 1024, 3) if steady else None,
    }


def owned_group_members(group):
    """Inspect live members only; no command arguments or environment are read."""
    raw = subprocess.check_output(['ps', '-A', '-o', 'pid=,pgid=,stat=,lstart='],
                                  text=True, timeout=5)
    members = {}
    for line in raw.splitlines():
        fields = line.split(None, 3)
        if len(fields) != 4 or fields[2].startswith('Z'):
            continue
        try:
            if int(fields[1]) == group:
                members[int(fields[0])] = fields[3].strip()
        except ValueError:
            continue
    return members


def cleanup_owned_group(process, observed_identities, grace=5):
    """Clean up an owned session, including children surviving their parent.

    This is never called for --pid observers. If the owned root has exited, at
    least one previously observed PID/start identity must still match before
    signalling the group; a reused or unproven group is left untouched and flagged.
    """
    result = {'sent_sigterm': False, 'sent_sigkill': False, 'error': None}
    known = dict(observed_identities)
    try:
        members = owned_group_members(process.pid)
        if not members:
            process.poll()
            return result
        root_alive = process.poll() is None
        confirmed = root_alive or any(known.get(pid) == identity for pid, identity in members.items())
        if not confirmed:
            result['error'] = 'process group remains but ownership cannot be confirmed'
            return result
        known.update(members)
        os.killpg(process.pid, signal.SIGTERM)
        result['sent_sigterm'] = True
        until = time.monotonic() + grace
        while time.monotonic() < until:
            members = owned_group_members(process.pid)
            if not members:
                break
            time.sleep(min(.2, max(0, until - time.monotonic())))
        if members:
            if any(known.get(pid) == identity for pid, identity in members.items()):
                os.killpg(process.pid, signal.SIGKILL)
                result['sent_sigkill'] = True
            else:
                result['error'] = 'process group identity changed during cleanup; not signalled again'
        if process.poll() is None:
            process.wait(timeout=2)
    except ProcessLookupError:
        process.poll()
    except (OSError, subprocess.SubprocessError) as error:
        result['error'] = type(error).__name__ + ': ' + str(error)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, help='observe an existing process; never terminate it')
    parser.add_argument('--duration', type=float, default=35)
    parser.add_argument('--settle', type=float, default=15)
    parser.add_argument('--output', type=Path, required=True, help='output prefix for .csv/.json/.log')
    parser.add_argument('--max-steady-cpu', type=float, help='legacy gate on ps rolling CPU percentage')
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
    collector_error = None
    stop_reason = 'duration_complete'
    root_identity = None
    observed_identities = {}
    cleanup = None
    natural_exit_code = None
    if command:
        log = args.output.with_suffix('.log').open('w')
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        root = process.pid
    else:
        root = args.pid
    start = time.monotonic()
    deadline = start
    previous = None
    previous_at = None
    try:
        with args.output.with_suffix('.csv').open('w', newline='') as csvfile:
            writer = csv.DictWriter(csvfile, fieldnames=['elapsed_s', 'cpu_percent',
                'interval_cpu_percent', 'rss_kb', 'processes'])
            writer.writeheader()
            while time.monotonic() - start < args.duration:
                try:
                    raw = subprocess.check_output(['ps', '-A', '-o',
                        'pid=,ppid=,%cpu=,rss=,stat=,time=,lstart='], text=True, timeout=5)
                except (OSError, subprocess.SubprocessError) as error:
                    collector_error = type(error).__name__ + ': ' + str(error)
                    stop_reason = 'collector_error'
                    break
                members = process_snapshot(raw, root)
                if not members:
                    stop_reason = 'root_exited'
                    break
                identity = members[root]['start_identity']
                if identity is None:
                    collector_error = 'ps did not return a process start identity'
                    stop_reason = 'identity_unavailable'
                    break
                if root_identity is not None and identity != root_identity:
                    collector_error = 'observed root PID was reused; stopped before sampling the replacement'
                    stop_reason = 'root_identity_changed'
                    break
                root_identity = identity
                observed_identities.update({pid: p['start_identity'] for pid, p in members.items()
                                            if p['start_identity'] is not None})
                sampled_at = time.monotonic()
                row = {'elapsed_s': round(sampled_at - start, 3),
                       'cpu_percent': round(sum(p['cpu_percent'] for p in members.values()), 3),
                       'interval_cpu_percent': interval_cpu_percent(previous, members,
                           sampled_at - previous_at if previous_at is not None else 0),
                       'rss_kb': sum(p['rss_kb'] for p in members.values()), 'processes': len(members)}
                previous, previous_at = members, sampled_at
                rows.append(row)
                writer.writerow(row)
                csvfile.flush()
                if process is not None and process.poll() is not None:
                    stop_reason = 'root_exited'
                    break
                deadline += 1
                time.sleep(max(0, min(deadline - time.monotonic(), args.duration - (time.monotonic() - start))))
    finally:
        monitoring_duration = time.monotonic() - start
        if process is not None:
            natural_exit_code = process.poll()
            cleanup = cleanup_owned_group(process, observed_identities)
            ended_by_monitor = natural_exit_code is None and cleanup['sent_sigterm']
        if log is not None:
            log.close()
    result = summarize(rows, args.settle)
    result.update(pid=root, duration_s=round(time.monotonic() - start, 3),
                  settle_s=args.settle, ended_by_monitor=ended_by_monitor,
                  exit_code=process.returncode if process is not None else None,
                  natural_exit_code=natural_exit_code,
                  requested_duration_s=args.duration,
                  monitoring_duration_s=round(monitoring_duration, 3),
                  stop_reason=stop_reason,
                  completed_full_duration=stop_reason == 'duration_complete' and collector_error is None,
                  measurement_status='failed' if collector_error else 'complete' if stop_reason == 'duration_complete' else 'partial',
                  root_start_identity=root_identity,
                  identity_metric='ps lstart (one-second resolution); first observed identity is the baseline',
                  owned_group_cleanup=cleanup,
                  cpu_metric='cpu_percent: ps rolling estimate; interval_cpu_percent: cumulative CPU-time deltas',
                  cpu_unit='percent of one logical CPU; may exceed 100',
                  percentile_method='nearest-rank',
                  memory_unit='MiB (legacy output field names retain mb)',
                  memory_metric='sum of process RSS; shared pages may be counted more than once',
                  collector_error=collector_error,
                  sampling_limit='processes born and exited wholly between samples are unobserved')
    violations = []
    if collector_error:
        violations.append('resource collection failed: ' + collector_error)
    if cleanup and cleanup['error']:
        violations.append('owned process cleanup failed: ' + cleanup['error'])
    if not rows:
        violations.append('no process samples')
    if natural_exit_code not in (None, 0):
        violations.append('command failed with exit code ' + str(natural_exit_code))
    if (args.max_steady_cpu is not None or args.max_rss_mb is not None) and not result['completed_full_duration']:
        violations.append('requested measurement duration was not completed; performance gate is inconclusive')
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
