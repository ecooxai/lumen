#!/usr/bin/env python3
"""One-Hz task-owned process-tree telemetry, with no browser profile access.

CPU comes from successive cumulative CPU-time samples (100% = one core).
First-seen processes cannot have an interval delta and are reported explicitly.
RSS is a summed resident-set upper bound, not unique physical footprint.
"""
import argparse
from collections import defaultdict
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import platform
import signal
import statistics
import subprocess
import time


def cpu_seconds(value):
    days = 0
    if '-' in value:
        day, value = value.split('-', 1)
        days = int(day)
    parts = [float(x) for x in value.split(':')]
    total = 0
    for part in parts:
        total = total * 60 + part
    return days * 86400 + total


def parse_ps(text):
    rows = {}
    for line in text.splitlines():
        fields = line.strip().split(None, 5)
        if len(fields) != 6:
            continue
        try:
            pid, ppid, reported_cpu, rss, elapsed_cpu, command = fields
            rows[int(pid)] = {'ppid': int(ppid), 'ps_cpu_percent': float(reported_cpu),
                              'rss_bytes': int(rss) * 1024,
                              'cpu_seconds': cpu_seconds(elapsed_cpu),
                              'name': Path(command).name, 'cpu_source': 'ps cumulative CPU time'}
        except ValueError:
            continue
    return rows


def process_snapshot():
    result = subprocess.run(['ps', '-axo', 'pid=,ppid=,%cpu=,rss=,time=,comm='],
                            capture_output=True, text=True, check=True, timeout=4)
    rows = parse_ps(result.stdout)
    if platform.system() == 'Linux':
        ticks = os.sysconf('SC_CLK_TCK')
        for pid, row in list(rows.items()):
            try:
                stat = Path(f'/proc/{pid}/stat').read_text()
                fields = stat[stat.rfind(')') + 2:].split()
                row['cpu_seconds'] = proc_cpu_seconds(stat, ticks)
                row['cpu_source'] = 'proc stat jiffies'
                row['start_ticks'] = int(fields[19])
                row['state'] = fields[0]
                if row['state'] == 'Z':
                    del rows[pid]
            except (OSError, ValueError, IndexError):
                # Never switch a Linux PID from precise jiffies to rounded ps
                # CPU time: that can manufacture a later interval spike.
                del rows[pid]
    return rows


def proc_cpu_seconds(stat, ticks):
    # Fields14/15 follow the parenthesized comm field; comm may contain spaces.
    fields = stat[stat.rfind(')') + 2:].split()
    return (int(fields[11]) + int(fields[12])) / ticks


def descendants(roots, rows):
    selected = set(roots) & set(rows)
    children = defaultdict(list)
    for pid, row in rows.items():
        children[row['ppid']].append(pid)
    queue = list(selected)
    while queue:
        for child in children[queue.pop()]:
            if child not in selected:
                selected.add(child)
                queue.append(child)
    return selected


def owned_pids(roots, previous, rows):
    retained = {pid for pid, old in previous.items()
                if pid in rows and rows[pid]['name'] == old['name']
                and rows[pid]['cpu_seconds'] >= old['cpu_seconds']
                and rows[pid].get('start_ticks') == old.get('start_ticks')}
    return descendants(set(roots) | retained, rows)


def comparable_samples(current, previous):
    return (current.get('name') == previous.get('name')
            and current.get('start_ticks') == previous.get('start_ticks')
            and current.get('cpu_source') == previous.get('cpu_source')
            and current['cpu_seconds'] >= previous['cpu_seconds'])


def process_role(pid, row, roots):
    if pid in roots:
        return 'root'
    if platform.system() == 'Linux':
        try:
            # Only the already-attributed task process is inspected, and only
            # Chromium's static process-type labels are retained, never URLs.
            parts = Path(f'/proc/{pid}/cmdline').read_bytes().decode(errors='replace').split('\0')
            types = [part.split('=', 1)[1] for part in parts if part.startswith('--type=')]
            subtype = [part.split('=', 1)[1] for part in parts if part.startswith('--utility-sub-type=')]
            if types:
                return ':'.join(types + subtype)
        except OSError:
            pass
    return row['name']


