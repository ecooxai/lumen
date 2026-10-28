#!/usr/bin/env python3
"""Recover only the observed iPad004 app whose SIGTERM cleanup failed.

This is environment recovery, not a passing teardown result. The original WPT
row, report, logs, and retained app profile remain unchanged. It neither starts
another test nor changes the adapter's policy.
"""
import argparse
import fcntl
import hashlib
import importlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

ROOT = Path('/Users/devin/repos/lumen')
DEVICE = 'A77F68E4-972B-472B-80B8-F65E71D361E5'
BUNDLE = 'com.ecooxai.lumen'
GENERATION = ROOT / 'output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-004'
FAILED = GENERATION / 'runtime-smoke-001/http'
ROW_SHA = 'd9e551cdc33fba92361b742176d43e2c17951d69df91d90105baae2f9a1871cd'
BINARY_SHA = 'b6bfb5ffa7ec86c144e4c49842ac911b60f40631d8a06fa87147e20333c0cd97'
MONITOR_SHA = '36a0194c2d71c484841b31887773bf02f6f9e95d519132aeb5ee211ca3dde98a'
PINS = {
    'ipad_runner.py': '1876472b3552a425f9539ef9bb638cf86a6e006c3bc5f6ca603eb8305a39c4ba',
    'ipad_adapter.py': '0a45a31740340bf321c7add11dcaa1ffca8a63de44261ff7290f2a8413258c34',
    'full_runner.py': 'a7bc96c3697fb099ad63c145011bc434a66d865a713a62e5198731bed9600ac7',
    'ipad_generation.py': '38d6f962059e932e12a158ae94df37ca289f434380e6478b1ede856b31fafb73',
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def canonical(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def identity(path):
    value = Path(path).stat()
    return [value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_ctime_ns]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=GENERATION / 'owned-app-recovery-001')
    args = parser.parse_args()
    require(platform.system() == 'Darwin' and Path.cwd().resolve() == ROOT,
            'Run only in the authorized Mac checkout through devinmac3d')
    source = ROOT / 'tests/wpt-ipad004'
    for name, expected in PINS.items():
        require(digest(source / name) == expected, 'Reviewed iPad source changed: ' + name)
    monitor = ROOT / 'tools/monitor_process.py'
    require(digest(monitor) == MONITOR_SHA, 'Reviewed process monitor changed')
    sys.path.insert(0, str(source))
    core = importlib.import_module('full_runner')
    adapter_module = importlib.import_module('ipad_adapter')
    entry = importlib.import_module('ipad_runner')
    generation = importlib.import_module('ipad_generation')
    require(adapter_module.AUTHORIZED_DEVICE == DEVICE and adapter_module.AUTHORIZED_BUNDLE == BUNDLE,
            'Exact authorized target differs')
    output = args.output.resolve()
    require(output.is_relative_to(GENERATION) and not output.exists(), 'Use a fresh generation004 recovery directory')
    output.mkdir(parents=True, exist_ok=False)
    report = {'status': 'preparing', 'device': DEVICE, 'bundle_id': BUNDLE,
              'started_unix_ms': round(time.time() * 1000),
              'original_test_cleanup_passed': False, 'performance_release_gate_passed': False,
              'classification': 'recovery_after_recorded_sigterm_failure',
              'scope': 'Exact iPad004/bundle only. No test row, profile, simulator data, source, or other process is changed.',
              'steps': []}
    evidence = {}
    sampler = None

    def save():
        core.write_json(output / 'recovery.json', report)

    def load(path):
        path = Path(path)
        require(not path.is_symlink() and path.resolve(strict=True).is_relative_to(GENERATION)
                and path.stat().st_size <= 4 * 1024 * 1024, 'Unexpected recovery input')
        before = identity(path)
        data = path.read_bytes()
        require(identity(path) == before, 'Recovery input changed during read')
        evidence[str(path)] = {'identity': before, 'sha256': hashlib.sha256(data).hexdigest()}
        return data

    def unchanged():
        for path, record in evidence.items():
            require(identity(path) == record['identity'] and digest(path) == record['sha256'],
                    'Original test/recovery evidence changed: ' + path)

    save()
    code = 1
    try:
        with (ROOT / 'output/wpt' / ('ipad-' + DEVICE + '.lock')).open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            run = json.loads(load(FAILED / 'run.json'))
            lines = load(FAILED / 'results.jsonl').splitlines()
            require(len(lines) == 1, 'This recovery is bound to the single observed failed-cleanup row')
            row = json.loads(lines[0])
            require(canonical(row) == ROW_SHA and run.get('invocation_open') is False
                    and run.get('finished_schedule') is False and run.get('completed_result_rows') == 1,
                    'The exact closed failed-cleanup invocation changed')
            require(row['cleanup'].get('stopped') is False and row['cleanup'].get('sent_sigterm') is True
                    and row['cleanup'].get('error') == 'Owned iPad app did not terminate within the bounded cleanup window',
                    'Original failed teardown evidence differs')
            expected = row['launch_identity']
            require(expected['pid'] == 92925 and expected['start'] == 'Thu Oct 22 13:09:04 2026',
                    'This helper is scoped to the observed owned app identity')
            report['original_row_sha256'] = ROW_SHA
            report['original_cleanup'] = row['cleanup']
            report['expected_launch_identity'] = expected
            report['generation_binding'] = generation.verify_generation(ROOT)
            adapter = adapter_module.IpadAdapter(DEVICE, BUNDLE, runtime=core)
            require(str(adapter.binary) == expected['executable']
                    and adapter.metadata['installed_binary_sha256'] == BINARY_SHA
                    and run['signature']['binary_sha256'] == BINARY_SHA,
                    'Installed app no longer matches the exact failed launch')
            proof, guards = entry.verify_signed_bundles(ROOT / 'build-sim/Lumen.app', adapter.app)
            require(proof['matching_bundle_contents_sha256'] == run['signature']['signed_bundle_provenance']['matching_bundle_contents_sha256'],
                    'The signed app bundle no longer matches the failed run')
            profile = Path(row['cleanup']['retained_profile'])
            require(profile.parent == adapter.temp_root and profile.name.startswith('lumen-wpt-')
                    and profile.is_dir() and not profile.is_symlink(), 'Retained failed-case profile changed')
            report['retained_profile_preserved'] = str(profile)
            current = adapter_module.process_identity(expected['pid'])
            running = adapter._running_instances()
            report['before'] = {'identity': current, 'matching_installed_app_pids': running}
            save()
            if current != expected:
                require(not running, 'A different app instance exists; no recovery action is authorized for it')
                report['status'] = 'original_app_already_gone_no_termination_requested'
                code = 0
            else:
                require(running == [expected['pid']], 'Unexpected additional app instance; refuse bundle-scoped termination')
                entry.check_bundle_guards(guards)
                unchanged()
                require(adapter_module.process_identity(expected['pid']) == expected
                        and adapter._running_instances() == [expected['pid']],
                        'App ownership changed immediately before termination')
                report['phase'] = 'request-platform-termination'
                save()
                sampler = core.Sampler(expected['pid'], output / 'recovery-app.metrics.jsonl')
                command = ['xcrun', 'simctl', 'terminate', DEVICE, BUNDLE]
                prefix = output / 'terminate-request'
                invocation = [sys.executable, str(monitor), '--duration', '20', '--settle', '0',
                              '--output', str(prefix), '--', *command]
                completed = subprocess.run(invocation, cwd=ROOT, check=False)
                measured = json.loads(prefix.with_suffix('.json').read_text())
                step = {'command': command, 'monitor_exit_code': completed.returncode,
                        'monitor_report': str(prefix.with_suffix('.json')), 'measurement': measured}
                report['steps'].append(step)
                save()
                unsafe = [value for value in measured.get('violations', []) if value != 'no process samples']
                require(completed.returncode in (0, 1) and measured.get('natural_exit_code') == 0
                        and measured.get('ended_by_monitor') is False and not measured.get('collector_error')
                        and not measured.get('owned_group_cleanup', {}).get('error') and not unsafe,
                        'Platform termination request failed; preserve its monitored evidence')
                began = time.monotonic()
                while time.monotonic() - began < 10:
                    current = adapter_module.process_identity(expected['pid'])
                    running = adapter._running_instances()
                    if current != expected and not running:
                        break
                    time.sleep(.1)
                report['after'] = {'identity': current, 'matching_installed_app_pids': running,
                                   'verification_elapsed_s': round(time.monotonic() - began, 3)}
                require(current != expected and not running, 'The exact owned app did not terminate after the platform request')
                report['status'] = 'recovered_owned_app_gone_original_cleanup_remains_failed'
                code = 0
            entry.check_bundle_guards(guards)
            unchanged()
    except Exception as error:
        report.update(status='stopped', error=type(error).__name__ + ': ' + str(error))
        code = 1
    finally:
        if sampler is not None:
            try:
                report['recovery_app_metrics'] = sampler.stop()
                if report['recovery_app_metrics'].get('errors'):
                    report['collector_failure'] = True
                    code = 1
            except Exception as error:
                report['collector_failure'] = type(error).__name__ + ': ' + str(error)
                code = 1
        report['original_evidence'] = evidence
        report['finished_unix_ms'] = round(time.time() * 1000)
        save()
    print(json.dumps({'status': report['status'], 'report': str(output / 'recovery.json'),
                      'original_test_cleanup_passed': False, 'error': report.get('error')}))
    return code


if __name__ == '__main__':
    raise SystemExit(main())
