#!/usr/bin/env python3
"""Run a fresh CfT URL77 control and compare it with the completed b5b2 native run."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import sys

REPO = Path('/Users/devin/repos/lumen')
NATIVE = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-url-native-shared-worker-v34/lumen'
NATIVE_SHA = 'b5b2ee8c80685a644a998b8b2cdbe527a372e6c2d77b408a5d0d657c7e65151a'
RUNNER_SHA = '0da9eda073345d9aeb053f8a8eda21de632eb5e5078a00d0b01b029fcee4ffda'
ADOPTION_SHA = 'bb030858a2082b49dac1cd661ff2707de2168046dd2b435c2fd8dac06289fb3d'
INVENTORY_SHA = '9ffd9444520fd3158b7566f37600b0f10b56bbded1ccbd9b1b1ed8166a836053'


def require(condition, reason):
    if not condition:
        raise RuntimeError(reason)


def sha256(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def load_module(path, expected, name):
    require(sha256(path) == expected, 'Reviewed helper changed: ' + str(path))
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def known_reference_teardown(meta):
    """Recognize only the measured post-report, acknowledged-close watchdog incident."""
    cleanup = meta.get('chromium_cleanup') or {}
    process = cleanup.get('process') or {}
    graceful = cleanup.get('graceful_close') or {}
    expected = 'Unexpected Chromium exit status 2'
    if not (meta.get('finished_schedule') is True and
            cleanup.get('errors') == [expected] and process.get('teardown_error') == expected and
            process.get('error') is None and process.get('root_exit_code') == 2 and
            process.get('root_exit_before_signal_cleanup') == 2 and
            process.get('root_exit_observation_verified') is True and
            process.get('sent_sigterm') is False and process.get('sent_sigkill') is False and
            process.get('process_group_gone') is True and process.get('remaining_group_members') == {} and
            graceful.get('method') == 'Browser.close' and graceful.get('attempted') is True and
            graceful.get('acknowledged') is True and graceful.get('process_group_gone') is True):
        return False
    for key, value in meta.items():
        if key.startswith('stopped_for_') and value and key != 'stopped_for_cleanup_failure':
            return False

    def other_error(value, path=()):
        if isinstance(value, dict):
            for key, item in value.items():
                current = path + (key,)
                if current in (('errors',), ('process', 'teardown_error')):
                    continue
                if 'error' in key.lower() and item:
                    return True
                if other_error(item, current):
                    return True
        elif isinstance(value, list):
            return any(other_error(item, path) for item in value)
        return False
    return not other_error(cleanup)


def inspect_completed(core, directory, count, engine, binary_hash, allow_known_teardown=False):
    meta = json.loads((directory / 'run.json').read_text())
    signature = meta['signature']
    require(signature['engine'] == engine and signature['binary_sha256'] == binary_hash,
            'Completed run has a different browser identity')
    require(signature['runner_sha256'] == RUNNER_SHA and signature['inventory_sha256'] == INVENTORY_SHA,
            'Completed run uses different runner or WPT inputs')
    schedule = core.verified_schedule(directory, signature)
    require(meta.get('finished_schedule') and len(schedule) == count, 'Run is not a completed expected schedule')
    rows = 0
    issues = []
    for index, row in enumerate(core.iter_jsonl(directory / 'results.jsonl')):
        require(index < count and (row['type'], row['test']) == (schedule[index]['type'], schedule[index]['test']),
                'Completed results are not the exact ordered schedule')
        rows += 1
        if row.get('status') == 'INFRA_ERROR' or (row.get('reporter_state') or {}).get('status') != 'ATTACHED':
            issues.append('Missing official reporter or infrastructure error: ' + row['test'])
        if count == 6 and (row.get('status') != 'OK' or not row.get('subtests')):
            issues.append('Reference smoke lacks a completed nonempty harness report: ' + row['test'])
        cleanup = row.get('cleanup') or {}
        if cleanup.get('error') or cleanup.get('profile_error') or cleanup.get('errors'):
            issues.append('Case cleanup failed: ' + row['test'])
        if core.measurement_failure(row):
            issues.append('Case telemetry failed: ' + row['test'])
    require(rows == count, 'Run does not contain every expected result')
    known_teardown = bool(engine == 'chromium' and allow_known_teardown and known_reference_teardown(meta))
    issues.extend(key for key, value in meta.items() if key.startswith('stopped_for_') and value
                  and not (known_teardown and key == 'stopped_for_cleanup_failure'))
    if engine == 'chromium':
        cleanup = meta.get('chromium_cleanup') or {}
        process = cleanup.get('process') or {}
        if not known_teardown and (not cleanup or cleanup.get('errors') or process.get('error') or process.get('teardown_error') or
                process.get('root_exit_code') != 0 or process.get('process_group_gone') is not True or
                process.get('sent_sigterm') is not False or process.get('sent_sigkill') is not False or
                process.get('root_exit_observation_verified') is not True or process.get('root_exit_before_signal_cleanup') != 0 or
                process.get('remaining_group_members') != {}):
            issues.append('Reference did not close naturally with verified empty owned process group')
        require((meta.get('browser_version') or {}).get('product', '').split('/')[-1] == '156.0.8078.47',
                'Actual reference browser version differs from the exact official artifact')
        require(bool(meta.get('chromium_sessions')), 'Reference telemetry session evidence is absent')
        for session in meta['chromium_sessions']:
            metrics = json.loads((Path(session) / 'chromium.metrics-summary.json').read_text())
            if core.measurement_failure({'metrics': metrics}):
                issues.append('Reference session telemetry failed')
        if known_teardown:
            require(len(meta['chromium_sessions']) == 1, 'Known teardown policy requires a single fresh reference session')
            log = Path(meta['chromium_sessions'][0]) / 'chromium-process.log'
            with log.open('rb') as source:
                source.seek(max(0, log.stat().st_size - 65536))
                tail = source.read(65536)
            require(b'Teardown watchdog expired; recording dump and terminating.' in tail,
                    'Exit 2 is not accompanied by the measured teardown-watchdog diagnostic')
            meta['functional_reference_incident'] = {
                'classification': 'functional_only_reference_teardown_failed',
                'cleanup_passed': False, 'performance_release_gate_passed': False, 'cleanup_evidence': cleanup,
                'browser_log': str(log), 'browser_log_sha256': sha256(log),
                'policy': 'Only complete reported tests may be compared; acknowledged Browser.close, exact watchdog exit 2, no runner signals, empty owned group, no other infrastructure errors.'}
    return meta, schedule, issues


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('run', 'compare'))
    parser.add_argument('--allow-known-reference-teardown', action='store_true',
                        help='Explicit functional-only comparison policy; preserve the exact measured reference watchdog failure')
    parser.add_argument('--reference', type=Path, default=Path('output/wpt/reference-browsers/cft-156.0.8078.47-mac-arm64-001/artifact-integrity-v1.json'))
    parser.add_argument('--smoke', type=Path, default=Path('output/wpt/gpt-6-astra-pro-devinmac3d-cft156-url-smoke-v34/chromium'))
    parser.add_argument('--output', type=Path, default=Path('output/wpt/gpt-6-astra-pro-devinmac3d-url-shared-worker-cft156-v34/chromium'))
    args = parser.parse_args()
    require(platform.system() == 'Darwin', 'Execute only on the authorized Mac through devinmac3d')
    os.chdir(REPO)
    runner = REPO / 'tests/wpt/full_runner.py'
    core = load_module(runner, RUNNER_SHA, 'cft_url77_core')
    verifier = load_module(Path(__file__).with_name('adopt_cft156.py'), ADOPTION_SHA, 'cft_url77_integrity')
    reference_path = args.reference.resolve(strict=True)
    reference = json.loads(reference_path.read_text())
    require(reference.get('status') == 'official_artifact_verified', 'Reference integrity preparation has not completed')
    native, schedule, native_issues = inspect_completed(core, NATIVE, 77, 'lumen', NATIVE_SHA)
    require(not native_issues, 'Native URL77 is incomplete or unsafe: ' + repr(native_issues))
    output = args.output.resolve()
    require(output.is_relative_to(REPO / 'output/wpt'), 'Use a project-local WPT output')
    signature = native['signature']
    require(signature['strict_infrastructure'] is True, 'Native URL77 used a different reporting policy')
    launch = output.with_name(output.name + '.launch.json')
    if args.action == 'compare':
        prior_launch = json.loads(launch.read_text())
        require(prior_launch['allow_known_reference_teardown'] == args.allow_known_reference_teardown,
                'Comparison reporting policy differs from launch')
        require(prior_launch['native_results_sha256'] == sha256(NATIVE / 'results.jsonl') and
                prior_launch['reference_manifest_sha256'] == sha256(reference_path),
                'Comparison inputs changed from the guarded control launch')
        chromium, control_schedule, control_issues = inspect_completed(core, output, 77, 'chromium', reference['binary_sha256'],
                                                                      args.allow_known_reference_teardown)
        require(control_schedule == schedule, 'Native and Chromium schedules differ')
        report = output.parent / 'comparison.json'
        require(not report.exists(), 'Preserve the existing comparison report')
        core.compare_command(argparse.Namespace(lumen=NATIVE, chromium=output, output=report))
        result = json.loads(report.read_text())
        result.update(reference_artifact={'manifest': str(reference_path), 'manifest_sha256': sha256(reference_path),
                          'verification_policy': reference['verification_policy'], 'strict_bundle_verification_passed': False},
                      comparison_evidence={'native_run_reused': str(NATIVE), 'reference_issues': control_issues,
                          'reference_smoke_incident': prior_launch.get('reference_smoke_incident'),
                          'reference_control_incident': chromium.get('functional_reference_incident'),
                          'reference_control_cleanup_passed': not bool(chromium.get('functional_reference_incident')) and not control_issues,
                          'reference_performance_release_gate_passed': not args.allow_known_reference_teardown and not control_issues,
                          'classification': ('diagnostic_only_other_reference_incident' if control_issues else
                                             'functional_only_reference_teardown_failed' if args.allow_known_reference_teardown else
                                             'matched_complete')})
        core.write_json(report, result)
        print(json.dumps({'comparison': str(report), 'classification': result['comparison_evidence']['classification']}))
        return
    require(not output.exists() and not launch.exists(), 'Use fresh reference results; never resume with changed provenance')
    smoke, _, smoke_issues = inspect_completed(core, args.smoke.resolve(strict=True), 6, 'chromium', reference['binary_sha256'],
                                              args.allow_known_reference_teardown)
    require(not smoke_issues, 'Reference smoke is ineligible under the explicitly requested policy: ' + repr(smoke_issues))
    require(smoke['signature']['chromium_headless'] is True, 'Reference mode differs from the reviewed headless baseline')
    flags = smoke['signature']['chromium_args']
    permitted = ('--host-resolver-rules=', '--ignore-certificate-errors-spki-list=')
    require(len(flags) == 2 and all(sum(flag.startswith(prefix) for flag in flags) == 1 for prefix in permitted),
            'Reference smoke used additional browser flags')
    inventory_path = REPO / 'output/wpt/inventory.json'
    require(sha256(inventory_path) == INVENTORY_SHA, 'Pinned WPT inventory changed')
    selected, descriptor = core.select_schedule(json.loads(inventory_path.read_text()), 'testharness', ['/url/'])
    require(selected == schedule and descriptor == signature['selection'], 'Fresh reference selection differs from native URL77')
    config = REPO / 'output/wpt/server.config.json'
    readiness = REPO / 'output/wpt/fixture-readiness-v3-clean.json'
    require(sha256(config) == signature['server_config_sha256'] and sha256(readiness) == signature['fixture_manifest_sha256'],
            'The native run fixture changed')
    reference = verifier.verify_reference(reference_path, REPO)
    command = [sys.executable, str(runner), 'run', '--inventory', str(inventory_path), '--engine', 'chromium',
               '--binary', reference['binary'], '--output', str(output), '--types', 'testharness', '--prefix', '/url/',
               '--origin', signature['origin'], '--secure-origin', signature['secure_origin'], '--h2-origin', signature['h2_origin'],
               '--timeout', str(signature['timeout_s']), '--long-timeout', str(signature['long_timeout_s']),
               '--server-config', str(config), '--fixture-manifest', str(readiness), '--strict-infrastructure']
    command.extend('--chromium-arg=' + value for value in flags)
    for key in ('LUMEN_PRE_FILE', 'LUMEN_SHOT', 'LUMEN_SHOT_MS', 'LUMEN_NO_JS'):
        os.environ.pop(key, None)
    launch.parent.mkdir(parents=True, exist_ok=True)
    with launch.open('x') as destination:
        json.dump({'argv': command, 'cwd': str(REPO), 'launcher_sha256': sha256(__file__),
                   'native_run_reused': str(NATIVE), 'native_results_sha256': sha256(NATIVE / 'results.jsonl'),
                   'reference_manifest': str(reference_path), 'reference_manifest_sha256': sha256(reference_path),
                   'reference_tree_sha256': reference['integrity']['tree_sha256'],
                   'reference_verification_policy': reference['verification_policy'], 'strict_bundle_verification_passed': False,
                   'allow_known_reference_teardown': args.allow_known_reference_teardown,
                   'reference_smoke_incident': smoke.get('functional_reference_incident'),
                   'comparison_classification': 'functional_only_reference_teardown_failed' if args.allow_known_reference_teardown else 'matched_complete',
                   'reference_performance_release_gate_passed': not args.allow_known_reference_teardown,
                   'schedule_sha256': core.digest(schedule), 'selection': descriptor}, destination, indent=2)
    print(json.dumps({'fresh_control': str(output), 'native_run_reused': str(NATIVE), 'scheduled_files': 77}), flush=True)
    os.execv(sys.executable, command)


if __name__ == '__main__':
    main()
