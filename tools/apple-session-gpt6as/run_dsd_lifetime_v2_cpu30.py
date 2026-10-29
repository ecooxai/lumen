"""Build and measure the real repeated-context lifetime regression in isolation."""
from pathlib import Path
from datetime import datetime, timezone
import argparse
import hashlib
import json
import shutil
import statistics
import subprocess
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument('--phase', choices=['green-2'], required=True)
parser.add_argument('--candidate-summary')
args = parser.parse_args()
root = Path.cwd().resolve()
base = root / 'out/lume-20261029-cpu30'
out = base / ('native-dsd-lifetime-' + args.phase)
out.mkdir(parents=True, exist_ok=False)
build = 'build-apple-load-20261029'
utc = lambda: datetime.now(timezone.utc).isoformat()

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

fixture = Path('tests/test_declarative_shadow_lifetime_v2.cc')
makefile = Path('tools/apple-session-gpt6as/dsd_lifetime_v2_cpu30.mk')
if sha(fixture) != '65de63021f681862ae6d2ee1bddcb8729cfeb398b1660a252cb2e3e0f6e00438':
    raise SystemExit('Reviewed lifetime fixture changed')
if sha(makefile) != '170cdf5ce224242eb603b842ed2ca5849f913365a3477ea199b578c082bad619':
    raise SystemExit('Reviewed supplemental lifetime target changed')
paths = subprocess.check_output(
    ['git', 'ls-files', '-z', '--', 'src', 'Makefile', 'ios'], text=True).split('\0')
paths.extend([str(fixture), str(makefile)])
sources = {name: sha(name) for name in sorted(set(paths)) if name and Path(name).is_file()}
manifest = out / 'compiled-source-sha256.json'
manifest.write_text(json.dumps(sources, sort_keys=True, indent=2) + '\n')
started = time.monotonic()
summary = {
    'startedUTC': utc(), 'phase': args.phase, 'fixtureVersion': 2,
    'scopeChange': 'The initial network template must remain ordinary. Detached writer ownership checks are retained; the v1 fixture and every v1 receipt remain immutable.',
    'sourceBaseCommit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
    'compiledSourceManifest': str(manifest.relative_to(root)),
    'compiledSourceManifestSHA256': sha(manifest),
    'fixtureSHA256': sha(fixture), 'supplementalMakefileSHA256': sha(makefile),
    'scope': '24 real V8 realm teardown cycles; project regression, not WPT or GUI performance',
    'cpuDefinition': 'One-second process-tree CPU deltas; 100% is one logical core.',
    'rssDefinition': 'External process-tree RSS plus macOS task_info current RSS at each realm teardown; warm-up and allocator retention can affect RSS.',
    'stages': [], 'artifacts': {}, 'passed': False, 'expectedFailureObserved': False,
}

def save():
    temporary = out / 'summary.json.tmp'
    temporary.write_text(json.dumps(summary, indent=2) + '\n')
    temporary.replace(out / 'summary.json')

def measured(name, command, limit):
    prefix = out / name
    began = time.monotonic()
    run = subprocess.run(
        [sys.executable, 'tools/monitor_process.py', '--duration', str(limit), '--settle', '0',
         '--output', str(prefix.relative_to(root)), '--', *command],
        capture_output=True, text=True, timeout=limit + 35)
    (out / (name + '-collector.log')).write_text(run.stdout + run.stderr)
    meta = json.loads(prefix.with_suffix('.json').read_text())
    log = prefix.with_suffix('.log').read_text(errors='replace')
    row = {
        'name': name, 'command': command, 'durationSeconds': round(time.monotonic() - began, 3),
        'monitor': str(prefix.with_suffix('.json').relative_to(root)),
        'monitorSHA256': sha(prefix.with_suffix('.json')),
        'log': str(prefix.with_suffix('.log').relative_to(root)),
        'logSHA256': sha(prefix.with_suffix('.log')),
        'naturalExitCode': meta.get('natural_exit_code'),
        'collectorError': meta.get('collector_error'), 'endedByMonitor': meta.get('ended_by_monitor'),
        'cpuSamples': meta.get('interval_cpu_samples'),
        'cpuPeakPercent': meta.get('interval_cpu_peak_percent'),
        'rssPeakMiB': meta.get('rss_peak_mb'),
        'maximumSamplingGapSeconds': meta.get('max_sampling_gap_s'),
    }
    row['completedNaturally'] = not row['collectorError'] and not row['endedByMonitor'] and row['naturalExitCode'] is not None
    summary['stages'].append(row)
    save()
    return row, log

