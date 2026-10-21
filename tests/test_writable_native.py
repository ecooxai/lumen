#!/usr/bin/env python3
"""Rebuild actual Lumen targets and require completed native stream assertions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'out/lab-streams'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sample(root_pid):
    rows = subprocess.check_output(['ps', '-axo', 'pid=,ppid=,pcpu=,rss='], text=True, timeout=3, cwd='/')
    processes = {}
    for row in rows.splitlines():
        fields = row.split()
        if len(fields) == 4:
            try: processes[int(fields[0])] = (int(fields[1]), float(fields[2]), int(fields[3]))
            except ValueError: pass
    selected = {root_pid}
    while True:
        more = {pid for pid, info in processes.items() if info[0] in selected}
        if more <= selected: break
        selected |= more
    values = [processes[pid] for pid in selected if pid in processes]
    return {'cpuPercentPS': round(sum(row[1] for row in values), 2), 'rssKiB': sum(row[2] for row in values), 'processCount': len(values)}


def run(command, label, limit, environment=None):
    started = time.monotonic()
    result = {'command': command, 'label': label, 'samples': [], 'timedOut': False, 'memoryLimitExceeded': False, 'observerFailed': False}
    path = OUTPUT / (label + '.log')
    child = None
    with path.open('w') as log:
        try:
            child = subprocess.Popen(command, cwd=ROOT, env=environment, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            result['pid'] = child.pid
            while child.poll() is None:
                current = sample(child.pid)
                current['elapsedSeconds'] = round(time.monotonic() - started, 3)
                result['samples'].append(current)
                result['timedOut'] = current['elapsedSeconds'] > limit
                result['memoryLimitExceeded'] = current['rssKiB'] > 4096 * 1024
                if result['timedOut'] or result['memoryLimitExceeded']: break
                time.sleep(1)
        except (OSError, ValueError, subprocess.SubprocessError):
            result['observerFailed'] = True
        finally:
            if child is not None and child.poll() is None:
                try: os.killpg(child.pid, signal.SIGTERM)
                except ProcessLookupError: pass
                try: child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    try: os.killpg(child.pid, signal.SIGKILL)
                    except ProcessLookupError: pass
                    child.wait(timeout=5)
            result['exitCode'] = child.wait() if child is not None else None
    result['seconds'] = round(time.monotonic() - started, 3)
    result['processExited'] = child is None or child.poll() is not None
    result['peakRSSMiB'] = round(max((row['rssKiB'] for row in result['samples']), default=0) / 1024, 2)
    result['peakCPUPercentPS'] = max((row['cpuPercentPS'] for row in result['samples']), default=0)
    result['log'] = str(path.relative_to(ROOT))
    result['pass'] = result['exitCode'] == 0 and not any(result[name] for name in ['timedOut', 'memoryLimitExceeded', 'observerFailed'])
    return result, path.read_text()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--reuse-built-report', type=Path, help='Reuse only a successful recorded build whose complete source/binary fingerprint still matches.')
    args = parser.parse_args()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    report_path = OUTPUT / 'writable-native.json'
    reused = json.loads(args.reuse_built_report.read_text()) if args.reuse_built_report else None
    if report_path.exists():
        history = OUTPUT / 'history' / (str(time.time_ns()) + '-' + sha(report_path)[:8])
        history.mkdir(parents=True, exist_ok=False)
        previous = json.loads(report_path.read_text())
        shutil.copy2(report_path, history / report_path.name)
        for command in previous.get('commands', []):
            log = ROOT / command.get('log', '')
            if log.parent == OUTPUT and log.is_file(): shutil.copy2(log, history / log.name)
    report = {'schemaVersion': 1, 'running': True, 'pass': False, 'scope': 'actual Lumen build/jsrun stream fixtures; GUI rendering and WebTransport network support are separate', 'cpuMetric': 'summed owned-process OS ps CPU, sampled each second', 'commands': []}
    def save(): report_path.write_text(json.dumps(report, indent=2))
    save()
    scenario = ROOT / 'out/verification/monitor-scenario.txt'
    if scenario.parent.exists(): scenario.write_text('native-writable-stream-rebuild-and-fixtures\n')
    try:
        names = ['src/js/prelude/07-web.js', 'build/jsrun', 'build/lumen', 'tests/js/writable-backpressure.html', 'tests/js/streams.html']
        if reused is not None:
            previous_build = next((row for row in reused.get('commands', []) if row.get('command') == ['make', '-j2', 'RTC=1', 'build/jsrun', 'build/lumen']), None)
            current = {name: sha(ROOT / name) for name in names}
            if previous_build is None or previous_build.get('pass') is not True or reused.get('fingerprints') != current:
                raise RuntimeError('recorded-build-fingerprint-mismatch')
            report['reusedBuild'] = {'sourceReport': str(args.reuse_built_report), 'fingerprintsMatch': True, 'build': previous_build}
        else:
            build, _ = run(['make', '-j2', 'RTC=1', 'build/jsrun', 'build/lumen'], 'writable-native-build', 240)
            report['commands'].append(build); save()
            if not build['pass']: raise RuntimeError('native-build-failed')
        report['fingerprints'] = {name: sha(ROOT / name) for name in names}
        environment = os.environ.copy()
        environment['LUMEN_CONSOLE'] = '1'
        environment['JSRUN_DUMP'] = '1'
        environment['JSRUN_UNTIL_ATTR'] = 'data-writable-complete'
        probe, output = run(['build/jsrun', 'tests/js/writable-backpressure.html', '5000'], 'writable-native-lifecycle', 15, environment)
        report['commands'].append(probe); save()
        match = re.search(r'RESULTS writable-backpressure (\{[^\n]*\})', output)
        if not probe['pass'] or match is None: raise RuntimeError('native-lifecycle-completion-missing')
        actual = json.loads(match.group(1))
        expected = {'weighted-backpressure', 'released-writer', 'error-with-undefined', 'abort-in-flight', 'pipe-abort-uses-locked-writer'}
        report['nativeLifecycle'] = actual
        if actual.get('total') != 5 or actual.get('passed') != 5 or set(actual.get('results', {})) != expected or any(value != 'PASS' for value in actual['results'].values()):
            raise RuntimeError('native-lifecycle-assertion-failed')
        environment.pop('JSRUN_UNTIL_ATTR', None)
        regression, output = run(['build/jsrun', 'tests/js/streams.html', '5000'], 'writable-native-existing-streams', 15, environment)
        report['commands'].append(regression); save()
        match = re.search(r'RESULTS streams ([^\n]+)', output)
        if not regression['pass'] or match is None: raise RuntimeError('existing-streams-completion-missing')
        cases = dict(item.split(':', 1) for item in match.group(1).strip().split())
        expected = {'pipeTo', 'pipeThrough', 'pull', 'tee', 'textstreams', 'closed', 'response.body'}
        report['existingStreams'] = cases
        if set(cases) != expected or any(value != 'PASS' for value in cases.values()): raise RuntimeError('existing-streams-assertion-failed')
        report['pass'] = True
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as failure:
        report['failure'] = str(failure) if isinstance(failure, RuntimeError) else 'runner-or-evidence-error'
    finally:
        report['running'] = False
        report['finishedAtUnixSeconds'] = time.time()
        save()
        if scenario.parent.exists(): scenario.write_text('idle-between-scenarios\n')
    print(json.dumps({key: value for key, value in report.items() if key != 'commands'}))
    return 0 if report['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
