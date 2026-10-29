from pathlib import Path
from datetime import datetime, timezone
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument('--candidate-summary', required=True)
args = parser.parse_args()
root = Path.cwd().resolve()
out = root / 'out/lume-20261029-cpu30/native-dsd-adoption-2'
out.mkdir(parents=True, exist_ok=False)
utc = lambda: datetime.now(timezone.utc).isoformat()

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

candidate_path = (root / args.candidate_summary).resolve()
candidate_path.relative_to(root / 'out/lume-20261029-cpu30')
candidate = json.loads(candidate_path.read_text())
if not candidate.get('passed'):
    raise SystemExit('DSD combined candidate is not passing')
manifest = Path(candidate['compiledSourceManifest'])
manifest_sha = candidate.get('compiledSourceManifestSHA256', candidate.get('sourceManifestSHA256'))
if sha(manifest) != manifest_sha:
    raise SystemExit('Candidate compiled source manifest changed')
sources = {name: value for name, value in json.loads(manifest.read_text()).items()
           if name.startswith('src/') or name == 'Makefile'}
fixture = Path('tests/test_declarative_shadow_adoption_lifetime.cc')
makefile = Path('tools/apple-session-gpt6as/dsd_adoption_lifetime_cpu30.mk')
fixture_hashes = [{'path': 'tests/test_declarative_shadow_adoption_lifetime.cc', 'sha256': '5f8d947e6592742b472560017c2a08959551151fb69c3d1aa23934f6ce9b7b8e', 'bytes': 5929}, {'path': 'tools/apple-session-gpt6as/dsd_adoption_lifetime_cpu30.mk', 'sha256': '3abcdc51721c698fe896ce5bc162c6f470371c42ea2eb2b60f5ee43feea3efe7', 'bytes': 371}]
for row in fixture_hashes:
    sources[row['path']] = row['sha256']
for name, expected in sources.items():
    if sha(name) != expected:
        raise SystemExit('Adoption control source differs: ' + name)
artifact = candidate['artifacts']['lumen']
if sha(artifact['path']) != artifact['sha256']:
    raise SystemExit('DSD candidate native bytes changed')
started = time.monotonic()
summary = {
    'startedUTC': utc(), 'candidateSummary': str(candidate_path.relative_to(root)),
    'candidateSummarySHA256': sha(candidate_path), 'candidateArtifact': artifact,
    'compiledSourceManifest': str(manifest), 'compiledSourceManifestSHA256': manifest_sha,
    'fixtures': fixture_hashes,
    'scope': 'Eight real V8 realm teardown cycles, each with four retired-tree adoption dispositions; custom regression only.',
    'stages': [], 'passed': False,
}

def save():
    temporary = out / 'summary.json.tmp'
    temporary.write_text(json.dumps(summary, indent=2) + '\n')
    temporary.replace(out / 'summary.json')

def stage(name, command, duration):
    prefix = out / name
    began = time.monotonic()
    run = subprocess.run([sys.executable, 'tools/monitor_process.py', '--duration', str(duration),
                          '--settle', '0', '--output', str(prefix.relative_to(root)), '--', *command],
                         capture_output=True, text=True, timeout=duration + 35)
    (out / (name + '-collector.log')).write_text(run.stdout + run.stderr)
    meta = json.loads(prefix.with_suffix('.json').read_text())
    log = prefix.with_suffix('.log').read_text(errors='replace')
    row = {'name': name, 'command': command, 'durationSeconds': round(time.monotonic() - began, 3),
           'naturalExitCode': meta.get('natural_exit_code'), 'collectorError': meta.get('collector_error'),
           'endedByMonitor': meta.get('ended_by_monitor'), 'cpuSamples': meta.get('interval_cpu_samples'),
           'cpuPeakPercent': meta.get('interval_cpu_peak_percent'), 'rssPeakMiB': meta.get('rss_peak_mb'),
           'maximumSamplingGapSeconds': meta.get('max_sampling_gap_s'),
           'monitor': str(prefix.with_suffix('.json').relative_to(root)),
           'monitorSHA256': sha(prefix.with_suffix('.json')),
           'log': str(prefix.with_suffix('.log').relative_to(root)),
           'logSHA256': sha(prefix.with_suffix('.log'))}
    row['passed'] = row['naturalExitCode'] == 0 and not row['collectorError'] and not row['endedByMonitor']
    summary['stages'].append(row)
    save()
    if not row['passed']:
        raise RuntimeError(name + ' failed; retained log must be inspected')
    return log

save()
try:
    build = 'build-apple-load-20261029'
    stage('build', ['make', '-j3', '-f', 'Makefile', '-f', str(makefile),
                    'BUILD=' + build, 'MODE=release', 'RTC=1', 'WT=1',
                    build + '/test_declarative_shadow_adoption_lifetime'], 120)
    binary = root / build / 'test_declarative_shadow_adoption_lifetime'
    digest = sha(binary)
    snapshot = out / ('test_declarative_shadow_adoption_lifetime-' + digest[:16])
    shutil.copy2(binary, snapshot)
    snapshot.chmod(0o555)
    if sha(snapshot) != digest:
        raise RuntimeError('Immutable adoption control snapshot differs')
    summary['testArtifact'] = {'path': str(snapshot.relative_to(root)), 'sha256': digest,
                               'bytes': snapshot.stat().st_size}
    save()
    log = stage('adoption', [str(snapshot)], 30)
    rows = [json.loads(line.split('DSD_ADOPTION_CYCLE ', 1)[1]) for line in log.splitlines() if 'DSD_ADOPTION_CYCLE ' in line]
    totals = [json.loads(line.split('DSD_ADOPTION_RESULTS ', 1)[1]) for line in log.splitlines() if 'DSD_ADOPTION_RESULTS ' in line]
    if len(rows) != 8 or [row['cycle'] for row in rows] != list(range(8)) or len(totals) != 1:
        raise RuntimeError('Adoption control cycle evidence is incomplete')
    total = totals[0]
    if [total[k] for k in ['cycles', 'dispositionsPerCycle', 'checks', 'passed', 'failed']] != [8, 4, 34, 34, 0]:
        raise RuntimeError('Adoption control did not pass every reviewed check')
    if not all(row['afterTeardown'] == row['baselineNodes'] == total['baselineNodes'] for row in rows):
        raise RuntimeError('Retired adopted trees left native node residue')
    for name, expected in sources.items():
        if sha(name) != expected:
            raise RuntimeError('Source changed during adoption control: ' + name)
    summary['result'] = total
    summary['cycles'] = rows
    summary['passed'] = True
except Exception as error:
    summary['error'] = type(error).__name__ + ': ' + str(error)
finally:
    summary.update(finishedUTC=utc(), durationSeconds=round(time.monotonic() - started, 3))
    save()
    print(json.dumps({'summary': str((out / 'summary.json').relative_to(root)),
                      'passed': summary['passed'], 'durationSeconds': summary['durationSeconds'],
                      'error': summary.get('error')}), flush=True)
raise SystemExit(0 if summary['passed'] else 1)
