#!/usr/bin/env python3
"""Read closed v36 first-shard evidence; never launch or modify a browser.

This sidecar measures existing observations and retained generated profiles.
It does not change a schedule, remove a profile, qualify performance, or infer
resource use for a future larger shard from a short run.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shutil
import stat
import time

REPO = Path('/Users/devin/repos/lumen')
COHORT = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-full-b5b2-cft156-functional-v36'
COHORT_DIGEST = '525932c50deab0fc4f13b4620bbda7ebe7822e6f60d56615ae4f11dcbea70853'
COMPARISON_SHA = 'ce60912200ea9af49bdba61b9d587682d2de11ee34c387c33670aa0c70cf2278'
CORE_SHA = 'a7bc96c3697fb099ad63c145011bc434a66d865a713a62e5198731bed9600ac7'
INVENTORY_SHA = '9ffd9444520fd3158b7566f37600b0f10b56bbded1ccbd9b1b1ed8166a836053'
UNIT = 'testharness-000000'
MAX_INPUT = 64 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def canonical(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def identity(path):
    value = path.stat()
    return [value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_ctime_ns]


class Evidence:
    def __init__(self):
        self.files = {}

    def path(self, supplied):
        supplied = Path(supplied)
        path = supplied.resolve(strict=True)
        require(path.is_relative_to(COHORT) and path.is_file() and not supplied.is_symlink(),
                'Read only regular evidence files in the exact v36 cohort')
        require(path.stat().st_size <= MAX_INPUT, 'Evidence exceeds the bounded input size')
        if str(path) not in self.files:
            before = identity(path)
            with path.open('rb') as source:
                digest = hashlib.file_digest(source, 'sha256').hexdigest()
            require(identity(path) == before, 'Evidence changed while being fingerprinted')
            self.files[str(path)] = {'sha256': digest, 'identity': before}
        return path

    def load(self, path):
        return json.loads(self.path(path).read_text())

    def rows(self, path, limit, line_limit):
        with self.path(path).open('rb') as source:
            count = 0
            while True:
                line = source.readline(line_limit + 1)
                if not line:
                    break
                require(len(line) <= line_limit and line.endswith(b'\n'),
                        'Oversized or interrupted JSONL record; preserve raw evidence')
                require(line.strip(), 'Unexpected empty JSONL record')
                count += 1
                require(count <= limit, 'More records than the bounded evidence contract')
                yield json.loads(line)

    def unchanged(self):
        for name, record in self.files.items():
            require(identity(Path(name)) == record['identity'], 'Evidence changed during assessment: ' + name)


def sample_stream(evidence, path):
    count = intervals = 0
    first = last = previous = None
    peak = peak_processes = 0
    cpu_seconds = covered_seconds = 0.0
    interval_peak = None
    for row in evidence.rows(path, 20000, 65536):
        elapsed, rss = row['elapsed_s'], row['rss_kb']
        require(isinstance(elapsed, (int, float)) and math.isfinite(elapsed) and elapsed >= 0,
                'Invalid sample timestamp')
        require(isinstance(rss, int) and rss >= 0, 'Invalid sampled RSS')
        require(previous is None or elapsed > previous['elapsed_s'], 'Non-increasing sample times')
        interval = row.get('interval_cpu_percent')
        if interval is not None:
            require(previous is not None and isinstance(interval, (int, float))
                    and math.isfinite(interval) and interval >= 0, 'Invalid CPU interval sample')
            span = elapsed - previous['elapsed_s']
            covered_seconds += span
            cpu_seconds += interval * span / 100.0
            intervals += 1
            interval_peak = max(interval_peak, interval) if interval_peak is not None else interval
        first = first or row
        last = previous = row
        count += 1
        peak = max(peak, rss)
        peak_processes = max(peak_processes, row['processes'])
    return {'samples': count, 'interval_cpu_samples': intervals,
            'observed_span_s': round(last['elapsed_s'] - first['elapsed_s'], 3) if count > 1 else 0,
            'covered_cpu_interval_s': round(covered_seconds, 3),
            'observed_cpu_time_s': round(cpu_seconds, 6),
            'interval_time_weighted_cpu_percent': round(100 * cpu_seconds / covered_seconds, 3) if covered_seconds else None,
            'interval_cpu_peak_percent': interval_peak,
            'sampled_rss_peak_mib': round(peak / 1024, 3) if count else None,
            'sampled_rss_first_mib': round(first['rss_kb'] / 1024, 3) if first else None,
            'sampled_rss_last_mib': round(last['rss_kb'] / 1024, 3) if last else None,
            'sampled_rss_change_mib': round((last['rss_kb'] - first['rss_kb']) / 1024, 3) if first else None,
            'maximum_observed_processes': peak_processes,
            'quantiles': 'Not computed; scalar stream totals only. Empty or one-sample streams cannot qualify performance.'}


def profile_size(supplied, session):
    """Stat only this closed session's recorded profile, without following links."""
    path = Path(supplied)
    require(not path.is_symlink() and path.resolve(strict=True).parent == session
            and path.name.startswith('lumen-wpt-chrome-') and path.is_dir(),
            'Retained profile is outside its exact generated Chromium session')
    observed, unique, counts = {}, set(), Counter()
    apparent = allocated = 0
    pending = [(path, 0)]
    while pending:
        current, depth = pending.pop()
        require(depth <= 16 and len(observed) < 100000, 'Profile inventory exceeds its bounded scope')
        info = current.lstat()
        observed[str(current)] = [info.st_dev, info.st_ino, info.st_mode, info.st_size, info.st_mtime_ns, info.st_ctime_ns]
        if stat.S_ISDIR(info.st_mode):
            counts['directories'] += 1
            with os.scandir(current) as entries:
                for entry in entries:
                    require(len(pending) + len(observed) < 100000, 'Profile contains too many entries')
                    pending.append((Path(entry.path), depth + 1))
        elif stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode):
            counts['regular_files' if stat.S_ISREG(info.st_mode) else 'symlinks'] += 1
            apparent += info.st_size
            inode = (info.st_dev, info.st_ino)
            if inode not in unique:
                allocated += getattr(info, 'st_blocks', 0) * 512
                unique.add(inode)
        else:
            counts['other_entries'] += 1
    for name, expected in observed.items():
        info = Path(name).lstat()
        require([info.st_dev, info.st_ino, info.st_mode, info.st_size, info.st_mtime_ns, info.st_ctime_ns] == expected,
                'Retained profile changed during its read-only size inventory')
    return {'path': str(path), 'apparent_file_and_symlink_bytes': apparent,
            'unique_allocated_file_and_symlink_bytes': allocated, 'entry_counts': counts,
            'note': 'No links followed and no contents read or deleted; directory allocation is excluded.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(platform.system() == 'Darwin' and Path.cwd().resolve() == REPO,
            'Run only in the authorized Mac checkout through devinmac3d')
    evidence = Evidence()
    cohort = evidence.load(COHORT / 'cohort.json')
    controller = evidence.load(COHORT / 'controller.json')
    require(canonical(cohort) == COHORT_DIGEST == controller['cohort_sha256'], 'Measured v36 cohort identity changed')
    require(controller['status'] == 'paused' and controller.get('child_pid') is None
            and controller.get('stop_reason') == 'Invocation shard limit reached', 'Assess only the closed first invocation')
    unit = next(value for value in cohort['units'] if value['name'] == UNIT)
    require(unit['phase'] == 'broad' and unit['type'] == 'testharness' and unit['offset'] == 0
            and unit['selected_files'] == 50, 'The measured first-50 selection changed')
    state = controller['units'][UNIT]
    require(state.get('paired_complete') is True, 'Wait for both actual stages and their comparison')
    comparison_path = COHORT / UNIT / 'comparison.json'
    comparison = evidence.load(comparison_path)
    require(evidence.files[str(comparison_path)]['sha256'] == COMPARISON_SHA == state['comparison_sha256'],
            'Measured completed first-50 comparison changed')
    output = {'scope': 'Closed v36 first 50 testharness variants; no extrapolated resource pass or whole-suite score',
              'observed_unix_ms': round(time.time() * 1000), 'cohort': str(COHORT),
              'cohort_canonical_sha256': COHORT_DIGEST, 'comparison_sha256': COMPARISON_SHA,
              'paired_files': 50, 'parity': comparison.get('parity'), 'engines': {}, 'reference_sessions': [],
              'reference_performance_release_gate_passed': False,
              'measurement_units': {'cpu': 'Percent of one logical CPU; multithreaded work can exceed 100%.',
                  'memory': 'Sampled process-tree RSS in MiB; shared pages may be counted repeatedly.',
                  'sampling': 'One-second samples; short-lived processes between samples remain unobserved.',
                  'comparison': 'Native launches one app per file; Chromium uses one session. These are different lifetime scopes.'}}
    for engine in ('lumen', 'chromium'):
        directory = COHORT / UNIT / engine
        meta = evidence.load(directory / 'run.json')
        schedule = evidence.load(directory / 'schedule.json')
        signature = meta['signature']
        require(signature['engine'] == engine and signature['runner_sha256'] == CORE_SHA
                and signature['inventory_sha256'] == INVENTORY_SHA
                and canonical(schedule) == signature['schedule_sha256'] == unit['schedule_sha256'],
                'Actual engine/schedule/provenance differs')
        require(meta.get('finished_schedule') is True and len(schedule) == 50, 'The browser stage is incomplete')
        results, statuses, telemetry, cleanups = [], Counter(), Counter(), Counter()
        for index, row in enumerate(evidence.rows(directory / 'results.jsonl', 50, 16 * 1024 * 1024)):
            require((row['type'], row['test']) == (schedule[index]['type'], schedule[index]['test']),
                    'Actual rows are not the exact contiguous paired schedule')
            statuses[row['status']] += 1
            cleanup = row.get('cleanup') or {}
            cleanups['failed'] += bool(cleanup.get('error') or cleanup.get('profile_error') or cleanup.get('errors'))
            cleanups['sent_sigterm'] += cleanup.get('sent_sigterm') is True
            cleanups['sent_sigkill'] += cleanup.get('sent_sigkill') is True
            metrics = row.get('metrics') or {}
            telemetry[metrics.get('telemetry_status', 'session_reference')] += 1
            result = {'test': row['test'], 'status': row['status'], 'duration_ms': row['duration_ms'],
                      'harness_completed': 'subtest_count' in row, 'assertions': Counter(x['status'] for x in row.get('subtests', [])),
                      'telemetry_errors': row.get('telemetry_errors') or metrics.get('errors') or [],
                      'cleanup_failed': bool(cleanup.get('error') or cleanup.get('profile_error') or cleanup.get('errors'))}
            if engine == 'lumen':
                prefix = 'case-' + format(index, '06d') + '-' + canonical([row['type'], row['test']])[:10]
                result['samples'] = sample_stream(evidence, directory / (prefix + '.metrics.jsonl'))
                require(result['samples']['samples'] == metrics.get('samples'), 'Native summary/sample count differs')
            results.append(result)
        require(len(results) == 50, 'First-50 browser stage lost result rows')
        output['engines'][engine] = {'files': len(results), 'harness_statuses': statuses,
            'recorded_stage_span_s': round((meta['last_checkpoint_unix_ms'] - meta['started_at_unix_ms']) / 1000, 3),
            'case_duration_sum_s': round(sum(row['duration_ms'] for row in results) / 1000, 3),
            'telemetry_case_status_counts': telemetry, 'per_case_cleanup_counts': cleanups,
            'stops': {key: value for key, value in meta.items() if key.startswith('stopped_for_') and value},
            'observed_functional_reference_incident': state[engine].get('functional_reference_incident'),
            'cases': results}
        if engine == 'lumen':
            observed = [row['samples'] for row in results]
            seconds = sum(row['covered_cpu_interval_s'] for row in observed)
            cpu_time = sum(row['observed_cpu_time_s'] for row in observed)
            output['engines'][engine]['per_file_observation_totals'] = {
                'scope': 'Sum of observed per-file app lifetimes; excludes gaps and is not an idle or browser-session baseline.',
                'samples': sum(row['samples'] for row in observed),
                'interval_cpu_samples': sum(row['interval_cpu_samples'] for row in observed),
                'covered_cpu_interval_s': round(seconds, 3),
                'observed_cpu_time_s': round(cpu_time, 6),
                'interval_time_weighted_cpu_percent': round(100 * cpu_time / seconds, 3) if seconds else None,
                'maximum_sampled_case_rss_mib': max((row['sampled_rss_peak_mib'] for row in observed
                                                    if row['sampled_rss_peak_mib'] is not None), default=None),
                'cases_with_interval_cpu': sum(row['interval_cpu_samples'] > 0 for row in observed),
                'cases_without_interval_cpu': sum(row['interval_cpu_samples'] == 0 for row in observed)}
    for candidate in cohort['units']:
        if candidate['name'] not in controller['units']:
            continue
        state = controller['units'][candidate['name']]
        require(state.get('paired_complete') is True, 'An included prior smoke is not closed')
        directory = COHORT / candidate['name'] / 'chromium'
        meta = evidence.load(directory / 'run.json')
        require(len(meta.get('chromium_sessions', [])) == 1, 'This first invocation must have exactly one session per stage')
        session = Path(meta['chromium_sessions'][0]).resolve(strict=True)
        require(session.parent == directory and session.name.startswith('chromium-session-'), 'Session path escaped the exact stage')
        summary = evidence.load(session / 'chromium.metrics-summary.json')
        cleanup = evidence.load(session / 'chromium.cleanup.json')
        process = cleanup.get('process') or {}
        require(process.get('process_group_gone') is True and process.get('remaining_group_members') == {},
                'Do not inspect a retained profile until its owned group is verified gone')
        stream = sample_stream(evidence, session / 'chromium.metrics.jsonl')
        require(stream['samples'] == summary.get('samples'), 'Chromium summary/sample count differs')
        profile = profile_size(process['retained_profile'], session) if process.get('retained_profile') else None
        output['reference_sessions'].append({'unit': candidate['name'], 'phase': candidate['phase'], 'samples': stream,
            'collector_errors': summary.get('errors'), 'telemetry_status': summary.get('telemetry_status'),
            'scope': summary.get('session_scope'), 'cleanup': {'errors': cleanup.get('errors'),
                'process': {key: process.get(key) for key in ('root_exit_code', 'root_exit_before_signal_cleanup',
                    'sent_sigterm', 'sent_sigkill', 'process_group_gone', 'remaining_group_members', 'error', 'teardown_error')},
                'graceful_close': {key: cleanup.get('graceful_close', {}).get(key)
                    for key in ('acknowledged', 'drain_duration_ms', 'process_group_gone')}},
            'retained_profile': profile,
            'functional_reference_incident_classification': (state['chromium'].get('functional_reference_incident') or {}).get('classification')})
    counts = {kind: data['selected_broad_variants'] for kind, data in cohort['inventory_coverage'].items()
              if data.get('selected_broad_variants')}
    output['planning_arithmetic_only'] = {'scheduled_variants_by_type': counts,
        'broad_pairs_at_50': sum(math.ceil(value / 50) for value in counts.values()),
        'broad_pairs_at_200': sum(math.ceil(value / 200) for value in counts.values()),
        'note': 'Session counts only. Future runtime, retained profile size, and memory growth have not been measured.'}
    output['host_free_bytes_at_assessment'] = shutil.disk_usage(REPO).free
    evidence.unchanged()
    output['source_files'] = evidence.files
    target = args.output.resolve()
    require(target.is_relative_to(REPO / 'output/wpt') and not target.exists(), 'Use a fresh project-local assessment output')
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open('x') as destination:
        json.dump(output, destination, indent=2)
        destination.write('\n')
    print(json.dumps({'report': str(target), 'paired_files': 50,
        'stage_spans_s': {key: value['recorded_stage_span_s'] for key, value in output['engines'].items()},
        'native_observation_totals': output['engines']['lumen'].get('per_file_observation_totals'),
        'reference_sessions': output['reference_sessions'], 'planning_arithmetic_only': output['planning_arithmetic_only'],
        'host_free_bytes': output['host_free_bytes_at_assessment']}, sort_keys=True))


if __name__ == '__main__':
    main()
