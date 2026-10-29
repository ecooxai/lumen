#!/usr/bin/env python3
"""Read completed native003 WPT telemetry without launching or signalling any app."""
from pathlib import Path
from collections import Counter
from datetime import datetime, timezone
import csv
import hashlib
import json
import math
import statistics
import subprocess
import time

ROOT = Path.cwd().resolve()
if str(ROOT) != '/Users/devin/repos/lumen':
    raise SystemExit('Expected canonical task Mac repository')
BASE = ROOT / 'out/10292019r-mac'
COHORT = BASE / 'wpt-final-003/cohort'
OUT = BASE / 'wpt-resource-audit-003'
OUT.mkdir(exist_ok=False)
SOURCE = 'b816d32f2a486e28fa2cb6846c603a40e7e8ca02'
BINARY = '5c16cbd5db05ddf0b8f7b22e54fb86dce6797771db80402e171ac8581955a2f6'
began = time.monotonic()
report = {'startedUTC': datetime.now(timezone.utc).isoformat(), 'acceptedEvidence': False,
          'sourceCommit': SOURCE, 'lumenSHA256': BINARY, 'issues': [], 'groups': [],
          'cpuConvention': 'Cumulative CPU-time intervals;100%=one logical CPU. The ps rolling estimate is reported separately.',
          'scope': 'Completed WPT process-tree samples, bounded durations and cleanup; no GUI scrolling or idle acceptance inferred.',
          'memoryConvention': 'MiB from sampled process-tree RSS; shared pages may be counted more than once. Subsecond/startup samples are not steady-state memory evidence.'}

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def p95(values):
    return sorted(values)[max(0, math.ceil(len(values) * .95) - 1)] if values else None

def maximum(values):
    return max(values) if values else None

def number(value):
    return isinstance(value, (float, int)) and not isinstance(value, bool) and math.isfinite(value)

def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()

def compact_case(row, group, directory):
    metrics = row.get('metrics') or {}
    cleanup = row.get('cleanup') or {}
    prefix = 'case-' + format(row['sequence'], '06d') + '-' + digest([row['type'], row['test']])[:10]
    raw_path = directory / (prefix + '.metrics.jsonl')
    result = {'group': group, 'test': row['test'], 'status': row['status'],
              'assertions': len(row.get('subtests', [])), 'durationSeconds': row['duration_ms'] / 1000,
              'pid': row.get('pid'), 'cleanupVerifiedGone': cleanup.get('process_group_gone') is True,
              'cleanupError': cleanup.get('error') or cleanup.get('profile_error'),
              'sentSIGTERM': cleanup.get('sent_sigterm'), 'sentSIGKILL': cleanup.get('sent_sigkill'),
              'telemetryErrors': metrics.get('errors') or row.get('telemetry_errors') or [],
              'declaredSamples': metrics.get('samples'), 'rawPath': str(raw_path.relative_to(ROOT)),
              'reviewFlags': []}
    if cleanup.get('sent_sigkill'):
        result['reviewFlags'].append('Owned cleanup required SIGKILL')
    if result['cleanupError'] or result['telemetryErrors'] or not result['cleanupVerifiedGone']:
        result['reviewFlags'].append('Cleanup or telemetry evidence requires review')
    if not raw_path.is_file():
        require(not metrics.get('samples'), 'Recorded nonempty metrics have no raw file: ' + row['test'])
        result.update(rawSamples=0, intervalSamples=0, intervalCPUPeakPercent=None,
                      RSSPeakMiB=None, rawCoverage='No sample; no resource claim')
        return result
    require(raw_path.stat().st_size <= 4 * 1024 * 1024 and not raw_path.is_symlink(), 'Unexpected large/type raw case telemetry')
    raw = [json.loads(line) for line in raw_path.read_text().splitlines() if line.strip()]
    require(len(raw) <= 200 and len(raw) == metrics.get('samples'), 'Raw sample count differs: ' + row['test'])
    elapsed = [sample['elapsed_s'] for sample in raw]
    require(all(number(value) for value in elapsed), 'Invalid raw sample time')
    gaps = [b-a for a, b in zip(elapsed, elapsed[1:])]
    require(all(gap > 0 for gap in gaps), 'Non-increasing raw sample times')
    cpu = [sample['interval_cpu_percent'] for sample in raw if number(sample.get('interval_cpu_percent'))]
    rss = [sample['rss_kb'] / 1024 for sample in raw]
    require(all(number(value) and value >= 0 for value in cpu + rss), 'Invalid raw resource sample')
    require(len(cpu) == metrics.get('interval_cpu_samples'), 'Raw interval coverage differs')
    if cpu:
        require(abs(max(cpu) - metrics['interval_cpu_peak_percent']) < .002, 'Raw CPU peak differs')
    if rss:
        require(abs(max(rss) - metrics['rss_peak_mb']) < .002, 'Raw RSS peak differs')
    cpu_seconds = high_seconds = longest_high = current_high = 0.0
    high_count = 0
    for previous, sample in zip(raw, raw[1:]):
        interval = sample['elapsed_s'] - previous['elapsed_s']
        value = sample.get('interval_cpu_percent')
        if not number(value):
            current_high = 0.0
            continue
        cpu_seconds += value * interval / 100
        if value >= 100:
            high_seconds += interval
            current_high += interval
            longest_high = max(longest_high, current_high)
            high_count += 1
        else:
            current_high = 0.0
    warm = [sample['rss_kb']/1024 for sample in raw if sample['elapsed_s'] >= 2]
    # This is within an actively running case, not an idle or leak verdict.
    active_growth = statistics.median(warm[-3:]) - statistics.median(warm[:3]) if len(warm) >= 6 else None
    if longest_high >= 5:
        result['reviewFlags'].append('At least five observed seconds continuously using one or more logical CPUs during active WPT')
    if active_growth is not None and active_growth >= 128:
        result['reviewFlags'].append('Active-case sampled RSS grows at least128MiB after the first2seconds; allocations require workload context')
    if row['status'] in ('TIMEOUT', 'CRASH', 'ERROR', 'INFRA_ERROR') and cpu and cpu[-1] > 30:
        result['reviewFlags'].append('Non-completing case still consumes more than30%of one CPU in its last sampled interval')
    if gaps and max(gaps) > 1.5:
        result['reviewFlags'].append('One-second sampling gap exceeds1.5seconds')
    result.update(rawSamples=len(raw), intervalSamples=len(cpu), rawSHA256=sha(raw_path),
                  observedSpanSeconds=round(elapsed[-1]-elapsed[0], 3) if elapsed else 0,
                  maximumSamplingGapSeconds=round(max(gaps), 3) if gaps else None,
                  intervalCPUMedianPercent=statistics.median(cpu) if cpu else None,
                  intervalCPU_P95Percent=p95(cpu), intervalCPUPeakPercent=maximum(cpu),
                  psRollingCPUPeakPercent=maximum([sample['cpu_percent'] for sample in raw]),
                  measuredCPUSeconds=round(cpu_seconds, 3), samplesAtLeastOneCPU=high_count,
                  secondsAtLeastOneCPU=round(high_seconds, 3), longestConsecutiveSecondsAtLeastOneCPU=round(longest_high, 3),
                  RSSPeakMiB=maximum(rss), RSSFirstMiB=rss[0] if rss else None,
                  RSSFinalMiB=rss[-1] if rss else None, activeCasePost2sRSSGrowthMiB=active_growth,
                  rawCoverage='Complete intervals captured' if cpu else 'Startup-only/subsecond; no CPU interval or steady-state memory conclusion')
    return result