def validate_targets(targets):
    if not isinstance(targets, dict) or not isinstance(targets.get('groups'), list):
        raise ValueError('targets requires a groups list')
    names, pids = set(), set()
    for group in targets['groups']:
        name, roots = group.get('name'), group.get('pids')
        if not isinstance(name, str) or not name or name in names:
            raise ValueError('group names must be unique nonempty strings')
        if not isinstance(roots, list) or any(type(p) is not int or p < 1 for p in roots):
            raise ValueError('group pids must be a list of positive integers')
        if pids & set(roots):
            raise ValueError('a root PID cannot belong to more than one group')
        names.add(name)
        pids.update(roots)
    return targets


def percentile(values, quantile):
    if not values:
        return None
    return sorted(values)[max(0, math.ceil(quantile * len(values)) - 1)]


def summarize(records):
    buckets = defaultdict(lambda: defaultdict(list))
    errors = []
    times = []
    for record in records:
        if record.get('type') == 'error':
            errors.append(record)
        if record.get('type') != 'sample':
            continue
        times.append(record['elapsed_seconds'])
        for name, group in record['groups'].items():
            phase = record['phase']
            if record.get('launch') is not None:
                phase += ':launch-' + str(record['launch'])
            buckets[name][phase].append((record['elapsed_seconds'], group))
    groups = {}
    for name, phases in buckets.items():
        groups[name] = {}
        for phase, points in phases.items():
            observed = [(t, g) for t, g in points if g.get('observed')]
            cpu = [g['cpu_percent'] for _, g in observed if g['cpu_percent'] is not None and not g.get('phase_boundary')]
            rss = [g['rss_bytes'] / 1048576 for _, g in observed]
            base_phase = phase.split(':',1)[0]
            threshold = 3 if base_phase == 'idle' else 10 if base_phase == 'typing' else None
            enough = len(cpu) >= 10 and len(observed) == len(points)
            status = 'unverified'
            if enough and threshold is not None:
                status = 'within_threshold' if percentile(cpu, .95) <= threshold else 'above_threshold'
            elif enough:
                status = 'measured_no_threshold'
            groups[name][phase] = {
                'samples': len(points), 'observed_samples': len(observed),
                'duration_seconds': round(points[-1][0] - points[0][0], 3),
                'cpu_mean_percent': round(statistics.mean(cpu), 3) if cpu else None,
                'cpu_median_percent': round(statistics.median(cpu), 3) if cpu else None,
                'cpu_p95_percent': round(percentile(cpu, .95), 3) if cpu else None,
                'cpu_peak_percent': round(max(cpu), 3) if cpu else None,
                'samples_above_threshold': sum(x > threshold for x in cpu) if threshold is not None else None,
                'cpu_threshold_percent': threshold, 'cpu_status': status,
                'rss_start_mib': round(rss[0], 3) if rss else None,
                'rss_end_mib': round(rss[-1], 3) if rss else None,
                'rss_peak_mib': round(max(rss), 3) if rss else None,
                'rss_change_mib': round(rss[-1]-rss[0], 3) if rss else None,
                'phase_boundary_samples_excluded_from_cpu_summary': sum(bool(g.get('phase_boundary')) for _,g in observed),
                'first_seen_process_events': sum(len(g.get('new_pids', [])) for _,g in observed),
            }
    gaps = [round(b-a,3) for a,b in zip(times,times[1:]) if b-a > 1.5]
    return {'groups': groups, 'sampling_gaps_over_1_5_seconds': gaps, 'errors': errors,
            'notes': ['CPU is interval cumulative-process-time delta; 100% is one CPU core.',
                      'CPU used before the first observation of a new process is not in interval totals.',
                      'Summed RSS counts shared pages more than once and is not physical footprint.',
                      'Only explicit roots and observed descendants belong to a task; reparented descendants are retained.',
                      'A minimum of 10 observed samples is required for a phase verdict; longer windows are preferable.',
                      'A within_threshold verdict uses the 95th percentile; peaks and exceedance counts remain visible.',
                      'Unknown/missing samples do not count as passing.',
                      'Phase-transition intervals remain in raw samples but are excluded from CPU phase summaries.',
                      'Linux zombie/exited or unreadable processes are not reported as live RSS0 observations.']}