save()
try:
    if args.candidate_summary:
        candidate_path = (root / args.candidate_summary).resolve()
        candidate_path.relative_to(base)
        candidate = json.loads(candidate_path.read_text())
        if not candidate.get('passed'):
            raise RuntimeError('Combined DSD candidate is not GREEN')
        candidate_manifest = Path(candidate['compiledSourceManifest'])
        expected_manifest_sha = candidate.get('compiledSourceManifestSHA256', candidate.get('sourceManifestSHA256'))
        if sha(candidate_manifest) != expected_manifest_sha:
            raise RuntimeError('Candidate source manifest changed')
        for name, expected in json.loads(candidate_manifest.read_text()).items():
            if name.startswith('src/') or name == 'Makefile':
                if name not in sources or sources[name] != expected:
                    raise RuntimeError('Lifetime source differs from candidate: ' + name)
        artifact = candidate['artifacts']['lumen']
        if sha(artifact['path']) != artifact['sha256']:
            raise RuntimeError('Candidate native bytes changed')
        summary['candidate'] = {'summary': str(candidate_path.relative_to(root)),
                                'summarySHA256': sha(candidate_path), 'artifact': artifact}
    build_row, _ = measured('build', [
        'make', '-j3', '-f', 'Makefile', '-f', str(makefile),
        'BUILD=' + build, 'MODE=release', 'RTC=1', 'WT=1',
        build + '/test_declarative_shadow_lifetime_v2'], 180)
    if not build_row['completedNaturally'] or build_row['naturalExitCode'] != 0:
        raise RuntimeError('Lifetime regression build failed')
    binary = root / build / 'test_declarative_shadow_lifetime_v2'
    digest = sha(binary)
    snapshot = out / ('test_declarative_shadow_lifetime_v2-' + digest[:16])
    shutil.copy2(binary, snapshot)
    snapshot.chmod(0o555)
    if sha(snapshot) != digest:
        raise RuntimeError('Lifetime immutable snapshot differs')
    summary['artifacts']['lifetime'] = {'path': str(snapshot.relative_to(root)),
                                      'sha256': digest, 'bytes': snapshot.stat().st_size}
    save()
    row, log = measured('lifetime', [str(snapshot)], 60)
    cycles = [json.loads(line.split('DSD_LIFETIME_CYCLE ', 1)[1]) for line in log.splitlines() if 'DSD_LIFETIME_CYCLE ' in line]
    totals = [json.loads(line.split('DSD_LIFETIME_RESULTS ', 1)[1]) for line in log.splitlines() if 'DSD_LIFETIME_RESULTS ' in line]
    if len(cycles) != 24 or [r['cycle'] for r in cycles] != list(range(24)) or len(totals) != 1:
        raise RuntimeError('Incomplete real-context lifetime observations')
    total = totals[0]
    if total['cycles'] != 24 or total['checks'] != 122 or total['passed'] + total['failed'] != 122:
        raise RuntimeError('Lifetime result counts differ from the reviewed fixture')
    node_baselines = all(r['liveNodesAfterTeardown'] == r['baselineNodes'] == total['baselineNodes'] for r in cycles)
    rss = [r['rssAfterTeardownMiB'] for r in cycles]
    if any(value < 0 for value in rss):
        raise RuntimeError('Native current-RSS telemetry was unavailable')
    warm = statistics.median(rss[4:8])
    final = statistics.median(rss[-4:])
    summary['observed'] = {
        'result': total, 'cycleCount': len(cycles), 'allPostTeardownNodeBaselinesExact': node_baselines,
        'warmCycles4To7RSSMedianMiB': warm, 'finalFourCyclesRSSMedianMiB': final,
        'settledRSSChangeMiB': round(final - warm, 6),
        'maximumPostTeardownRSSMiB': max(rss),
        'maximumLiveNodesBeforeTeardown': max(r['liveNodesBeforeTeardown'] for r in cycles),
        'cycles': cycles,
    }
    for name, expected in sources.items():
        if sha(name) != expected:
            raise RuntimeError('Source changed during lifetime build/run: ' + name)
    if args.phase.startswith('red'):
        summary['expectedFailureObserved'] = bool(row['completedNaturally'] and row['naturalExitCode'] == 1 and total['failed'] >= 24 and node_baselines)
        if not summary['expectedFailureObserved']:
            raise RuntimeError('Expected functional RED was not established cleanly')
    else:
        summary['passed'] = bool(row['completedNaturally'] and row['naturalExitCode'] == 0 and total['failed'] == 0 and node_baselines)
        if not summary['passed']:
            raise RuntimeError('DSD lifetime correctness or native node cleanup failed')
except Exception as error:
    summary['error'] = type(error).__name__ + ': ' + str(error)
finally:
    summary.update(finishedUTC=utc(), durationSeconds=round(time.monotonic() - started, 3))
    save()
    print(json.dumps({'summary': str((out / 'summary.json').relative_to(root)),
                      'passed': summary['passed'], 'expectedFailureObserved': summary['expectedFailureObserved'],
                      'durationSeconds': summary['durationSeconds'], 'error': summary.get('error')}), flush=True)
raise SystemExit(0 if summary['passed'] or summary['expectedFailureObserved'] else 1)
