#!/usr/bin/env python3
"""Read existing WPT telemetry; does not launch workloads or infer scroll CPU."""
from pathlib import Path
from datetime import datetime, timezone
from collections import Counter
import csv
import hashlib
import json
import statistics

ROOT = Path('/Users/devin/repos/lumen').resolve()
if Path.cwd().resolve() != ROOT:
    raise SystemExit('Run from the verified Mac repository')
BASE = ROOT / 'out/10292019r-mac/wpt-baseline-001'


def sha(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def compact(row):
    metrics = row.get('metrics') or {}
    cleanup = row.get('cleanup') or {}
    return {'test': row.get('test'), 'sequence': row.get('sequence'), 'status': row.get('status'),
            'assertions': len(row.get('subtests', [])), 'duration_ms': row.get('duration_ms'),
            'pid': row.get('pid'), 'metrics': {key: metrics.get(key) for key in
                ['samples', 'interval_cpu_samples', 'observed_span_s', 'max_sampling_gap_s',
                 'cpu_peak_percent', 'interval_cpu_peak_percent', 'rss_peak_mb', 'rss_final_mb',
                 'rss_steady_growth_mb', 'errors', 'telemetry_status']},
            'cleanup': {key: cleanup.get(key) for key in
                ['error', 'profile_error', 'process_group_gone', 'remaining_group_members',
                 'root_exit_code', 'sent_sigterm', 'sent_sigkill']}}


report = {'updated_utc': datetime.now(timezone.utc).isoformat(),
          'scope': 'Recorded baseline WPT process-tree peaks and per-case cleanup; not GUI scroll/idle acceptance',
          'groups': []}
for group in ['url', 'encoding-core']:
    row = {'group': group}
    monitor = BASE / (group + '.json')
    if monitor.is_file():
        value = json.loads(monitor.read_text())
        row['wrapper'] = {key: value.get(key) for key in ['started_utc', 'finished_utc', 'duration_s',
            'samples', 'interval_cpu_samples', 'interval_cpu_mean_percent', 'interval_cpu_peak_percent',
            'rss_peak_mb', 'rss_final_mb', 'rss_steady_growth_mb', 'natural_exit_code', 'stop_reason',
            'collector_error', 'owned_group_cleanup', 'violations']}
        row['wrapper_sha256'] = sha(monitor)
    csvfile = BASE / (group + '.csv')
    if csvfile.is_file():
        with csvfile.open() as stream:
            samples = list(csv.DictReader(stream))
        row['wrapper_sample_count'] = len(samples)
        row['first_three_samples'] = samples[:3]
        row['last_five_samples'] = samples[-5:]
        row['peak_rss_sample'] = max(samples, key=lambda p: float(p.get('rss_kb') or 0), default=None)
    results_path = BASE / 'cohort' / group / 'results.jsonl'
    row['results_path'] = str(results_path.relative_to(ROOT))
    if results_path.is_file():
        results = [json.loads(line) for line in results_path.read_text().splitlines() if line.strip()]
        row['results_sha256'] = sha(results_path)
        row['cases'] = len(results)
        row['statuses'] = dict(Counter(item.get('status') for item in results))
        row['total_assertions'] = sum(len(item.get('subtests', [])) for item in results)
        row['cleanup_verified_gone'] = sum((item.get('cleanup') or {}).get('process_group_gone') is True for item in results)
        row['cleanup_errors'] = [compact(item) for item in results if (item.get('cleanup') or {}).get('error') or (item.get('cleanup') or {}).get('profile_error')]
        row['top_five_sampled_rss_cases'] = [compact(item) for item in sorted(results,
            key=lambda item: ((item.get('metrics') or {}).get('rss_peak_mb') or 0), reverse=True)[:5]]
        row['top_five_assertion_counts'] = [compact(item) for item in sorted(results,
            key=lambda item: len(item.get('subtests', [])), reverse=True)[:5]]
        row['interval_coverage_cases'] = sum(((item.get('metrics') or {}).get('interval_cpu_samples') or 0) > 0 for item in results)
    else:
        row['results_missing'] = True
    report['groups'].append(row)
report['interpretation_limits'] = [
    'Wrapper RSS sums the runner and its browser descendants; individual peaks may occur between one-second samples.',
    'Fresh-process WPT cleanup verifies process release; it does not measure retained memory in one continuously open browser.',
    'Whole-file testharness, test data and assertion results can consume memory; case/assertion counts identify scope but do not by themselves prove that every allocation is necessary.',
    'The separate fixed native scroll/idle/navigation fixture must establish steady-use performance.'
]
output = ROOT / 'out/10292019r-apple/wpt-resource-audit.json'
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
