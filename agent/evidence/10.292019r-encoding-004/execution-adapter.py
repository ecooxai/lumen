#!/usr/bin/env python3
"""Explicit Linux headless WPT adapter for the user's authorized local fallback.

The frozen upstream schedule, custom reporter, collector, resource sampler and
owned-process cleanup are reused from the existing adapter. Linux jsrun is a
different execution mode from the historical Mac app: it gets an explicit
runtime beyond the unchanged external15/65-second watchdog. No platform value
is spoofed and no historical Mac result is relabeled as current evidence.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
PIN = '7f8d49b5a82606efc771f43d10aad5bce5ccbfeb'
FROZEN = 'adcbf9719f722368b1e53ac1bd61cf38e566d3bd7495d9d6127cad38dd6aeabe'


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def read(path):
    return json.loads(Path(path).read_text())


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def save(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def utc():
    return datetime.now(timezone.utc).isoformat()


def load_helpers():
    spec = importlib.util.spec_from_file_location('lumen_wpt_common', ROOT / 'tests/wpt/full_runner.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def adapter_failure(collector, error):
    message = type(error).__name__ + ': ' + str(error)
    result = collector.result({'status': 'INFRA_ERROR', 'message': message})
    if collector.done:
        result['original_harness_status'] = collector.done.get('status')
    # A valid harness completion must not conceal a later adapter failure.
    result.update(status='INFRA_ERROR', message=message, adapter_error=message)
    result.setdefault('telemetry_errors', []).append('Linux execution adapter failed: ' + message)
    return result


def run_native(common, binary, url, timeout, prefix, environment):
    error = common.monitor_module().pid_namespace_error()
    if error:
        raise RuntimeError(error)
    collector = common.Collector()
    profile = Path(tempfile.mkdtemp(prefix='lumen-linux-case-', dir=prefix.parent))
    download = profile / 'downloads'
    download.mkdir()
    env = dict(environment)
    for name in tuple(env):
        if name.startswith('JSRUN_') or name in ('LUMEN_SHOT', 'LUMEN_SHOT_MS', 'LUMEN_NO_JS', 'LUMEN_PRE_FILE'):
            env.pop(name, None)
    env.update(LUMEN_CONSOLE='1', LUMEN_PROFILE_DIR=str(profile), LUMEN_DOWNLOAD_DIR=str(download))
    started = time.monotonic()
    sampler = None
    process = None
    result = None
    # The outer watchdog owns test duration. The harness's default1s lifetime
    # must never truncate asynchronous upstream tests.
    command = [str(binary), url, str(int((timeout + 5) * 1000))]
    try:
        with prefix.with_suffix('.console.log').open('wb') as log:
            process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            sampler = common.Sampler(process.pid, prefix.with_suffix('.metrics.jsonl'))
            with prefix.with_suffix('.console.log').open(errors='replace') as source:
                carry = ''
                while time.monotonic() - started < timeout:
                    carry += source.read()
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
            fallback = {'status': 'CRASH' if process.poll() not in (None, 0)
                        else 'ERROR' if process.poll() == 0 else 'TIMEOUT',
                        'message': 'Headless engine exited before harness completion'
                        if process.poll() is not None else 'External file watchdog expired'}
            result = collector.result(fallback)
            result['natural_exit_code'] = process.poll()
            result['pid'] = process.pid
    except Exception as error:
        result = adapter_failure(collector, error)
    finally:
        try:
            metrics = sampler.stop() if sampler else {'errors': ['sampler initialization unavailable']}
        except Exception as error:
            metrics = {'errors': [type(error).__name__ + ': ' + str(error)]}
        cleanup = common.terminate_owned(process, sampler) if process else {'error': 'process never started'}
        if not cleanup.get('error'):
            try:
                shutil.rmtree(profile)
            except OSError as error:
                cleanup['profile_error'] = str(error)
        else:
            cleanup['retained_profile'] = str(profile)
        if result is None:
            result = collector.result({'status': 'INFRA_ERROR', 'message': 'Incomplete Linux adapter execution'})
        result.update(duration_ms=round((time.monotonic() - started) * 1000), metrics=metrics,
                      cleanup=cleanup, launch_arguments_after_url=[command[-1]])
    return result


def summarize(rows, total=1515):
    def qualified(row):
        return (row.get('status') == 'OK' and not row.get('metrics', {}).get('errors')
                and not row.get('telemetry_errors') and not row.get('cleanup', {}).get('error')
                and not row.get('cleanup', {}).get('profile_error'))
    passing = sum(bool(qualified(row) and row.get('subtests') and
                       all(check['status'] == 'PASS' for check in row['subtests'])) for row in rows)
    return {'scheduled': total, 'recorded': len(rows), 'fullyPassing': passing,
            'unrun': total - len(rows), 'passPercentOfRecorded': 100 * passing / len(rows) if rows else None,
            'assertions': dict(Counter(check['status'] for row in rows for check in row.get('subtests', []))),
            'harnessStatuses': dict(Counter(row['status'] for row in rows)),
            'unqualifiedEvidenceVariants': sum(not qualified(row) and row.get('status') == 'OK' for row in rows)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', choices=['lumen', 'chromium'], required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--binary-sha256', required=True)
    parser.add_argument('--source-manifest', type=Path)
    parser.add_argument('--runtime-manifest', type=Path)
    parser.add_argument('--runtime-environment', type=Path)
    parser.add_argument('--fixture', type=Path, default=ROOT / 'output/wpt-10290244')
    parser.add_argument('--historical-plan', type=Path, default=ROOT / 'agent/evidence/20261029-cpu30-005/frozen-cohort-plan.json')
    parser.add_argument('--groups', nargs='+', default=['encoding-streams', 'encoding-core'])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--max-seconds', type=float, default=600)
    args = parser.parse_args()
    if platform.system() != 'Linux':
        raise ValueError('This explicit fallback adapter requires actual Linux; use the Mac adapter on Mac')
    common = load_helpers()
    error = common.monitor_module().pid_namespace_error()
    if error:
        raise RuntimeError(error)
    if args.max_seconds <= 0:
        raise ValueError('Positive invocation budget required')
    binary = args.binary.resolve(strict=True)
    if sha(binary) != args.binary_sha256:
        raise ValueError('Executable identity differs')
    fixture = args.fixture.resolve(strict=True)
    frozen = read(fixture / 'priority-selection-v1.json')
    historical = read(args.historical_plan)
    inventory = read(fixture / 'inventory.json')
    environment = read(fixture / 'browser-fixture-environment.json')
    ready = read(fixture / 'fixture-readiness.json')
    if frozen['wpt_revision'] != PIN or digest(frozen['schedule']) != FROZEN:
        raise ValueError('Pinned frozen schedule changed')
    if frozen['schedule'] != historical['frozenSchedule'] or len(frozen['schedule']) != 1515:
        raise ValueError('Historical1515 denominator changed')
    if len(set(args.groups)) != len(args.groups) or any(group not in historical['groups'] for group in args.groups):
        raise ValueError('Duplicate or unknown execution group')
    if not ready.get('fixture_ready') or ready.get('error') or sha(fixture / 'fixture-readiness.json') != environment['fixtureManifestSHA256']:
        raise ValueError('Official fixture readiness differs')
    if subprocess.check_output(['ps', '-p', str(ready['server_pid']), '-o', 'lstart='], text=True).strip() != ready['server_start_identity']:
        raise ValueError('Official fixture process identity changed')
    source_fixture = common.source_fixture_signature(inventory)
    for name, expected in frozen['source_sha256'].items():
        if sha(Path(inventory['wpt_root']) / name) != expected:
            raise ValueError('Upstream selected source changed: ' + name)
    for row in historical['helperInputs']:
        if sha(Path(inventory['wpt_root']) / row['source']) != row['sha256']:
            raise ValueError('Pinned upstream helper changed: ' + row['source'])
    reporter = common.verify_reporter_route(environment['origins']['http'])
    env = {key: value for key, value in os.environ.items() if not key.startswith(('LUMEN_', 'JSRUN_'))}
    support = {}
    if args.engine == 'lumen':
        if not all([args.source_manifest, args.runtime_manifest, args.runtime_environment]):
            raise ValueError('Native Linux runs require exact source, runtime and environment manifests')
        runtime = read(args.runtime_manifest)
        if runtime['sourceManifest'] != args.source_manifest.name:
            raise ValueError('Runtime manifest does not bind the selected source manifest')
        if runtime['files'][str(binary.relative_to(ROOT))]['sha256'] != args.binary_sha256:
            raise ValueError('Runtime manifest belongs to a different executable')
        for filename, identity in runtime['files'].items():
            if sha(ROOT / filename) != identity['sha256']:
                raise ValueError('Runtime artifact changed: ' + filename)
        values = read(args.runtime_environment)
        if set(values) - {'LUMEN_ICU_DATA', 'LD_LIBRARY_PATH'}:
            raise ValueError('Unexpected runtime environment key')
        env.update(values)
        support = {'v8': runtime['v8'], 'platform': runtime['platform'], 'rtc': runtime['rtc'],
                   'webtransport': runtime['webtransport'], 'icuSHA256': sha(binary.parent / 'icudtl.dat'),
                   'runtimeEnvironmentSHA256': sha(args.runtime_environment)}
    env.update(environment['nativeEnvironment'])
    base_signature = {
        'engine': args.engine, 'execution_adapter': 'linux-headless-v1',
        'wpt_revision': PIN, 'manifest_sha256': inventory['manifest_sha256'],
        'inventory_sha256': sha(fixture / 'inventory.json'), 'source_fixture': source_fixture,
        'server_config_sha256': sha(fixture / 'server.config.json'),
        'fixture_manifest_sha256': sha(fixture / 'fixture-readiness.json'),
        'binary_sha256': args.binary_sha256, 'runner_sha256': sha(__file__),
        'collector_sha256': sha(ROOT / 'tests/wpt/full_runner.py'), 'reporter_sha256': reporter,
        'monitor_sha256': sha(ROOT / 'tools/monitor_process.py'),
        'origin': environment['origins']['http'], 'secure_origin': environment['origins']['https'],
        'h2_origin': environment['origins']['h2'], 'timeout_s': 15, 'long_timeout_s': 65,
        'strict_infrastructure': False,
        'native_fixture': {'host_map': env['LUMEN_HOST_MAP'], 'host_map_mode': env['LUMEN_HOST_MAP_MODE'],
                           'ssl_cert_sha256': sha(env['SSL_CERT_FILE']), 'runtime_support': support},
        'source_manifest_sha256': sha(args.source_manifest) if args.source_manifest else None,
        'runtime_manifest_sha256': sha(args.runtime_manifest) if args.runtime_manifest else None,
        'chromium_headless': args.engine == 'chromium',
    }
    out = args.output.resolve()
    if not out.is_relative_to(ROOT):
        raise ValueError('Output must stay within the authorized project')
    out.mkdir(parents=True, exist_ok=True)
    plan = {'schema': 1, 'task': '10.292019r', 'scope': 'Actual Linux headless frozen upstream subset; not Apple app execution or full WPT',
            'binary': {'path': str(binary), 'sha256': args.binary_sha256},
            'sourceManifest': str(args.source_manifest) if args.source_manifest else None,
            'sourceManifestSHA256': base_signature['source_manifest_sha256'],
            'wptRevision': PIN, 'fullFrozenScheduleSHA256': FROZEN, 'fullFrozenVariantCount': 1515,
            'frozenSchedule': frozen['schedule'], 'groups': historical['groups'],
            'groupScheduleSHA256': historical['groupScheduleSHA256'], 'protocol': base_signature,
            'fixture': str(fixture), 'actualPlatform': platform.platform(), 'machine': platform.machine()}
    plan_path = out / 'cohort.json'
    if plan_path.exists() and read(plan_path) != plan:
        raise ValueError('Resume protocol/artifact differs; choose a fresh output directory')
    save(plan_path, plan)
    for name in ['source_manifest', 'runtime_manifest']:
        value = getattr(args, name)
        if value:
            target = out / (name.replace('_', '-') + '.json')
            if target.exists() and sha(target) != sha(value):
                raise ValueError('Preserve old artifact-binding manifest')
            shutil.copy2(value, target)
    all_rows = []
    for group in plan['groups']:
        result_file = out / group / 'results.jsonl'
        if result_file.exists():
            all_rows.extend(common.iter_jsonl(result_file))
    begun = time.monotonic()
    invocation = {'startedUTC': utc(), 'requestedGroups': args.groups, 'maxSeconds': args.max_seconds,
                  'fullOfficialWptPassPercent': None, 'platform': platform.platform(), 'stages': []}
    save(out / 'active-invocation.json', invocation)
    browser = None
    try:
        if args.engine == 'chromium':
            browser = common.Chromium(binary, out / 'chromium-session',
                                      [*environment['chromiumArguments'], '--no-sandbox', '--disable-gpu'], headless=True)
            invocation['chromiumVersion'] = browser.version
        for group in args.groups:
            schedule = plan['groups'][group]
            directory = out / group
            directory.mkdir(exist_ok=True)
            signature = {**base_signature, 'schedule_sha256': plan['groupScheduleSHA256'][group]}
            meta_path = directory / 'run.json'
            if meta_path.exists() and read(meta_path)['signature'] != signature:
                raise ValueError('Existing group signature differs')
            save(directory / 'schedule.json', schedule)
            metadata = read(meta_path) if meta_path.exists() else {'signature': signature, 'actualPlatform': platform.platform(), 'startedUTC': utc()}
            save(meta_path, metadata)
            known = list(common.iter_jsonl(directory / 'results.jsonl'))
            keys = [(row['type'], row['test']) for row in known]
            allowed = {(row['type'], row['test']) for row in schedule}
            if len(set(keys)) != len(keys) or not set(keys).issubset(allowed):
                raise ValueError('Existing group has duplicate or foreign rows')
            done = set(keys)
            with (directory / 'results.jsonl').open('a') as stream:
                for index, test in enumerate(schedule):
                    if (test['type'], test['test']) in done:
                        continue
                    if time.monotonic() - begun >= args.max_seconds:
                        metadata['stoppedForBudget'] = True
                        break
                    if sha(binary) != args.binary_sha256 or sha(__file__) != base_signature['runner_sha256'] or sha(ROOT / 'tests/wpt/full_runner.py') != base_signature['collector_sha256'] or sha(ROOT / 'tools/monitor_process.py') != base_signature['monitor_sha256']:
                        raise ValueError('Executable or execution protocol changed during the run')
                    options = argparse.Namespace(origin=base_signature['origin'], secure_origin=base_signature['secure_origin'], h2_origin=base_signature['h2_origin'])
                    url = common.test_url(test, options)
                    prefix = directory / ('case-' + format(index, '06d') + '-' + digest([test['type'], test['test']])[:10])
                    timeout = 65 if test.get('timeout') == 'long' else 15
                    result = browser.run(url, timeout, prefix) if browser else run_native(common, binary, url, timeout, prefix, env)
                    if sha(binary) != args.binary_sha256 or sha(__file__) != base_signature['runner_sha256'] or sha(ROOT / 'tests/wpt/full_runner.py') != base_signature['collector_sha256'] or sha(ROOT / 'tools/monitor_process.py') != base_signature['monitor_sha256']:
                        save(prefix.with_suffix('.changed-execution-result.json'), result)
                        result = {'status': 'INFRA_ERROR', 'message': 'Executable or protocol changed during this case; original result quarantined', 'subtests': [], 'telemetry_errors': ['execution identity changed']}
                    row = {'test': test['test'], 'type': test['type'], 'url': url, 'sequence': index, **result}
                    stream.write(json.dumps(row, ensure_ascii=True) + '\n')
                    stream.flush()
                    os.fsync(stream.fileno())
                    all_rows.append(row)
                    known.append(row)
                    save(out / 'summary.json', summarize(all_rows))
                    if result.get('cleanup', {}).get('error') or result.get('cleanup', {}).get('profile_error') or result.get('metrics', {}).get('errors') or result.get('telemetry_errors'):
                        raise RuntimeError('Measurement/cleanup failed; retained raw case and stopped')
            metadata.update(finishedUTC=utc(), finished_schedule=len(known) == len(schedule), summary=summarize(known, len(schedule)))
            save(meta_path, metadata)
            invocation['stages'].append({'group': group, **metadata['summary']})
            print(json.dumps({'group': group, **metadata['summary']}), flush=True)
            if time.monotonic() - begun >= args.max_seconds:
                break
    except Exception as error:
        invocation['error'] = type(error).__name__ + ': ' + str(error)
        raise
    finally:
        if browser:
            try:
                invocation['chromiumCleanup'] = browser.close()
            except Exception as error:
                invocation['chromiumCleanupError'] = type(error).__name__ + ': ' + str(error)
        invocation.update(finishedUTC=utc(), durationSeconds=time.monotonic() - begun, summary=summarize(all_rows))
        save(out / ('invocation-' + str(time.time_ns()) + '.json'), invocation)
        save(out / 'summary.json', invocation['summary'])
        print(json.dumps({'output': str(out), **invocation['summary']}), flush=True)
    if invocation.get('chromiumCleanupError'):
        raise RuntimeError('Chromium cleanup failed; this control is unqualified')


if __name__ == '__main__':
    main()