def collect(args):
    destination = Path(args.output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    stop = {'value': False}
    def interrupted(signum, frame):
        stop['value'] = True
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, interrupted)
    started = time.monotonic()
    previous = {}
    last_time = None
    last_phase = None
    baseline = {'type': 'metadata', 'utc': datetime.now(timezone.utc).isoformat(),
                'platform': platform.platform(), 'interval_seconds': 1,
                'cpu_source': 'proc stat jiffies' if platform.system() == 'Linux' else 'ps cumulative CPU time',
                'target_file': args.targets, 'max_seconds': args.seconds}
    with destination.open('x', buffering=1) as output:
        output.write(json.dumps(baseline) + '\n')
        next_sample = started
        while not stop['value'] and time.monotonic() - started < args.seconds:
            now = time.monotonic()
            try:
                targets = validate_targets(json.loads(Path(args.targets).read_text()))
                rows = process_snapshot()
                elapsed = now - last_time if last_time is not None else None
                groups = {}
                all_owned = set()
                for target in targets['groups']:
                    name = target['name']
                    prior = previous.get(name, {})
                    selected = owned_pids(set(target['pids']), prior, rows)
                    if selected & all_owned:
                        raise ValueError('task process trees overlap; roots must be independent')
                    all_owned.update(selected)
                    current = {pid: rows[pid] for pid in selected}
                    for pid, row in current.items():
                        old = prior.get(pid, {})
                        cached_role = old.get('role') if old.get('start_ticks') == row.get('start_ticks') and old.get('name') == row.get('name') else None
                        row['role'] = cached_role or process_role(pid, row, target['pids'])
                    new = [pid for pid in current if pid not in prior or not comparable_samples(current[pid], prior[pid])]
                    deltas = {pid: max(0, row['cpu_seconds'] - prior[pid]['cpu_seconds'])
                              for pid,row in current.items() if pid in prior
                              and comparable_samples(row, prior[pid])}
                    has_previous = bool(deltas)
                    cpu = sum(deltas.values())
                    groups[name] = {'pids': sorted(selected), 'roots': target['pids'],
                                    'observed': bool(selected),
                                    'phase_boundary': last_phase != targets.get('phase','unspecified'),
                                    'cpu_percent': round(cpu / elapsed * 100, 3) if elapsed and has_previous else None,
                                    'ps_cpu_percent': round(sum(row['ps_cpu_percent'] for row in current.values()),3),
                                    'rss_bytes': sum(row['rss_bytes'] for row in current.values()),
                                    'new_pids': new,
                                    'processes': [{'pid': pid, 'name': row['name'], 'role': row['role'],
                                                  'rss_bytes': row['rss_bytes'], 'cpu_seconds': row['cpu_seconds'],
                                                  'cpu_source': row['cpu_source'],
                                                  'cpu_percent': round(deltas[pid] / elapsed * 100, 3) if elapsed and pid in deltas else None}
                                                  for pid,row in current.items()]}
                    previous[name] = current
                record = {'type':'sample', 'utc':datetime.now(timezone.utc).isoformat(),
                          'elapsed_seconds':round(now-started,3), 'interval_seconds':elapsed,
                          'phase':targets.get('phase','unspecified'),
                          'iteration':targets.get('iteration'), 'launch':targets.get('launch'), 'groups':groups,
                          'host_ps_cpu_percent':round(sum(row['ps_cpu_percent'] for row in rows.values()),3),
                          'host_process_count':len(rows)}
                output.write(json.dumps(record,separators=(',',':'))+'\n')
                last_time = now
                last_phase = targets.get('phase','unspecified')
            except (OSError, ValueError, subprocess.SubprocessError) as exc:
                output.write(json.dumps({'type':'error','elapsed_seconds':round(now-started,3),'error':str(exc)})+'\n')
            next_sample += 1
            remaining = next_sample - time.monotonic()
            if remaining > 0:
                time.sleep(remaining)
            else:
                next_sample = time.monotonic()
        output.write(json.dumps({'type':'finished','elapsed_seconds':round(time.monotonic()-started,3)})+'\n')
    print(json.dumps({'output':str(destination),'finished':True}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='command', required=True)
    sample = subs.add_parser('collect')
    sample.add_argument('--targets', required=True)
    sample.add_argument('--output', required=True)
    sample.add_argument('--seconds', type=int, default=3600)
    summary = subs.add_parser('summarize')
    summary.add_argument('input')
    summary.add_argument('--output')
    args = parser.parse_args()
    if args.command == 'collect':
        if not 1 <= args.seconds <= 14400:
            parser.error('--seconds must be 1..14400')
        collect(args)
    else:
        records = [json.loads(line) for line in Path(args.input).read_text().splitlines() if line.strip()]
        result = json.dumps(summarize(records), indent=2)
        if args.output:
            Path(args.output).write_text(result+'\n')
            print(json.dumps({'output':args.output}))
        else:
            print(result)


if __name__ == '__main__':
    main()
