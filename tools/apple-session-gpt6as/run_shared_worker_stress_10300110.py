"""Build and measure a frozen candidate's real SharedWorker lifecycle oracle."""
from datetime import datetime, timezone
from pathlib import Path
import argparse
import csv
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import sys
import time


def sha(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-summary', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--kind', choices=('shared', 'transferred'), default='shared',
                        help='Separate basic owner lifetime or transferred last-client lifetime oracle')
    args = parser.parse_args()
    root = Path.cwd().resolve()
    if sys.platform != 'darwin' or str(root) != '/Users/devin/repos/lumen':
        parser.error('Run from the canonical native Mac repository')
    product_path = args.build_summary.resolve()
    product_path.relative_to(root)
    product = json.loads(product_path.read_text())
    if not product.get('passed'):
        parser.error('The native candidate build/regression gate must pass first')
    manifest = product_path.parent / 'compiled-source-sha256.json'
    if sha(manifest) != product['sourceManifestSHA256']:
        parser.error('The compiled source manifest differs')
    sources = json.loads(manifest.read_text())
    for name, digest in sources.items():
        if sha(root / name) != digest:
            parser.error('The currently compiled source differs: ' + name)
    out = args.output.resolve()
    out.relative_to(root / 'out')
    out.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    env = {name: value for name, value in os.environ.items() if not name.startswith('LUMEN_')}
    env['DEVELOPER_DIR'] = '/Library/Developer/CommandLineTools'
    profile = out / 'profile'
    profile.mkdir()
    env['LUMEN_PROFILE_DIR'] = str(profile)
    transferred = args.kind == 'transferred'
    target = 'test_shared_worker_transfer_stress' if transferred else 'test_shared_worker_lifetime_stress'
    result_label = 'shared-worker-transfer-stress' if transferred else 'shared-worker-lifetime-stress'
    report = {'startedUTC': datetime.now(timezone.utc).isoformat(), 'passed': False, 'kind': args.kind,
              'scope': 'Actual native SharedWorker isolate/owner/port lifecycle; not official WPT or GUI scrolling',
              'carrierSHA256': sha(Path(__file__)),
              'buildSummary': str(product_path.relative_to(root)), 'buildSummarySHA256': sha(product_path),
              'sourceManifestSHA256': sha(manifest), 'stages': [], 'cyclesRequired': 100,
              'limits': {'threadDescriptorSettlingMilliseconds': 500, 'warmRSSGrowthMiB': 64,
                         'postWorkIdleP95OneCoreCPUPercent': 3}, 'productArtifacts': product['artifacts']}

    def save():
        temporary = out / 'summary.json.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(out / 'summary.json')

    def measured(name, command, limit):
        prefix = out / name
        result = subprocess.run([sys.executable, 'tools/monitor_process.py', '--duration', str(limit),
                                 '--settle', '0', '--output', str(prefix.relative_to(root)), '--', *command],
                                env=env, capture_output=True, text=True, timeout=limit+35)
        (out / (name+'-collector.log')).write_text(result.stdout+result.stderr)
        value = json.loads(prefix.with_suffix('.json').read_text())
        passed = (value.get('natural_exit_code') == 0 and not value.get('collector_error')
                  and not value.get('ended_by_monitor') and not value.get('owned_group_cleanup', {}).get('error'))
        report['stages'].append({'name': name, 'passed': passed, 'durationSeconds': value['duration_s'],
            'monitor': str(prefix.with_suffix('.json').relative_to(root)), 'monitorSHA256': sha(prefix.with_suffix('.json')),
            'logSHA256': sha(prefix.with_suffix('.log')), 'rssPeakMiB': value['rss_peak_mb'],
            'intervalCPU_P95Percent': value['steady_interval_cpu_p95_percent'], 'intervalCPU_PeakPercent': value['interval_cpu_peak_percent'],
            'maxSamplingGapSeconds': value['max_sampling_gap_s'], 'cpuUnit': value['cpu_unit']})
        save()
        if not passed:
            raise RuntimeError(name+' failed; raw records preserved')
        return value

    try:
        fixture = root / ('tests/' + target + '.cc')
        report['fixtureSHA256'] = sha(fixture)
        report['importedDedicatedFixtureSHA256'] = sha(root / 'tests/test_worker_async_lifecycle.cc')
        build = product['buildCache']
        if not re.fullmatch(r'[A-Za-z0-9_./-]+', build):
            raise ValueError('Invalid candidate build-cache path')
        fragment = out / 'shared-stress.mk'
        fragment.write_text(
            f'$(BUILD)/tests/{target}.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG -I$(BUILD)/src/js\n'
            f'$(BUILD)/tests/{target}.cc.o: $(BUILD)/tests/test_worker_async_lifecycle.cc.o\n'
            f'$(BUILD)/{target}: $(call OBJ,tests/{target}.cc $(CORE_SRC) $(filter-out src/js/worker.cc,$(JS_SRC))) $(V8LINK_RSP)\n'
            '\t$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS) -lproc\n')
        measured('build', ['make', '-j1', '-f', 'Makefile', '-f', str(fragment), 'BUILD='+build,
                          'MODE=release', 'RTC=1', 'WT=1', build+'/'+target], 180)
        source = root / build / target
        digest = sha(source)
        binary = out / ('shared-worker-stress-'+digest[:16])
        shutil.copy2(source, binary); binary.chmod(0o555)
        if sha(binary) != digest:
            raise RuntimeError('Frozen stress executable copy differs')
        report['stressBinary'] = {'path': str(binary.relative_to(root)), 'sha256': digest}
        monitor = measured('run', [str(binary), '100', str(out / 'journal.jsonl')], 240)
        log = (out / 'run.log').read_text()
        matches = re.findall(r'RESULTS ' + result_label + r' PASS \((\d+)/(\d+) checks\)', log)
        journal = [json.loads(line) for line in (out / 'journal.jsonl').read_text().splitlines()]
        console = [json.loads(line.split(' ',1)[1]) for line in log.splitlines() if line.startswith('SHARED_RESOURCE_CHECKPOINT ')]
        if (len(matches) != 1 or matches[0][0] != matches[0][1]
                or len(journal) != 101 or journal != console
                or [row['cycle'] for row in journal] != list(range(1,102))
                or any(row['failures'] or row['workers'] != (1 if transferred and row['cycle'] <= 100 else 0)
                       or row['clients'] or row['pending'] for row in journal)):
            raise RuntimeError('Shared lifecycle journal, console, or result is incomplete')
        warm, final = journal[3], journal[-1]
        report['resources'] = {'warmCycle': 4, 'warmRSSMiB': warm['rss_kib']/1024,
            'finalRSSMiB': final['rss_kib']/1024, 'warmToFinalRSSMiB': (final['rss_kib']-warm['rss_kib'])/1024,
            'warmThreads': warm['threads'], 'finalThreads': final['threads'],
            'warmFDs': warm['fds'], 'finalFDs': final['fds'], 'journalRecords': len(journal),
            'journalSHA256': sha(out / 'journal.jsonl'), 'checkpoints': journal}
        with (out / 'run.csv').open() as stream:
            samples = list(csv.DictReader(stream))
        end = monitor['monitoring_duration_s']
        idle = [float(row['interval_cpu_percent']) for row in samples if row.get('interval_cpu_percent')
                and end-10 <= float(row['elapsed_s']) <= end-1]
        if len(idle)<8 or monitor['max_sampling_gap_s']>1.5:
            raise RuntimeError('Post-work one-second idle sampling is incomplete')
        p95 = sorted(idle)[math.ceil(0.95*len(idle))-1]
        report['postWorkIdle'] = {'p95OneCoreCPUPercent': p95, 'samples': len(idle), 'peakOneCoreCPUPercent': max(idle),
                                  'scope': 'Final nine seconds of the declared 12-second native fixture idle interval'}
        if p95>3 or final['threads']>warm['threads'] or final['fds']>warm['fds'] or final['rss_kib']-warm['rss_kib']>64*1024:
            raise RuntimeError('Declared shared lifecycle resource limits were exceeded')
        for name, expected in sources.items():
            if sha(root / name) != expected:
                raise RuntimeError('Source changed while the native stress oracle ran')
        for artifact in product['artifacts'].values():
            if sha(root / artifact['path']) != artifact['sha256']:
                raise RuntimeError('A frozen candidate artifact changed')
        if (sha(fixture) != report['fixtureSHA256']
                or sha(root / 'tests/test_worker_async_lifecycle.cc') != report['importedDedicatedFixtureSHA256']
                or sha(Path(__file__)) != report['carrierSHA256']):
            raise RuntimeError('A lifecycle fixture changed during the measured run')
        report.update(passed=True, cyclesCompleted=100, realWorkerIsolatesCreated=201 if transferred else 200,
                      checksPassed=int(matches[0][0]), checksTotal=int(matches[0][1]))
    except Exception as error:
        report['error'] = type(error).__name__+': '+str(error)
    finally:
        report.update(finishedUTC=datetime.now(timezone.utc).isoformat(), durationSeconds=round(time.monotonic()-started,3))
        save()
        print(json.dumps({'passed': report['passed'], 'summary': str((out/'summary.json').relative_to(root)),
                          'cyclesCompleted': report.get('cyclesCompleted'), 'error': report.get('error'),
                          'durationSeconds': report['durationSeconds']}))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
