#!/usr/bin/env python3
"""Manifest-driven WPT runner for the rendered Lumen Mac app.

The manifest and server are upstream WPT. This runner does not emulate browser
APIs or testdriver actions. Upstream testharness and test262 wrappers are run;
other test classes stay explicitly unrun. Chromium
uses a fresh browser context per file; Lumen uses a fresh native process. Results
are resumable JSONL, with the test revision, binary digest and exact schedule.
"""
import argparse
from collections import Counter, deque
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import signal
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import urllib.request


REPORTER = Path(__file__).with_name('testharnessreport.js').read_text()
SUPPORTED_EXECUTION_TYPES = frozenset(('testharness', 'test262'))


def write_json(path, obj):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + '.tmp')
    temp.write_text(json.dumps(obj, indent=2, ensure_ascii=True) + '\n')
    temp.replace(path)


def digest(obj):
    return hashlib.sha256(json.dumps(obj, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def file_digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def binary_identity(path):
    try:
        stat = Path(path).stat()
        return {'device': stat.st_dev, 'inode': stat.st_ino, 'size': stat.st_size,
                'mtime_ns': stat.st_mtime_ns, 'ctime_ns': stat.st_ctime_ns}
    except OSError as exc:
        return {'error': type(exc).__name__ + ': ' + str(exc)}


def git_value(root, args):
    result = subprocess.run(['git', '-C', str(root), *args], capture_output=True, text=True, timeout=20)
    return result.stdout.strip() if result.returncode == 0 else None


def inventory_manifest(manifest):
    """Traverse the upstream v8/v9 manifest trie, preserving every URL variant."""
    rows = []
    counts = Counter()
    source_files = Counter()
    url_base = manifest.get('url_base', '/')
    if manifest.get('version') not in (8, 9):
        raise ValueError('Unsupported manifest format; inspect upstream before adapting: ' + str(manifest.get('version')))

    def visit(kind, node, parts):
        if isinstance(node, dict):
            for name, child in node.items():
                visit(kind, child, parts + [name])
            return
        if not isinstance(node, list) or not node:
            raise ValueError('Invalid manifest leaf at ' + '/'.join(parts))
        source = '/'.join(parts)
        source_files[kind] += 1
        if kind == 'support':
            return
        # support/conformancechecker leaves contain a hash but no runnable URLs.
        for record in node[1:]:
            if not isinstance(record, list) or not record:
                raise ValueError('Invalid manifest item at ' + source)
            rel_url = record[0] or source
            if not isinstance(rel_url, str):
                raise ValueError('Non-string URL at ' + source)
            # URLManifestItem.url joins the manifest base with its stored relative URL.
            url = url_base.rstrip('/') + '/' + rel_url.lstrip('/')
            metadata = record[-1] if isinstance(record[-1], dict) else {}
            refs = record[1] if kind in ('reftest', 'print-reftest') and len(record) > 2 else []
            rows.append({'test': url, 'type': kind, 'source': source,
                         'timeout': metadata.get('timeout', 'normal'),
                         'metadata': metadata, 'references': refs})
            counts[kind] += 1

    for kind, tree in manifest['items'].items():
        visit(kind, tree, [])
    rows.sort(key=lambda row: (row['type'], row['test']))
    keys = [(row['type'], row['test']) for row in rows]
    if len(keys) != len(set(keys)):
        raise ValueError('Duplicate manifest test URLs; refuse ambiguous results')
    return {'manifest_version': manifest['version'], 'url_base': url_base,
            'test_variants_by_type': dict(sorted(counts.items())),
            'source_files_by_type': dict(sorted(source_files.items())),
            'total_test_variants': len(rows), 'tests': rows}


def iter_jsonl(path, repair=False):
    """Read one test result at a time; memory is bounded by one result line.

    An interrupted final line may be repaired only when explicitly requested.
    Invalid interior lines are never skipped or silently counted as missing.
    """
    path = Path(path)
    if not path.exists():
        return
    index, last_line = 0, b''
    with path.open('rb') as source:
        while True:
            offset = source.tell()
            line = source.readline()
            if not line:
                break
            index += 1
            try:
                row = json.loads(line)
            except (ValueError, UnicodeDecodeError):
                if repair and not line.endswith(b'\n') and not source.read(1):
                    path.with_suffix('.interrupted-tail').write_bytes(line)
                    with path.open('r+b') as output:
                        output.truncate(offset)
                    break
                raise ValueError('Invalid result JSONL line ' + str(index))
            last_line = line
            yield row
    if repair and last_line and not last_line.endswith(b'\n'):
        with path.open('ab') as f:
            f.write(b'\n')


def read_jsonl(path, repair=False):
    """Convenience reader for small diagnostic inputs; runners use iter_jsonl."""
    return list(iter_jsonl(path, repair=repair))


def row_passes(row):
    subs = row.get('subtests', [])
    if row.get('type') in SUPPORTED_EXECUTION_TYPES and not subs:
        return False
    return row.get('status') in ('OK', 'PASS') and all(s.get('status') == 'PASS' for s in subs)


class OutcomeStats:
    """Bounded counters, shared by the global and per-class summaries."""
    def __init__(self, scheduled):
        self.scheduled, self.recorded = scheduled, 0
        self.outcomes, self.subs, self.zero = Counter(), Counter(), Counter()
        self.passed = self.completed = self.attempted = 0

    def add(self, row):
        self.recorded += 1
        self.outcomes[row['status']] += 1
        self.subs.update(s['status'] for s in row.get('subtests', []))
        self.passed += row_passes(row)
        self.completed += row['status'] in ('OK', 'PASS', 'FAIL', 'ERROR', 'PRECONDITION_FAILED')
        self.attempted += row['status'] != 'NOTRUN'
        if row['type'] in SUPPORTED_EXECUTION_TYPES and not row.get('subtests'):
            self.zero[row['type']] += 1

    def value(self):
        asserted = sum(v for k, v in self.subs.items() if k != 'NOTRUN')
        return {'scheduled_files': self.scheduled, 'recorded_files': self.recorded,
                'remaining_files': self.scheduled - self.recorded,
                'file_outcomes': dict(sorted(self.outcomes.items())),
                'files_passing_all_assertions': self.passed, 'valid_completed_files': self.completed,
                'attempted_files': self.attempted,
                'attempted_file_pass_rate': self.passed / self.attempted if self.attempted else None,
                'zero_assertion_testharness_files': self.zero['testharness'],
                'zero_assertion_files_by_type': dict(sorted(self.zero.items())),
                'all_assertions_pass_file_rate': self.passed / self.completed if self.completed else None,
                'completed_only_metric_note': 'all_assertions_pass_file_rate excludes timeouts/crashes/infrastructure errors; use attempted_file_pass_rate as headline.',
                'scheduled_file_pass_fraction': self.passed / self.scheduled if self.scheduled else None,
                'subtest_outcomes': dict(sorted(self.subs.items())),
                'executed_subtests_excluding_notrun': asserted,
                'subtest_pass_rate': self.subs['PASS'] / asserted if asserted else None,
                'note': 'Timeouts, crashes, infrastructure errors and unrun files are explicit; they never count as passes.'}


class Summary(OutcomeStats):
    """Incremental accounting: checkpoint cost must not grow with suite size."""
    def __init__(self, schedule):
        self.keys = {(r['type'], r['test']) for r in schedule}
        if len(self.keys) != len(schedule):
            raise ValueError('Duplicate schedule entries')
        super().__init__(len(schedule))
        self.seen = set()
        self.by_type = {kind: OutcomeStats(count) for kind, count in Counter(r['type'] for r in schedule).items()}

    def add(self, row):
        key = (row['type'], row['test'])
        if key not in self.keys or key in self.seen:
            raise ValueError('Unexpected or duplicate result key: ' + str(key))
        self.seen.add(key)
        super().add(row)
        self.by_type[row['type']].add(row)

    def value(self):
        value = super().value()
        value['by_type'] = {kind: stats.value() for kind, stats in sorted(self.by_type.items())}
        value['headline_metric_note'] = 'Report testharness and test262 separately; the combined counters are coverage bookkeeping, not a web-platform pass-rate headline.'
        return value


def summarize(rows, schedule):
    summary = Summary(schedule)
    for row in rows:
        summary.add(row)
    return summary.value()


def restore_accounting(path, schedule):
    """Only a contiguous prefix can be appended without reordering results."""
    accounting = Summary(schedule)
    for index, row in enumerate(iter_jsonl(path, repair=True)):
        if index >= len(schedule) or (row['type'], row['test']) != (schedule[index]['type'], schedule[index]['test']):
            raise ValueError('Existing results are not a contiguous schedule prefix; refuse mixed resume')
        accounting.add(row)
    return accounting


def verified_schedule(directory, signature):
    schedule = json.loads((Path(directory) / 'schedule.json').read_text())
    if digest(schedule) != signature['schedule_sha256']:
        raise ValueError('Stored schedule differs from the signed schedule: ' + str(directory))
    return schedule


class Parity:
    """Accumulate paired assertions without retaining previous test files."""
    def __init__(self, split_types=True):
        self.counts, self.failures = Counter(), []
        self.by_type = {} if split_types else None

    def add(self, key, lr, cr):
        if self.by_type is not None:
            if key[0] not in self.by_type:
                self.by_type[key[0]] = Parity(split_types=False)
            group = self.by_type[key[0]]
            previous, previous_gaps = dict(group.counts), len(group.failures)
            group.add(key, lr, cr)
            self.counts.update({name: count - previous.get(name, 0) for name, count in group.counts.items()})
            self.failures.extend(group.failures[previous_gaps:previous_gaps + max(0, 200 - len(self.failures))])
            return
        counts, failures = self.counts, self.failures
        if cr is None:
            counts['chromium_unrun_files'] += 1
            return
        if cr.get('status') not in ('OK', 'PASS', 'FAIL', 'ERROR', 'TIMEOUT', 'PRECONDITION_FAILED'):
            counts['chromium_incomplete_files'] += 1
        if lr is None:
            counts['lumen_unrun_files'] += 1
        # Occurrence number preserves names even when an invalid test duplicates them.
        seen = Counter()
        lsub = {}
        for sub in (lr or {}).get('subtests', []):
            occurrence = seen[sub['name']]
            seen[sub['name']] += 1
            lsub[(sub['name'], occurrence)] = sub['status']
        seen = Counter()
        entries = cr.get('subtests', [])
        if not entries and cr.get('status') == 'PASS' and key[0] not in SUPPORTED_EXECUTION_TYPES:
            entries = [{'name': '(file assertion)', 'status': 'PASS'}]
            if lr and lr.get('status') == 'PASS':
                lsub[('(file assertion)', 0)] = 'PASS'
        for sub in entries:
            occurrence = seen[sub['name']]
            seen[sub['name']] += 1
            if sub['status'] != 'PASS':
                continue
            counts['chromium_passing_assertions'] += 1
            status = lsub.get((sub['name'], occurrence), 'MISSING')
            if status == 'PASS':
                counts['also_passed_by_lumen'] += 1
            else:
                counts['lumen_' + status.lower()] += 1
                if len(failures) < 200:
                    failures.append({'type': key[0], 'test': key[1], 'subtest': sub['name'], 'lumen_status': status})
    def value(self):
        counts = self.counts
        denominator = counts['chromium_passing_assertions']
        value = {'counts': dict(counts), 'chromium_pass_retention': counts['also_passed_by_lumen'] / denominator if denominator else None,
                'first_200_gaps': self.failures,
                'note': 'Missing Lumen assertions count against parity; Chromium-unrun files remain explicit and cannot establish full-suite parity.'}
        if self.by_type is not None:
            value['by_type'] = {kind: group.value() for kind, group in sorted(self.by_type.items())}
            value['headline_metric_note'] = 'Use per-type Chromium-pass retention; combining test262 with testharness can hide web-platform gaps.'
        return value


def parity(lumen_rows, chrome_rows, schedule):
    lumen = {(r['type'], r['test']): r for r in lumen_rows}
    chrome = {(r['type'], r['test']): r for r in chrome_rows}
    result = Parity()
    for row in schedule:
        key = (row['type'], row['test'])
        result.add(key, lumen.get(key), chrome.get(key))
    return result.value()


def compare_streams(lumen_path, chrome_path, schedule):
    """Merge the runner's ordered JSONL streams, retaining at most one file each."""
    positions = {(row['type'], row['test']): index for index, row in enumerate(schedule)}
    lumen_summary, chrome_summary, result = Summary(schedule), Summary(schedule), Parity()

    def ordered_rows(path, summary):
        previous = -1
        for row in iter_jsonl(path):
            key = (row['type'], row['test'])
            if key not in positions:
                raise ValueError('Unscheduled result in comparison: ' + str(key))
            index = positions[key]
            if index <= previous:
                raise ValueError('Result stream is duplicated or not in schedule order: ' + str(path))
            previous = index
            summary.add(row)
            yield index, row

    lumen_iter = iter(ordered_rows(lumen_path, lumen_summary))
    chrome_iter = iter(ordered_rows(chrome_path, chrome_summary))
    lumen_next, chrome_next = next(lumen_iter, None), next(chrome_iter, None)
    for index, row in enumerate(schedule):
        lumen = lumen_next[1] if lumen_next is not None and lumen_next[0] == index else None
        chrome = chrome_next[1] if chrome_next is not None and chrome_next[0] == index else None
        result.add((row['type'], row['test']), lumen, chrome)
        if lumen is not None:
            lumen_next = next(lumen_iter, None)
        if chrome is not None:
            chrome_next = next(chrome_iter, None)
    if lumen_next is not None or chrome_next is not None:
        raise ValueError('Comparison contains results beyond the schedule')
    return lumen_summary.value(), chrome_summary.value(), result.value()


# Drop-in definitions for tests/wpt/full_runner.py. Keep existing imports.
# Call terminate_owned(process, sampler) and preserve its returned cleanup result.

_PROCESS_MONITOR = None


def monitor_module():
    global _PROCESS_MONITOR
    if _PROCESS_MONITOR is None:
        mod_path = Path(__file__).resolve().parents[2] / 'tools' / 'monitor_process.py'
        spec = importlib.util.spec_from_file_location('lumen_process_monitor', mod_path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        _PROCESS_MONITOR = module
    return _PROCESS_MONITOR


class SampleTotals:
    """Exact running counts, sums and extrema; quantiles need the raw samples."""
    def __init__(self):
        self.count = self.intervals = 0
        self.cpu_sum = self.interval_sum = 0.0
        self.cpu_peak = self.interval_peak = None
        self.rss_peak = 0
        self.first = self.last = None

    def add(self, row):
        if self.first is None:
            self.first = row
        self.last = row
        self.count += 1
        self.cpu_sum += row['cpu_percent']
        self.cpu_peak = max(self.cpu_peak, row['cpu_percent']) if self.cpu_peak is not None else row['cpu_percent']
        self.rss_peak = max(self.rss_peak, row['rss_kb'])
        interval = row.get('interval_cpu_percent')
        if interval is not None:
            self.intervals += 1
            self.interval_sum += interval
            self.interval_peak = max(self.interval_peak, interval) if self.interval_peak is not None else interval

    def extend_summary(self, recent, retained):
        value = dict(recent)
        value['in_memory_sample_limit'] = 600
        value['raw_samples_path_note'] = 'The complete one-second stream is retained in the metrics JSONL file.'
        if self.count <= retained:
            value['quantile_scope'] = 'all recorded samples'
            return value
        # Full-run medians/p95 are intentionally unavailable once the bounded
        # window rolls over. Never relabel recent-window quantiles as full-run.
        value['recent_window_summary'] = dict(recent)
        value['quantile_scope'] = 'full-run quantiles unavailable; recent_window_summary covers the retained last 600 samples'
        for key in ('steady_cpu_median_percent', 'steady_cpu_p95_percent',
                    'steady_interval_cpu_median_percent', 'steady_interval_cpu_p95_percent',
                    'rss_steady_median_mb', 'rss_steady_p95_mb'):
            value[key] = None
        cpu_mean = round(self.cpu_sum / self.count, 3) if self.count else None
        interval_mean = round(self.interval_sum / self.intervals, 3) if self.intervals else None
        value.update(samples=self.count, steady_samples=self.count,
                     interval_cpu_samples=self.intervals, steady_interval_cpu_samples=self.intervals,
                     cpu_mean_percent=cpu_mean, steady_cpu_mean_percent=cpu_mean,
                     cpu_peak_percent=self.cpu_peak,
                     interval_cpu_mean_percent=interval_mean, steady_interval_cpu_mean_percent=interval_mean,
                     interval_cpu_peak_percent=self.interval_peak,
                     rss_peak_mb=round(self.rss_peak / 1024, 3),
                     rss_final_mb=round(self.last['rss_kb'] / 1024, 3) if self.last else None,
                     rss_steady_growth_mb=round((self.last['rss_kb'] - self.first['rss_kb']) / 1024, 3) if self.first else None)
        return value


class Sampler:
    """Record one-second process-tree samples using the shared monitor's math."""
    def __init__(self, pid, path):
        self.pid, self.path = pid, Path(path)
        self.rows, self.errors = deque(maxlen=600), []
        self.totals = SampleTotals()
        self.stopped = threading.Event()
        self.mod = monitor_module()
        namespace_error = self.mod.pid_namespace_error()
        if namespace_error:
            raise RuntimeError(namespace_error)
        self.root_identity = None
        self.root_cpu_identity = None
        self.observed_identities = {}
        self.previous_snapshot, self.previous_at = None, None
        self.stop_reason = None
        self.final_summary = None
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.thread.start()

    def _record_snapshot(self, raw, elapsed):
        current = self.mod.precise_linux_cpu_times(self.mod.process_snapshot(raw, self.pid))
        if not current:
            self.stop_reason = 'process_exited'
            return None
        identity = current[self.pid]['start_identity']
        if identity is None:
            self.errors.append('ps omitted root process start identity')
            self.stop_reason = 'identity_unavailable'
            return None
        if self.root_identity is not None and identity != self.root_identity:
            self.errors.append('root PID was reused; replacement process was not sampled')
            self.stop_reason = 'root_identity_changed'
            return None
        cpu_identity = current[self.pid].get('cpu_start_identity')
        if self.root_cpu_identity is not None and cpu_identity is not None and cpu_identity != self.root_cpu_identity:
            self.errors.append('root PID was reused within the ps start-time resolution; replacement process was not sampled')
            self.stop_reason = 'root_identity_changed'
            return None
        if cpu_identity is not None:
            self.root_cpu_identity = cpu_identity
        self.root_identity = identity
        row = {'elapsed_s': round(elapsed, 3),
               'cpu_percent': round(sum(p['cpu_percent'] for p in current.values()), 3),
               'interval_cpu_percent': self.mod.interval_cpu_percent(
                   self.previous_snapshot, current,
                   elapsed - self.previous_at if self.previous_at is not None else 0),
               'rss_kb': sum(p['rss_kb'] for p in current.values()),
               'processes': len(current)}
        self.previous_snapshot, self.previous_at = current, elapsed
        self.observed_identities.update({pid: p['start_identity'] for pid, p in current.items()
                                         if p['start_identity'] is not None})
        self.rows.append(row)
        self.totals.add(row)
        return row

    def _loop(self):
        began = time.monotonic()
        deadline = began
        try:
            with self.path.open('w') as output:
                while not self.stopped.is_set():
                    raw = subprocess.check_output(['ps', '-A', '-o',
                        'pid=,ppid=,%cpu=,rss=,stat=,time=,lstart='], text=True, timeout=4)
                    row = self._record_snapshot(raw, time.monotonic() - began)
                    if row is None:
                        break
                    output.write(json.dumps(row) + '\n')
                    output.flush()
                    deadline += 1.0
                    self.stopped.wait(max(0, deadline - time.monotonic()))
        except Exception as exc:
            self.errors.append(type(exc).__name__ + ': ' + str(exc))
            self.stop_reason = 'collector_error'
        finally:
            if self.stop_reason is None:
                self.stop_reason = 'requested_stop' if self.stopped.is_set() else 'sampler_finished'

    def stop(self):
        if self.final_summary is not None:
            return self.final_summary
        self.stopped.set()
        self.thread.join(timeout=5)
        if self.thread.is_alive():
            self.errors.append('resource sampler did not stop within its bounded window')
            self.stop_reason = 'sampler_thread_did_not_stop'
        value = self.totals.extend_summary(self.mod.summarize(list(self.rows), 0), len(self.rows))
        intervals = self.totals.intervals
        value.update(errors=list(self.errors), sample_interval_s=1,
                     stop_reason=self.stop_reason,
                     telemetry_status='failed' if self.errors else 'captured' if intervals else 'partial' if self.rows else 'unavailable',
                     interval_cpu_coverage='available' if intervals else 'unavailable',
                     observed_span_s=round(self.totals.last['elapsed_s'] - self.totals.first['elapsed_s'], 3) if self.totals.count > 1 else 0,
                     root_start_identity=self.root_identity,
                     root_cpu_start_identity=getattr(self, 'root_cpu_identity', None),
                     cpu_metric='cpu_percent: ps rolling estimate; interval_cpu_percent: cumulative CPU-time deltas',
                     cpu_time_source='Linux /proc/<pid>/stat user and system clock ticks' if sys.platform.startswith('linux') else 'ps time (fractional seconds on macOS)',
                     cpu_time_resolution_s=1 / os.sysconf('SC_CLK_TCK') if sys.platform.startswith('linux') else None,
                     cpu_unit='100%=one logical CPU; multithreaded work may exceed100%',
                     memory_unit='MiB (legacy field names retain mb)',
                     memory_metric='maximum sampled process-tree RSS, not a continuously observed peak; shared pages can be counted more than once',
                     short_run_limit='A subsecond case may only capture process startup; its single RSS sample cannot establish browser memory use or peak RSS.',
                     sampling_limit='one-second ps start identity; processes born and exited between samples are unobserved')
        self.final_summary = value
        return value


def process_group_diagnostics(group):
    """Bounded read-only detail for the owned group; never read arguments/env."""
    result = {'observed_unix_ms': round(time.time() * 1000), 'members': []}
    try:
        raw = subprocess.check_output(['ps', '-A', '-ww', '-o',
            'pid=,ppid=,pgid=,uid=,ruid=,stat=,lstart=,comm='], text=True, timeout=5)
        for line in raw.splitlines():
            fields = line.split(None, 11)
            if len(fields) != 12 or not fields[2].isdigit() or int(fields[2]) != group:
                continue
            result['members'].append({'pid': int(fields[0]), 'ppid': int(fields[1]),
                'pgid': int(fields[2]), 'uid': int(fields[3]), 'ruid': int(fields[4]),
                'stat': fields[5], 'start_identity': ' '.join(fields[6:11]), 'executable': fields[11]})
    except Exception as exc:
        result['diagnostic_error'] = type(exc).__name__ + ': ' + str(exc)
    return result


def terminate_owned(process, sampler=None, verification_wait_s=1):
    """Return cleanup evidence; cleanup failure must not replace test outcomes."""
    if sampler is not None and getattr(sampler, 'stop_reason', None) == 'root_identity_changed':
        return {'sent_sigterm': False, 'sent_sigkill': False,
                'error': 'root PID identity changed; refusing process-group cleanup',
                'process_group_gone': None}
    result = None
    try:
        module = sampler.mod if sampler is not None else monitor_module()
        identities = dict(sampler.observed_identities) if sampler is not None else {}
        result = module.cleanup_owned_group(process, identities)
        # SIGKILL delivery alone is not evidence that all owned helpers exited.
        began = time.monotonic()
        deadline = began + verification_wait_s
        remaining = module.owned_group_members(process.pid)
        verification = {'allowed_wait_s': verification_wait_s,
                        'initial_live_members': dict(remaining)}
        result['post_signal_verification'] = verification
        if remaining:
            verification['initial_process_state'] = process_group_diagnostics(process.pid)
        while remaining and time.monotonic() < deadline:
            time.sleep(0.05)
            process.poll()
            remaining = module.owned_group_members(process.pid)
        verification['duration_ms'] = round((time.monotonic() - began) * 1000)
        if remaining:
            verification['final_process_state'] = process_group_diagnostics(process.pid)
        result['process_group_gone'] = not remaining
        result['remaining_group_members'] = remaining
        result['root_exit_code'] = process.poll()
        if remaining:
            result['error'] = result.get('error') or 'Owned process group remains after cleanup'
        return result
    except Exception as exc:
        detail = type(exc).__name__ + ': ' + str(exc)
        if result is None:
            # The signal helper did not return evidence. Do not invent either
            # successful or absent signal delivery after an unexpected error.
            result = {'sent_sigterm': None, 'sent_sigkill': None,
                      'cleanup_evidence_returned': False, 'error': detail}
        else:
            result['verification_error'] = detail
            result['error'] = result.get('error') or detail
        result.update(process_group_gone=False, verification_status='failed')
        return result


class Collector:
    def __init__(self):
        self.subtests, self.done = [], None
        self.parse_errors = []
        self.ready, self.chunks = None, {}
        self.completion_callbacks = 0

    def line(self, line):
        # Marker text inside a JSON name/message is data, not another record.
        matches = [(pos, kind) for kind in ('SUB', 'DONE', 'CHUNK', 'READY', 'REPEAT')
                   if (pos := line.find('LUME_WPT_' + kind + ' ')) >= 0]
        if not matches:
            return
        pos, kind = min(matches)
        try:
            obj = json.loads(line[pos + len('LUME_WPT_' + kind + ' '):])
            if kind == 'CHUNK':
                if not isinstance(obj, dict) or obj.get('kind') not in ('SUB', 'DONE', 'REPEAT') or not isinstance(obj.get('id'), int) or not isinstance(obj.get('index'), int) or not isinstance(obj.get('total'), int) or not 1 <= obj['total'] <= 50000 or not isinstance(obj.get('data'), str):
                    raise ValueError('Invalid reporter chunk')
                packet_id = obj['id']
                group = self.chunks.setdefault(packet_id, {'kind':obj['kind'], 'total':obj['total'], 'parts':[]})
                if group['kind'] != obj['kind'] or group['total'] != obj['total'] or obj['index'] != len(group['parts']):
                    raise ValueError('Missing, duplicated, or out-of-order reporter chunk')
                group['parts'].append(obj['data'])
                if len(group['parts']) < group['total']:
                    return
                kind, obj = group['kind'], json.loads(''.join(group['parts']))
                del self.chunks[packet_id]
            self._record(kind, obj)
        except (ValueError, TypeError, KeyError, StopIteration):
            if len(self.parse_errors) < 10:
                self.parse_errors.append(line[:200])

    def _record(self, kind, obj):
        if not isinstance(obj, dict):
            raise ValueError('Reporter record must be an object')
        if kind == 'SUB':
            if not isinstance(obj.get('name'), str) or obj.get('status') not in ('PASS', 'FAIL', 'TIMEOUT', 'NOTRUN', 'PRECONDITION_FAILED', 'UNKNOWN'):
                raise ValueError('Invalid subtest record')
            self.subtests.append(obj)
        elif kind == 'DONE':
            if self.done is not None or obj.get('status') not in ('OK', 'ERROR', 'TIMEOUT', 'PRECONDITION_FAILED', 'UNKNOWN') or not isinstance(obj.get('subtest_count'), int):
                raise ValueError('Invalid or duplicate completion record')
            self.done = obj
            self.completion_callbacks = 1
        elif kind == 'REPEAT':
            if (self.done is None or obj.get('status') != 'IDENTICAL'
                    or type(obj.get('completion_callback_count')) is not int
                    or obj['completion_callback_count'] != self.completion_callbacks + 1
                    or obj.get('completion') != self.done
                    or self.done.get('subtest_count') != len(self.subtests)):
                raise ValueError('Conflicting or out-of-order repeated completion')
            self.completion_callbacks += 1
        elif kind == 'READY':
            if obj.get('status') not in ('ATTACHED', 'ERROR'):
                raise ValueError('Invalid reporter attachment record')
            self.ready = obj

    def result(self, fallback):
        result = dict(self.done or fallback)
        result['subtests'] = self.subtests
        result['reporter_state'] = self.ready
        if self.completion_callbacks > 1:
            result['reporter_diagnostics'] = {
                'completion_callbacks': self.completion_callbacks,
                'identical_completion_repeats': self.completion_callbacks - 1,
                'note': 'Repeated unchanged harness completion callbacks were observed; the original functional outcomes are retained once.'}
        if self.parse_errors or self.chunks or (self.ready and self.ready.get('status') == 'ERROR') or (self.done and self.done.get('subtest_count') != len(self.subtests)):
            result.update(status='INFRA_ERROR', message='Incomplete or malformed reporter output', parse_errors=self.parse_errors[:5])
        return result


def run_lumen(binary, url, timeout, prefix, reporter):
    collector = Collector()
    module = monitor_module()  # Resolve telemetry/cleanup dependencies before launching.
    namespace_error = module.pid_namespace_error()
    if namespace_error:
        raise RuntimeError(namespace_error)
    env = os.environ.copy()
    # Inherited screenshot/no-JS switches would invalidate this run.
    for key in ('LUMEN_SHOT', 'LUMEN_SHOT_MS', 'LUMEN_NO_JS', 'LUMEN_PRE_FILE'):
        env.pop(key, None)
    profile = Path(tempfile.mkdtemp(prefix='lumen-case-profile-', dir=prefix.parent))
    download = profile / 'downloads'
    download.mkdir()
    env.update(LUMEN_CONSOLE='1',
               LUMEN_PROFILE_DIR=str(profile.resolve()), LUMEN_DOWNLOAD_DIR=str(download.resolve()))
    began = time.monotonic()
    with prefix.with_suffix('.console.log').open('wb') as log:
        try:
            process = subprocess.Popen([str(binary), url], stdout=log, stderr=subprocess.STDOUT,
                                       env=env, start_new_session=True)
        except Exception:
            shutil.rmtree(profile)
            raise
        sampler = None
        try:
            sampler = Sampler(process.pid, prefix.with_suffix('.metrics.jsonl'))
            with prefix.with_suffix('.console.log').open(errors='replace') as reader:
                carry = ''
                while time.monotonic() - began < timeout:
                    carry += reader.read()
                    lines = carry.split('\n')
                    carry = lines.pop()
                    for line in lines:
                        collector.line(line)
                    if collector.done:
                        break
                    if process.poll() is not None:
                        collector.line(carry)
                        break
                    time.sleep(0.025)
            fallback = {'status': 'CRASH' if process.poll() not in (None, 0) else 'ERROR' if process.poll() == 0 else 'TIMEOUT',
                        'message': 'Native app exited before harness completion' if process.poll() is not None else 'External file watchdog expired'}
            result = collector.result(fallback)
            result['natural_exit_code'] = process.poll()
            result['pid'] = process.pid
        except Exception as exc:
            result = collector.result({'status': 'INFRA_ERROR', 'message': type(exc).__name__ + ': ' + str(exc)})
        finally:
            try:
                result_metrics = sampler.stop() if sampler else {'errors': ['sampler initialization failed']}
            except Exception as exc:
                result_metrics = {'errors': [type(exc).__name__ + ': ' + str(exc)]}
            cleanup = terminate_owned(process, sampler)
            if not cleanup.get('error'):
                try:
                    shutil.rmtree(profile)
                except OSError as exc:
                    cleanup['profile_error'] = str(exc)
            else:
                cleanup['retained_profile'] = str(profile)
    result.update(duration_ms=round((time.monotonic() - began) * 1000), metrics=result_metrics, cleanup=cleanup)
    return result


class WebSocket:
    """Small stdlib-only RFC6455 client for localhost Chrome DevTools."""
    def __init__(self, url):
        from base64 import b64encode
        parsed = urllib.parse.urlsplit(url)
        if parsed.scheme != 'ws' or parsed.hostname not in ('127.0.0.1', 'localhost', '::1'):
            raise ValueError('Only loopback DevTools sockets are allowed')
        self.sock = socket.create_connection((parsed.hostname, parsed.port), timeout=10)
        key = b64encode(os.urandom(16)).decode()
        path = parsed.path + ('?' + parsed.query if parsed.query else '')
        request = ('GET ' + path + ' HTTP/1.1\r\nHost: ' + parsed.netloc + '\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ' + key + '\r\nSec-WebSocket-Version: 13\r\n\r\n')
        self.sock.sendall(request.encode())
        header = b''
        while not header.endswith(b'\r\n\r\n'):
            chunk = self.sock.recv(1)
            if not chunk:
                raise EOFError('DevTools closed during websocket upgrade')
            header += chunk
            if len(header) > 65536:
                raise ValueError('Invalid websocket upgrade')
        expected = b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).lower()
        if b' 101 ' not in header.split(b'\r\n')[0] or expected not in header.lower():
            raise ValueError('DevTools websocket upgrade failed')

    def _read(self, count):
        data = b''
        while len(data) < count:
            part = self.sock.recv(count - len(data))
            if not part:
                raise EOFError('DevTools disconnected')
            data += part
        return data

    def send(self, obj):
        self._send_frame(1, json.dumps(obj).encode())

    def _send_frame(self, opcode, data):
        mask = os.urandom(4)
        size = len(data)
        header = bytes((0x80 | opcode, 0x80 | size)) if size < 126 else bytes((0x80 | opcode, 0xFE)) + struct.pack('!H', size) if size < 65536 else bytes((0x80 | opcode, 0xFF)) + struct.pack('!Q', size)
        self.sock.sendall(header + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def recv(self, timeout):
        self.sock.settimeout(timeout)
        chunks = []
        while True:
            first, second = self._read(2)
            opcode, size = first & 15, second & 127
            if size == 126:
                size = struct.unpack('!H', self._read(2))[0]
            elif size == 127:
                size = struct.unpack('!Q', self._read(8))[0]
            if size > 64 * 1024 * 1024:
                raise ValueError('Oversized DevTools frame')
            mask = self._read(4) if second & 128 else None
            data = self._read(size)
            if mask:
                data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
            if opcode == 8:
                raise EOFError('DevTools closed websocket')
            if opcode == 9:
                self._send_frame(10, data)
                continue
            if opcode == 10:
                continue
            chunks.append(data)
            if first & 128:
                return json.loads(b''.join(chunks))


class CDP:
    def __init__(self, url):
        self.ws, self.serial, self.events = WebSocket(url), 0, deque()

    def call(self, method, params=None, timeout=15):
        self.serial += 1
        serial = self.serial
        self.ws.send({'id': serial, 'method': method, 'params': params or {}})
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            obj = self.ws.recv(max(0.01, until - time.monotonic()))
            if obj.get('id') == serial:
                if 'error' in obj:
                    raise RuntimeError(method + ': ' + str(obj['error']))
                return obj.get('result', {})
            self.events.append(obj)
        raise TimeoutError(method)

    def event(self, timeout):
        return self.events.popleft() if self.events else self.ws.recv(timeout)

    def close(self):
        self.ws.sock.close()


def chromium_launch_arguments(binary, profile, extra_args, headless=True):
    """Keep browser mode explicit; otherwise preserve the isolated test profile."""
    if type(headless) is not bool:
        raise ValueError('Chromium headless mode must be a boolean')
    if any(value in ('--headless', '-headless') or
           value.startswith(('--headless=', '-headless=')) for value in extra_args):
        raise ValueError('Select Chromium mode explicitly; do not override it through extra arguments')
    return [str(binary), *(['--headless=new'] if headless else []),
            '--remote-debugging-port=0', '--user-data-dir=' + str(profile),
            '--no-first-run', '--no-default-browser-check', '--disable-background-networking',
            *extra_args, 'about:blank']


class Chromium:
    def __init__(self, binary, directory, extra_args, *, headless=True):
        module = monitor_module()
        namespace_error = module.pid_namespace_error()
        if namespace_error:
            raise RuntimeError(namespace_error)
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.metrics_reference = str(self.directory / 'chromium.metrics.jsonl')
        self.profile = Path(tempfile.mkdtemp(prefix='lumen-wpt-chrome-', dir=self.directory))
        self.log = (self.directory / 'chromium-process.log').open('wb')
        try:
            args = chromium_launch_arguments(binary, self.profile, extra_args, headless=headless)
            self.process = subprocess.Popen(args, stdout=self.log, stderr=subprocess.STDOUT, start_new_session=True)
            self.sampler = Sampler(self.process.pid, self.directory / 'chromium.metrics.jsonl')
            active = self.profile / 'DevToolsActivePort'
            until = time.monotonic() + 30
            while not active.exists() and self.process.poll() is None and time.monotonic() < until:
                time.sleep(0.05)
            if not active.exists():
                raise RuntimeError('Chromium did not expose a DevTools endpoint; see chromium-process.log')
            lines = active.read_text().splitlines()
            self.port = int(lines[0])
            self.browser = CDP('ws://127.0.0.1:' + str(self.port) + lines[1])
            self.version = self.browser.call('Browser.getVersion')
        except Exception as exc:
            try:
                self.close()
            except Exception as cleanup_exc:
                exc.add_note('Startup cleanup could not finish: ' + str(cleanup_exc))
            raise

    def close(self):
        errors, cleanup = [], None
        graceful = {'method': 'Browser.close', 'attempted': False, 'acknowledged': False}
        if hasattr(self, 'browser'):
            graceful['attempted'] = True
            try:
                # The endpoint belongs to this fresh-profile Popen instance.
                # Request its normal shutdown before disconnecting DevTools or
                # signalling processes; sandbox helpers can outlive a SIGTERM.
                self.browser.call('Browser.close', timeout=5)
                graceful['acknowledged'] = True
            except Exception as exc:
                # A browser can close its socket before sending the reply.
                # Preserve this evidence; actual group exit decides cleanup.
                graceful['request_error'] = type(exc).__name__ + ': ' + str(exc)
            try:
                self.browser.close()
            except Exception as exc:
                errors.append('DevTools close: ' + str(exc))
        if graceful['attempted'] and hasattr(self, 'process'):
            began, deadline = time.monotonic(), time.monotonic() + 20
            graceful['allowed_drain_s'] = 20
            graceful['exit_requirement'] = 'All live owned-group members must disappear; an exiting process state is not ignored.'
            try:
                module = self.sampler.mod if hasattr(self, 'sampler') else monitor_module()
                initial_state_recorded = False
                while True:
                    members = module.owned_group_members(self.process.pid)
                    self.process.poll()
                    if not members:
                        graceful['process_group_gone'] = True
                        break
                    if not initial_state_recorded:
                        graceful['initial_process_state'] = process_group_diagnostics(self.process.pid)
                        initial_state_recorded = True
                    if time.monotonic() >= deadline:
                        graceful['process_group_gone'] = False
                        graceful['remaining_group_members'] = members
                        graceful['final_process_state'] = process_group_diagnostics(self.process.pid)
                        break
                    time.sleep(0.2)
            except Exception as exc:
                graceful['verification_error'] = type(exc).__name__ + ': ' + str(exc)
            graceful['drain_duration_ms'] = round((time.monotonic() - began) * 1000)
        if hasattr(self, 'sampler'):
            try:
                metrics = self.sampler.stop()
                metrics['session_scope'] = 'Owned Chromium process tree while the root exists, including startup, workload and graceful teardown. Helper-only drain after root exit is not sampled; not an idle baseline.'
                write_json(self.directory / 'chromium.metrics-summary.json', metrics)
            except Exception as exc:
                errors.append('Telemetry finalization: ' + str(exc))
        if hasattr(self, 'process'):
            root_before_cleanup, root_observation_verified = None, False
            try:
                root_before_cleanup = self.process.poll()
                root_observation_verified = True
            except Exception as exc:
                errors.append('Root exit observation: ' + type(exc).__name__ + ': ' + str(exc))
            cleanup = terminate_owned(self.process, getattr(self, 'sampler', None), verification_wait_s=10)
            cleanup['root_exit_before_signal_cleanup'] = root_before_cleanup
            cleanup['root_exit_observation_verified'] = root_observation_verified
            code = cleanup.get('root_exit_code')
            expected_signal_exit = root_observation_verified and root_before_cleanup is None and (
                code == -signal.SIGTERM and cleanup.get('sent_sigterm') is True or
                code == -signal.SIGKILL and cleanup.get('sent_sigkill') is True)
            if code is not None and code != 0 and not expected_signal_exit:
                # An empty live group does not establish a healthy shutdown.
                # Chromium's own Mac teardown watchdog can exit RESULT_CODE_HUNG
                # without any runner signal. Preserve its profile and evidence.
                cleanup['teardown_error'] = 'Unexpected Chromium exit status ' + str(code)
                errors.append(cleanup['teardown_error'])
        self.log.close()
        if not cleanup or not (cleanup.get('error') or cleanup.get('teardown_error')):
            try:
                shutil.rmtree(self.profile)
            except OSError as exc:
                errors.append('Profile cleanup: ' + str(exc))
        else:
            cleanup['retained_profile'] = str(self.profile)
        result = {'process': cleanup, 'errors': errors, 'graceful_close': graceful}
        write_json(self.directory / 'chromium.cleanup.json', result)
        return result

    def run(self, url, timeout, prefix):
        collector = Collector()
        began = time.monotonic()
        context, page, result = None, None, None
        cleanup_errors = []
        try:
            context = self.browser.call('Target.createBrowserContext')['browserContextId']
            target = self.browser.call('Target.createTarget', {'url': 'about:blank', 'browserContextId': context})['targetId']
            req = urllib.request.Request('http://127.0.0.1:' + str(self.port) + '/json/list')
            with urllib.request.urlopen(req, timeout=10) as response:
                targets = json.load(response)
            target_info = next(x for x in targets if x['id'] == target)
            page = CDP(target_info['webSocketDebuggerUrl'])
            page.call('Page.enable')
            page.call('Runtime.enable')
            nav = page.call('Page.navigate', {'url': url})
            if nav.get('errorText'):
                result = collector.result({'status': 'INFRA_ERROR', 'message': nav['errorText']})
            else:
                with prefix.with_suffix('.console.log').open('w', errors='backslashreplace') as log:
                    while time.monotonic() - began < timeout and not collector.done:
                        try:
                            event = page.event(min(1, max(.01, timeout - (time.monotonic() - began))))
                        except socket.timeout:
                            continue
                        if event.get('method') == 'Runtime.consoleAPICalled':
                            args = event.get('params', {}).get('args', [])
                            text = ' '.join(str(x.get('value', x.get('description', ''))) for x in args)
                            log.write(text + '\n')
                            collector.line(text)
                        elif event.get('method') in ('Inspector.targetCrashed', 'Target.targetCrashed'):
                            result = collector.result({'status': 'CRASH', 'message': 'Chromium target crashed'})
                            break
                if result is None:
                    result = collector.result({'status': 'TIMEOUT', 'message': 'External file watchdog expired'})
        except Exception as exc:
            result = collector.result({'status': 'INFRA_ERROR', 'message': type(exc).__name__ + ': ' + str(exc)})
            if context is None:
                cleanup_errors.append('Context creation failed without a verified context id')
        finally:
            if page:
                try:
                    page.close()
                except Exception as exc:
                    cleanup_errors.append('Page DevTools close: ' + str(exc))
            if context is not None:
                try:
                    self.browser.call('Target.disposeBrowserContext', {'browserContextId': context})
                except Exception as exc:
                    cleanup_errors.append('Context disposal: ' + str(exc))
        cleanup = {'context_id': context, 'errors': cleanup_errors}
        if cleanup_errors:
            cleanup['error'] = '; '.join(cleanup_errors)
            prefix.with_suffix('.cleanup-error.txt').write_text(cleanup['error'])
        errors = list(self.sampler.errors)
        if self.sampler.stop_reason and not self.sampler.stopped.is_set():
            errors.append('Resource sampler stopped before browser cleanup: ' + self.sampler.stop_reason)
        result.update(duration_ms=round((time.monotonic() - began) * 1000), cleanup=cleanup,
                      browser_pid=self.process.pid, telemetry_errors=errors,
                      metrics_reference=getattr(self, 'metrics_reference', 'chromium.metrics.jsonl'))
        return result


def test_url(test, args):
    path = test['test']
    parsed = urllib.parse.urlsplit(path)
    # Match upstream tools/manifest/item.py URLManifestItem._flags exactly.
    flags = set(parsed.path.rsplit('/', 1)[-1].split('.')[1:-1])
    flags.update(urllib.parse.parse_qs(parsed.query).get('wpt_flags', []))
    secure = bool(flags & {'https', 'serviceworker', 'serviceworker-module'})
    origin = args.h2_origin if 'h2' in flags else args.secure_origin if secure else args.origin
    if 'www' in flags:
        parts = urllib.parse.urlsplit(origin)
        origin = urllib.parse.urlunsplit((parts.scheme, 'www.' + parts.netloc, parts.path, parts.query, parts.fragment))
    return origin.rstrip('/') + '/' + path.lstrip('/')


def inventory_command(args):
    root = args.wpt_root.resolve()
    manifest_path = args.manifest or root / 'MANIFEST.json'
    result = inventory_manifest(json.loads(manifest_path.read_text()))
    result.update(wpt_revision=git_value(root, ['rev-parse', 'HEAD']),
                  wpt_dirty=bool(git_value(root, ['status', '--porcelain', '--untracked-files=no'])),
                  manifest_sha256=file_digest(manifest_path), wpt_root=str(root))
    if not result['wpt_revision']:
        raise ValueError('WPT checkout has no revision')
    write_json(args.output, result)
    print(json.dumps({key: value for key, value in result.items() if key != 'tests'}))


def verify_reporter_route(origin):
    parts = urllib.parse.urlsplit(origin)
    if parts.scheme != 'http':
        raise ValueError('The reporter preflight requires the local HTTP WPT origin')
    endpoint = 'http://127.0.0.1:' + str(parts.port or 80) + '/resources/testharnessreport.js'
    request = urllib.request.Request(endpoint, headers={'Host': parts.netloc})
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(request, timeout=5) as response:
        payload = response.read(1024 * 1024)
    actual = hashlib.sha256(payload).hexdigest()
    if actual != hashlib.sha256(REPORTER.encode()).hexdigest():
        raise ValueError('Official WPT server reporter alias does not match this runner; restart with the exact testharnessreport.js route')
    return actual


def native_fixture_signature():
    """Bind native test routing and certificate bytes to every resumable run."""
    ca = os.environ.get('SSL_CERT_FILE')
    return {'host_map': os.environ.get('LUMEN_HOST_MAP'),
            'host_map_mode': os.environ.get('LUMEN_HOST_MAP_MODE'),
            'ssl_cert_sha256': file_digest(Path(ca)) if ca else None}


def source_fixture_signature(inventory):
    """Reject stale inventory pointers before opening any browser process."""
    root = Path(inventory['wpt_root'])
    revision = git_value(root, ['rev-parse', 'HEAD'])
    manifest_hash = file_digest(root / 'MANIFEST.json')
    if revision != inventory['wpt_revision'] or manifest_hash != inventory['manifest_sha256']:
        raise ValueError('WPT checkout or manifest changed since inventory creation; generate a fresh inventory and output cohort')
    tracked_diff = subprocess.run(['git', '-C', str(root), 'diff', '--binary', 'HEAD', '--'],
                                  capture_output=True, timeout=30, check=True).stdout
    if tracked_diff:
        raise ValueError('Tracked upstream WPT sources are modified; an unmodified pinned checkout is required')
    return {'wpt_revision': revision, 'manifest_sha256': manifest_hash, 'tracked_upstream_sources_clean': True}


def select_schedule(inventory, types=None, prefixes=(), offset=0, limit=None):
    """Slice a stable type/URL order without losing generated worker variants."""
    if offset < 0 or (limit is not None and limit <= 0):
        raise ValueError('Offset must be nonnegative and limit must be positive')
    kinds = set(types.split(',')) if types else set(inventory['test_variants_by_type'])
    unknown = kinds - set(inventory['test_variants_by_type'])
    if unknown:
        raise ValueError('Unknown test types: ' + ', '.join(sorted(unknown)))
    eligible = sorted((row for row in inventory['tests'] if row['type'] in kinds and
                       (not prefixes or any(row['test'].startswith(prefix) for prefix in prefixes))),
                      key=lambda row: (row['type'], row['test']))
    selection = eligible[offset:offset + limit if limit is not None else None]
    if not selection:
        raise ValueError('Selection contains no tests')
    descriptor = {'types': sorted(kinds), 'prefixes': sorted(set(prefixes)),
                  'order': ['type', 'test URL'], 'offset': offset, 'limit': limit,
                  'eligible_files_before_slice': len(eligible), 'selected_files': len(selection)}
    return selection, descriptor


def inventory_coverage(inventory, selection):
    selected = Counter(row['type'] for row in selection)
    coverage = {}
    for kind, total in sorted(inventory['test_variants_by_type'].items()):
        supported = kind in SUPPORTED_EXECUTION_TYPES
        coverage[kind] = {'inventory_variants': total, 'selected_variants': selected[kind],
                          'outside_selection_variants': total - selected[kind],
                          'executor_available': supported,
                          'execution_plan': ('SCHEDULED' if selected[kind] else 'NOTRUN') if supported else 'NOTRUN',
                          'reason': ('Official server wrapper and synchronous reporter' if selected[kind] else 'Outside this explicit selection')
                                    if supported else 'No validated executor for this class; never counted as passed'}
    return coverage


def measurement_failure(result):
    """Missing samples on a short case are allowed; a broken collector stops work."""
    metrics = result.get('metrics') or {}
    if metrics.get('errors') or metrics.get('telemetry_status') == 'failed':
        return metrics.get('errors') or ['Resource collector reported failure']
    if result.get('telemetry_errors'):
        return result['telemetry_errors']
    return None


def run_command(args):
    if platform.system() != 'Darwin':
        raise ValueError('This execution adapter is for the authorized Mac host only')
    inventory = json.loads(args.inventory.read_text())
    selection, selection_descriptor = select_schedule(inventory, args.types, args.prefix, args.offset, args.limit)
    if args.timeout <= 0 or args.long_timeout <= 0 or (args.max_seconds is not None and args.max_seconds <= 0):
        raise ValueError('Timeouts and optional time budget must be positive')
    source_fixture = source_fixture_signature(inventory)
    route_digest = verify_reporter_route(args.origin)
    args.output.mkdir(parents=True, exist_ok=True)
    binary = args.binary.resolve()
    if not binary.is_file():
        raise ValueError('Browser executable missing: ' + str(binary))
    initial_binary_identity = binary_identity(binary)
    binary_hash = file_digest(binary)
    if binary_identity(binary) != initial_binary_identity:
        raise ValueError('Browser executable changed while hashing; use a verified immutable executable snapshot')
    signature = {'wpt_revision': inventory['wpt_revision'], 'manifest_sha256': inventory['manifest_sha256'],
                 'inventory_sha256': file_digest(args.inventory), 'source_fixture': source_fixture,
                 'server_config_sha256': file_digest(args.server_config) if args.server_config else None,
                 'fixture_manifest_sha256': file_digest(args.fixture_manifest) if args.fixture_manifest else None,
                 'selection': selection_descriptor,
                 'schedule_sha256': digest(selection), 'engine': args.engine, 'binary_sha256': binary_hash,
                 'runner_sha256': file_digest(Path(__file__)), 'reporter_sha256': hashlib.sha256(REPORTER.encode()).hexdigest(),
                 'reporter_delivery': 'official WPT /resources/testharnessreport.js exact file alias',
                 'verified_http_reporter_sha256': route_digest,
                 'monitor_sha256': file_digest(Path(__file__).resolve().parents[2] / 'tools' / 'monitor_process.py'),
                 'native_fixture': native_fixture_signature() if args.engine == 'lumen' else None,
                 'origin': args.origin, 'secure_origin': args.secure_origin, 'h2_origin': args.h2_origin,
                 'timeout_s': args.timeout, 'long_timeout_s': args.long_timeout,
                 'strict_infrastructure': args.strict_infrastructure,
                 'chromium_args': args.chromium_arg,
                 'chromium_headless': not args.chromium_headful if args.engine == 'chromium' else None}
    meta_path = args.output / 'run.json'
    if meta_path.exists():
        old = json.loads(meta_path.read_text())
        if old['signature'] != signature:
            raise ValueError('Resume signature differs; use a new output directory')
        if not args.resume:
            raise ValueError('Output already exists; use --resume or a new output directory')
        verified_schedule(args.output, signature)
        meta = old
    else:
        meta = {'signature': signature, 'platform': platform.platform(), 'machine': platform.machine(),
                'started_at_unix_ms': round(time.time()*1000), 'binary': str(binary),
                'lumen_revision': git_value(Path(__file__).resolve().parents[2], ['rev-parse', 'HEAD']),
                'lumen_dirty': bool(git_value(Path(__file__).resolve().parents[2], ['status', '--porcelain', '--untracked-files=no'])),
                'full_inventory_counts': inventory['test_variants_by_type'], 'scheduled_files': len(selection),
                'supported_execution_types': sorted(SUPPORTED_EXECUTION_TYPES),
                'inventory_coverage_by_type': inventory_coverage(inventory, selection),
                'automation_limitation': 'No testdriver/WebDriver actions; manual, reference and protocol tests remain explicitly unrun.',
                'is_full_suite_schedule': len(selection) == len(inventory['tests'])}
        write_json(meta_path, meta)
        write_json(args.output / 'schedule.json', selection)
    reporter = args.output / 'reporter.js'
    reporter.write_text(REPORTER)
    results_path = args.output / 'results.jsonl'
    accounting = restore_accounting(results_path, selection)
    done = accounting.seen
    chrome = None
    stop = threading.Event()
    invocation_began = time.monotonic()
    meta['last_invocation_budget_s'] = args.max_seconds
    meta['last_invocation_started_unix_ms'] = round(time.time() * 1000)
    meta.pop('stopped_for_time_budget', None)
    consecutive_infra_errors = 0
    old_handlers = {}
    for sig in (signal.SIGINT, signal.SIGTERM):
        old_handlers[sig] = signal.signal(sig, lambda *_: stop.set())
    try:
        if args.engine == 'chromium' and len(done) < len(selection):
            session_directory = Path(tempfile.mkdtemp(prefix='chromium-session-', dir=args.output))
            meta.setdefault('chromium_sessions', []).append(str(session_directory))
            write_json(meta_path, meta)
            chrome = Chromium(binary, session_directory, args.chromium_arg, headless=not args.chromium_headful)
            meta['browser_version'] = chrome.version
            write_json(meta_path, meta)
        with results_path.open('a') as output:
            for index, test in enumerate(selection):
                if stop.is_set():
                    break
                if (test['type'], test['test']) in done:
                    continue
                if args.max_seconds is not None and time.monotonic() - invocation_began >= args.max_seconds:
                    meta['stopped_for_time_budget'] = True
                    break
                observed_binary_identity = binary_identity(binary)
                if observed_binary_identity != initial_binary_identity:
                    meta['stopped_for_binary_change'] = {'expected': initial_binary_identity, 'observed': observed_binary_identity}
                    break
                prefix = args.output / ('case-' + format(index, '06d') + '-' + digest([test['type'], test['test']])[:10])
                base = {'test': test['test'], 'type': test['type'], 'url': test_url(test, args),
                        'sequence': index, 'started_at_unix_ms': round(time.time()*1000)}
                if test['type'] not in SUPPORTED_EXECUTION_TYPES:
                    result = {'status': 'NOTRUN', 'reason': 'No validated executor for ' + test['type'], 'subtests': []}
                else:
                    timeout = args.long_timeout if test.get('timeout') == 'long' else args.timeout
                    try:
                        result = chrome.run(base['url'], timeout, prefix) if chrome else run_lumen(binary, base['url'], timeout, prefix, reporter.resolve())
                    except Exception as exc:
                        result = {'status': 'INFRA_ERROR', 'message': type(exc).__name__ + ': ' + str(exc), 'subtests': []}
                observed_binary_identity = binary_identity(binary)
                if observed_binary_identity != initial_binary_identity:
                    # Keep raw diagnostic evidence, but no assertions from a
                    # changed executable may contribute to this cohort's rates.
                    write_json(prefix.with_suffix('.invalid-binary-result.json'), result)
                    result = {'status': 'INFRA_ERROR', 'message': 'Executable changed during this file; functional result quarantined', 'subtests': []}
                    meta['stopped_for_binary_change'] = {'expected': initial_binary_identity, 'observed': observed_binary_identity}
                    stop.set()
                base.update(result)
                output.write(json.dumps(base, ensure_ascii=True) + '\n')
                output.flush()
                os.fsync(output.fileno())
                accounting.add(base)
                consecutive_infra_errors = consecutive_infra_errors + 1 if base['status'] == 'INFRA_ERROR' else 0
                cleanup_state = base.get('cleanup', {})
                if cleanup_state.get('error') or cleanup_state.get('profile_error'):
                    meta['stopped_for_cleanup_failure'] = True
                    stop.set()
                resource_error = measurement_failure(base)
                if resource_error:
                    meta['stopped_for_telemetry_failure'] = resource_error
                    stop.set()
                if consecutive_infra_errors >= 3:
                    meta['stopped_for_infrastructure_failure'] = True
                    stop.set()
                if args.strict_infrastructure and test['type'] in SUPPORTED_EXECUTION_TYPES:
                    reporter_attached = (base.get('reporter_state') or {}).get('status') == 'ATTACHED'
                    if base['status'] == 'INFRA_ERROR' or not reporter_attached:
                        meta['stopped_for_infrastructure_failure'] = {'test': base['test'], 'status': base['status'],
                                                                      'reporter_attached': reporter_attached}
                        stop.set()
                if len(done) % 10 == 0 or test['type'] in SUPPORTED_EXECUTION_TYPES:
                    summary = accounting.value()
                    write_json(args.output / 'summary.json', summary)
                    if test['type'] in SUPPORTED_EXECUTION_TYPES:
                        print(json.dumps({'completed': len(done), 'scheduled': len(selection), 'type': base['type'], 'test': base['test'],
                                          'status': base['status'], 'subtests': dict(Counter(s['status'] for s in base['subtests']))}), flush=True)
    finally:
        if chrome:
            try:
                meta['chromium_cleanup'] = chrome.close()
            except Exception as exc:
                meta['chromium_cleanup'] = {'errors': ['Cleanup finalization raised: ' + type(exc).__name__ + ': ' + str(exc)]}
            chrome_cleanup = meta['chromium_cleanup']
            if chrome_cleanup.get('errors') or (chrome_cleanup.get('process') or {}).get('error'):
                meta['stopped_for_cleanup_failure'] = True
            resource_error = measurement_failure({'metrics': getattr(chrome.sampler, 'final_summary', None)})
            if resource_error:
                meta['stopped_for_telemetry_failure'] = resource_error
        for sig, old_handler in old_handlers.items():
            signal.signal(sig, old_handler)
        write_json(args.output / 'summary.json', accounting.value())
        meta['last_checkpoint_unix_ms'] = round(time.time()*1000)
        meta['finished_schedule'] = len(done) == len(selection)
        write_json(meta_path, meta)


def compare_command(args):
    lm = json.loads((args.lumen / 'run.json').read_text())
    cm = json.loads((args.chromium / 'run.json').read_text())
    if lm['signature']['engine'] != 'lumen' or cm['signature']['engine'] != 'chromium':
        raise ValueError('Comparison requires Lumen results and Chromium control results')
    for key in ('wpt_revision', 'manifest_sha256', 'inventory_sha256', 'source_fixture', 'server_config_sha256', 'fixture_manifest_sha256', 'selection',
                'schedule_sha256', 'runner_sha256', 'reporter_sha256', 'monitor_sha256', 'origin', 'secure_origin', 'h2_origin', 'timeout_s', 'long_timeout_s', 'strict_infrastructure'):
        if lm['signature'][key] != cm['signature'][key]:
            raise ValueError('Comparison requires identical ' + key)
    schedule = verified_schedule(args.lumen, lm['signature'])
    verified_schedule(args.chromium, cm['signature'])
    lumen_summary, chromium_summary, compared = compare_streams(args.lumen / 'results.jsonl', args.chromium / 'results.jsonl', schedule)
    report = {'wpt_revision': lm['signature']['wpt_revision'], 'scheduled_files': len(schedule),
              'lumen': lumen_summary, 'chromium': chromium_summary,
              'parity': compared, 'full_inventory_counts': lm['full_inventory_counts'],
              'inventory_coverage_by_type': lm['inventory_coverage_by_type'],
              'is_full_suite_schedule': lm['is_full_suite_schedule'],
              'lumen_binary_sha256': lm['signature']['binary_sha256'],
              'chromium_binary_sha256': cm['signature']['binary_sha256'],
              'chromium_version': cm.get('browser_version'),
              'lumen_fixture': lm['signature'].get('native_fixture'),
              'chromium_args': cm['signature'].get('chromium_args'),
              'chromium_headless': cm['signature'].get('chromium_headless', True),
              'run_state': {engine: {'finished_schedule': meta.get('finished_schedule', False),
                                     'stops': {key: value for key, value in meta.items() if key.startswith('stopped_for_')}}
                            for engine, meta in (('lumen', lm), ('chromium', cm))}}
    write_json(args.output, report)
    print(json.dumps({k: v for k, v in report.items() if k != 'parity'}, indent=2))
    print(json.dumps({k: v for k, v in report['parity'].items() if k != 'first_200_gaps'}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    inv = sub.add_parser('inventory')
    inv.add_argument('--wpt-root', type=Path, required=True)
    inv.add_argument('--manifest', type=Path)
    inv.add_argument('--output', type=Path, required=True)
    inv.set_defaults(func=inventory_command)
    run = sub.add_parser('run')
    run.add_argument('--inventory', type=Path, required=True)
    run.add_argument('--engine', choices=['lumen', 'chromium'], required=True)
    run.add_argument('--binary', type=Path, required=True)
    run.add_argument('--output', type=Path, required=True)
    run.add_argument('--types', help='Comma separated test types; default schedules the full manifest')
    run.add_argument('--prefix', action='append', default=[])
    run.add_argument('--limit', type=int)
    run.add_argument('--offset', type=int, default=0, help='Skip this many eligible variants before --limit')
    run.add_argument('--max-seconds', type=float, help='Stop between files after this invocation budget; resume preserves the exact schedule')
    run.add_argument('--server-config', type=Path, help='Bind exact WPT server fixture configuration bytes to this cohort')
    run.add_argument('--fixture-manifest', type=Path, help='Bind realized endpoints, server options and readiness evidence to this cohort')
    run.add_argument('--strict-infrastructure', action='store_true', help='Stop after the first infrastructure error or missing reporter attachment')
    run.add_argument('--timeout', type=float, default=15)
    run.add_argument('--long-timeout', type=float, default=65)
    run.add_argument('--origin', default='http://web-platform.test:8000')
    run.add_argument('--secure-origin', default='https://web-platform.test:8443')
    run.add_argument('--h2-origin', default='https://web-platform.test:9000')
    run.add_argument('--chromium-arg', action='append', default=[])
    run.add_argument('--chromium-headful', action='store_true', help='Use a visible isolated Chromium window; preserve GPU/web features and bind this mode in provenance')
    run.add_argument('--resume', action='store_true')
    run.set_defaults(func=run_command)
    compare = sub.add_parser('compare')
    compare.add_argument('--lumen', type=Path, required=True)
    compare.add_argument('--chromium', type=Path, required=True)
    compare.add_argument('--output', type=Path, required=True)
    compare.set_defaults(func=compare_command)
    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