try:
    accepted_path = BASE / 'independent-final-003/receipt.json'
    accepted = json.loads(accepted_path.read_text())
    require(accepted.get('acceptedEvidence') is True and accepted['sourceCommit'] == SOURCE and
            accepted['binary']['sha256'] == BINARY, 'Require completed independent final WPT audit first')
    plan_path = COHORT / 'cohort.json'
    plan = json.loads(plan_path.read_text())
    require(sha(plan_path) == accepted['planSHA256'], 'Frozen cohort plan differs')
    manifest = {row['path']: row for row in accepted['rawFiles']}
    all_cases = []
    for group in plan['groups']:
        directory = COHORT / group
        results_path = directory / 'results.jsonl'
        if not results_path.is_file():
            directory = COHORT
            results_path = directory / (group + '-results.jsonl')
        if not results_path.is_file():
            continue
        before_hash = sha(results_path)
        require(before_hash == manifest[str(results_path.relative_to(ROOT))]['sha256'], 'Accepted results hash differs')
        require(results_path.stat().st_size <= 128 * 1024 * 1024, 'Group results exceed bounded128MiB scan')
        cases = []
        with results_path.open() as stream:
            for line in stream:
                if not line.strip():
                    continue
                row = json.loads(line)
                item = compact_case(row, group, directory)
                if item.get('rawSHA256'):
                    require(manifest.get(item['rawPath'], {}).get('sha256') == item['rawSHA256'], 'Accepted raw metric hash differs')
                cases.append(item)
                require(len(cases) <= 1515, 'Unexpected case count')
        require(sha(results_path) == before_hash, 'Results changed during read-only audit')
        entry = {'group': group, 'cases': len(cases), 'resultsSHA256': before_hash,
                 'statuses': dict(Counter(item['status'] for item in cases)),
                 'cleanupVerifiedGone': sum(item['cleanupVerifiedGone'] for item in cases),
                 'zeroCPUIntervalCases': sum(item['intervalSamples'] == 0 for item in cases),
                 'sampledCPUPeakPercent': maximum([item['intervalCPUPeakPercent'] for item in cases if item.get('intervalCPUPeakPercent') is not None]),
                 'sampledRSSPeakMiB': maximum([item['RSSPeakMiB'] for item in cases if item.get('RSSPeakMiB') is not None])}
        wrapper = BASE / 'wpt-final-003' / (group + '.json')
        wrapper_csv = wrapper.with_suffix('.csv')
        if wrapper.is_file():
            data = json.loads(wrapper.read_text())
            entry['wholeGroupRunnerTree'] = {key: data.get(key) for key in ('duration_s','samples','interval_cpu_samples','interval_cpu_peak_percent','rss_peak_mb','rss_final_mb','natural_exit_code','collector_error','owned_group_cleanup')}
            entry['wrapperSHA256'] = sha(wrapper)
        if wrapper_csv.is_file():
            with wrapper_csv.open() as stream:
                samples = list(csv.DictReader(stream))
            require(len(samples) <= 2000, 'Unexpected group wrapper sample count')
            entry['wholeGroupTailSamples'] = samples[-5:]
        report['groups'].append(entry)
        all_cases.extend(cases)
    require(len(all_cases) == accepted['summary']['recorded'], 'Resource audit case denominator differs')
    rows_path = OUT / 'case-resource-summary.jsonl'
    with rows_path.open('w') as stream:
        for item in all_cases:
            stream.write(json.dumps(item, separators=(',', ':')) + '\n')
    by_cpu = sorted(all_cases, key=lambda item: item.get('intervalCPUPeakPercent') or -1, reverse=True)
    by_rss = sorted(all_cases, key=lambda item: item.get('RSSPeakMiB') or -1, reverse=True)
    flagged = [item for item in all_cases if item['reviewFlags']]
    report.update(acceptedEvidence=True, acceptedWptReceiptSHA256=sha(accepted_path),
                  denominator=accepted['summary'], completedCases=len(all_cases),
                  casesWithCompleteCPUIntervals=sum(item['intervalSamples'] > 0 for item in all_cases),
                  startupOnlyOrUnsampledCases=sum(item['intervalSamples'] == 0 for item in all_cases),
                  cleanupVerifiedGone=sum(item['cleanupVerifiedGone'] for item in all_cases),
                  cleanupErrors=sum(bool(item['cleanupError']) for item in all_cases),
                  hardKillCleanupCases=sum(bool(item['sentSIGKILL']) for item in all_cases),
                  collectorErrorCases=sum(bool(item['telemetryErrors']) for item in all_cases),
                  topEightIntervalCPUCases=by_cpu[:8], topEightSampledRSSCases=by_rss[:8],
                  reviewFlagCases=len(flagged), firstFortyReviewFlagCases=flagged[:40],
                  allCaseSummary={'path': str(rows_path.relative_to(ROOT)), 'bytes': rows_path.stat().st_size, 'sha256': sha(rows_path)})
    ps = subprocess.run(['ps','-A','-o','pid=,ppid=,etime=,%cpu=,rss=,command='], text=True, capture_output=True, timeout=5)
    require(ps.returncode == 0, 'Read-only live process check failed')
    binary_path = str(ROOT / accepted['binary']['path'])
    report['liveFinalBinaryProcessesAfterWpt'] = [line.strip() for line in ps.stdout.splitlines() if binary_path in line]
    report['classification'] = {
        'boundedWorkloadEvidence': 'Case duration, assertion counts, sampled intervals and verified group disappearance are recorded for each outlier; no necessity of every allocation is inferred.',
        'leakScope': 'Fresh-process WPT verifies process release, not retained-memory behavior of a continuously open browser; the exact native00320-navigation and100-cycle controls provide that evidence separately.',
        'idleScope': 'No WPT interval is relabelled idle. Only the dedicated native GUI fixture establishes idle/scroll resource acceptance.',
        'highCPUFlagMeaning': 'Review flags are explicit workload observations, not new product pass/fail thresholds or proof of runaway CPU.',
        'currentProcesses': 'A matching binary process after the completed cohort requires review; this audit never sends signals.'}
except Exception as error:
    report['issues'].append(type(error).__name__ + ': ' + str(error))
    report['acceptedEvidence'] = False
report.update(finishedUTC=datetime.now(timezone.utc).isoformat(), durationSeconds=round(time.monotonic()-began, 3))
(OUT / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({'acceptedEvidence': report['acceptedEvidence'], 'cases': report.get('completedCases'),
                  'completeIntervalCases': report.get('casesWithCompleteCPUIntervals'),
                  'cleanupVerifiedGone': report.get('cleanupVerifiedGone'), 'reviewFlagCases': report.get('reviewFlagCases'),
                  'liveFinalBinaryProcesses': len(report.get('liveFinalBinaryProcessesAfterWpt', [])),
                  'issues': report['issues'], 'durationSeconds': report['durationSeconds'],
                  'receipt': str((OUT / 'receipt.json').relative_to(ROOT))}), flush=True)
raise SystemExit(0 if report['acceptedEvidence'] else 1)
