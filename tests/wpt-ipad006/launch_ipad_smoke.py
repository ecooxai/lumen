#!/usr/bin/env python3
"""Launch the isolated build006 iPad HTTP6 / HTTPS1 smoke, one stage at a time.

Generation006 targets the same recorded iPad and the proven build006 app,
using explicit simulator platform termination. The earlier failed cleanup
cohort and its platform recovery remain preserved as separate evidence.
Root executes this only through devinmac3d. The reviewed iPad entry point owns
the app, per-case deadlines, one-second telemetry and cleanup. This wrapper
never installs, rebuilds, selects another simulator or launches Chromium.
"""
import argparse
import importlib
import hashlib
import json
import os
from pathlib import Path
import platform
import sys
import time
import urllib.parse


ROOT = Path('/Users/devin/repos/lumen')
DEVICE = 'A77F68E4-972B-472B-80B8-F65E71D361E5'
BUNDLE = 'com.ecooxai.lumen'
GENERATION = 'output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006'
INSTALL_GENERATION = 'output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006'
PINS = {
    'ipad_runner.py': 'a15d1454fe147c588b0b1597233e8d11694920541eb6ecfc14aa8f4858e331df',
    'ipad_adapter.py': '885d9aed25b8525f9370246f3bc87b05d72713052749cb9a06a0198a25434fc4',
    'full_runner.py': 'a7bc96c3697fb099ad63c145011bc434a66d865a713a62e5198731bed9600ac7',
    'ipad_generation.py': 'ddfc70cf9a2388208c1b2b47a840fc41fa228cc41615f614909d57d122d803dd',
    'testharnessreport.js': 'dd0c222e2680c8621ac42dc98746b15891fa4e14d9f99f4c909dbc2ca2558328',
    'ipad_lifecycle.py': 'fdf39fca49c5e72bbb05d132a07c6abbc79ae37d15b49e4c280e863d85a6bbbf',
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def load(path):
    return json.loads(Path(path).read_text())


def inspect_stage(core, entry, cohort, stage, binding):
    """Keep failed assertions; mark open/partial/unsafe runs diagnostic."""
    launch = load(cohort / ('launch-' + stage + '.json'))
    require(launch['binding'] == binding, 'Stage launch binding differs from its smoke cohort')
    directory = cohort / stage
    paths = [directory / name for name in ('run.json', 'schedule.json', 'results.jsonl')]
    identities = {str(path): core.binary_identity(path) for path in paths}
    meta = load(paths[0])
    signature = meta['signature']
    schedule = core.verified_schedule(directory, signature)
    require(schedule == launch['schedule'], 'Actual iPad schedule differs from the planned ' + stage)
    require(signature['engine'] == 'lumen' and signature['execution_platform'] == 'ipad-simulator',
            'Stage is not the separately labelled iPad Lumen run')
    require(signature['binary_sha256'] == binding['binary_sha256']
            and signature['signed_bundle_provenance']['matching_bundle_contents_sha256'] == binding['bundle_sha256'],
            'Actual iPad bundle differs from the installed build006 generation')
    require(signature['adapter']['device']['udid'] == DEVICE
            and signature['adapter']['bundle_id'] == BUNDLE
            and signature['adapter']['adapter_sha256'] == PINS['ipad_adapter.py']
            and signature['adapter']['cleanup_policy'] == binding['lifecycle']['policy']
            and signature['adapter']['app_graceful_shutdown_verified'] is False,
            'Actual stage target/adapter identity differs')
    expected = {'core_runner_sha256': PINS['full_runner.py'], 'runner_sha256': PINS['ipad_runner.py'],
                'inventory_sha256': binding['inventory_sha256'],
                'server_config_sha256': binding['server_config_sha256'],
                'fixture_manifest_sha256': binding['fixture_sha256'],
                'reporter_sha256': binding['reporter_sha256'], 'monitor_sha256': binding['monitor_sha256'],
                'origin': 'http://web-platform.test:8000', 'secure_origin': 'https://web-platform.test:18443',
                'h2_origin': 'https://web-platform.test:9000', 'timeout_s': 15, 'long_timeout_s': 65,
                'strict_infrastructure': True}
    for key, value in expected.items():
        require(signature.get(key) == value, 'Actual iPad stage changed ' + key)
    require(signature['native_fixture']['host_map_mode'] == 'exact'
            and signature['native_fixture']['host_map'] == binding['host_map']
            and signature['native_fixture']['ssl_cert_sha256'] == binding['ca_sha256'],
            'Actual iPad fixture identity differs')
    accounting = core.Summary(schedule)
    unsafe, telemetry, termination_telemetry = [], [], []
    for index, row in enumerate(core.iter_jsonl(directory / 'results.jsonl')):
        require(index < len(schedule)
                and (row['type'], row['test']) == (schedule[index]['type'], schedule[index]['test']),
                'Actual stage results are not an ordered schedule prefix')
        accounting.add(row)
        request = (row.get('cleanup') or {}).get('request') or {}
        if request:
            termination_telemetry.append({'test': row['test'], 'request_passed': request.get('request_passed'),
                                         'metrics': request.get('metrics'), 'process_cleanup': request.get('process_cleanup')})
        reasons = entry.unsafe_result(row, strict=True)
        if reasons:
            unsafe.append({'test': row['test'], 'reasons': reasons})
        telemetry.append({'test': row['test'], 'metrics': row.get('metrics'),
                          'cleanup': row.get('cleanup'), 'reporter_state': row.get('reporter_state')})
    require(identities == {str(path): core.binary_identity(path) for path in paths},
            'Stage evidence changed during inspection; wait for the invocation to close')
    state = entry.comparison_run_state(meta, accounting.value())
    safe = (not entry.invocation_is_open(meta) and state['all_scheduled_rows_recorded']
            and state['finished_schedule'] and not any(state['stops'].values())
            and not state['cleanup_unverified'] and not unsafe)
    report = {'stage': stage, 'status': 'closed-complete' if safe else 'diagnostic-stopped-or-partial',
              'safe_to_continue': safe, 'execution_platform': 'ipad-simulator',
              'summary': accounting.value(), 'run_state': state, 'unsafe_results': unsafe,
              'telemetry_by_case': telemetry, 'resource_scope': entry.RESOURCE_NOTE,
              'platform_termination_telemetry_by_case': termination_telemetry,
              'lifecycle_policy': binding['lifecycle'], 'app_graceful_shutdown_verified': False,
              'measurement_note': 'Short or empty samples do not establish acceptable sustained CPU/RAM use.',
              'comparison': 'No Chromium control or Mac/iPad performance comparison was run by this launcher.'}
    core.write_json(cohort / ('inspection-' + stage + '.json'), report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('http', 'https', 'report'))
    parser.add_argument('--install-report', type=Path,
                        default=Path(INSTALL_GENERATION) / 'install-identity-001/install-verify.json')
    parser.add_argument('--output', type=Path, default=Path(GENERATION) / 'runtime-smoke-001')
    args = parser.parse_args()
    require(platform.system() == 'Darwin', 'Execute only on the authorized Mac through devinmac3d')
    root = ROOT.resolve(strict=True)
    os.chdir(root)
    source = root / 'tests/wpt-ipad006'
    sys.path.insert(0, str(source))
    for name, expected in PINS.items():
        with (source / name).open('rb') as checked:
            require(hashlib.file_digest(checked, 'sha256').hexdigest() == expected,
                    'Reviewed smoke source changed: ' + name)
    generation = importlib.import_module('ipad_generation')
    lifecycle = importlib.import_module('ipad_lifecycle')
    core = importlib.import_module('full_runner')
    entry = importlib.import_module('ipad_runner')
    require(entry.AUTHORIZED_DEVICE == DEVICE and entry.AUTHORIZED_BUNDLE == BUNDLE,
            'Authorized iPad identity changed')
    cohort = (root / args.output).resolve()
    require(cohort.is_relative_to(root / GENERATION), 'Runtime evidence must stay inside the fresh iPad generation006 cohort')
    lifecycle_binding = lifecycle.verify_lifecycle(root)
    if args.stage == 'report':
        binding = load(cohort / 'binding.json')
        require(binding['lifecycle'] == lifecycle_binding, 'Retained lifecycle evidence changed since launch')
        stages = {}
        for stage in ('http', 'https'):
            if not (cohort / ('launch-' + stage + '.json')).exists():
                stages[stage] = {'status': 'not-launched', 'safe_to_continue': False}
                continue
            try:
                stages[stage] = inspect_stage(core, entry, cohort, stage, binding)
            except (OSError, ValueError, KeyError, RuntimeError) as error:
                stages[stage] = {'status': 'evidence-unavailable-or-inconsistent', 'safe_to_continue': False,
                                 'error': type(error).__name__ + ': ' + str(error)}
        report = {'execution_platform': 'ipad-simulator', 'binding': binding, 'stages': stages,
                  'app_graceful_shutdown_verified': False,
                  'comparison': 'Standalone iPad smoke; no Chromium comparison or global WPT pass rate.'}
        core.write_json(cohort / 'smoke-report.json', report)
        print(json.dumps({stage: value.get('summary', value) for stage, value in stages.items()}))
        return 0 if all(stage['safe_to_continue'] for stage in stages.values()) else 2
    generation_binding = generation.verify_generation(root)
    installed_path = (root / args.install_report).resolve(strict=True)
    require(installed_path.is_relative_to(root / INSTALL_GENERATION), 'Install proof must belong to the retained iPad generation006 installation')
    installed = load(installed_path)
    require(installed.get('status') == 'installed_and_verified'
            and installed.get('device') == DEVICE and installed.get('bundle_id') == BUNDLE,
            'Exact iPad installation has not passed signed-bundle verification')
    require(installed.get('generation_binding') == generation_binding,
            'Install proof differs from the exact provisioning receipt or proven build006')
    require(installed.get('retained_failure_profile_unchanged') is True
            and installed.get('lifecycle_evidence') == lifecycle_binding
            and installed.get('matching_bundle_contents_sha256') == generation.BUNDLE_SHA,
            'Install proof does not preserve the prior failed profile and exact source bundle')
    binary_hash = installed['installed_executable_sha256']
    require(installed['source_executable_sha256'] == binary_hash and len(binary_hash) == 64,
            'Source/installed executable hashes differ')
    require(core.file_digest(entry.signed_binary(root)) == binary_hash, 'Signed source app changed after installation')
    reference_path = root / 'output/wpt/gpt-6-astra-pro-devinmac3d-url-full-v31/lumen/run.json'
    reference = load(reference_path)['signature']
    fixture = reference['native_fixture']
    ca = root / 'output/wpt/upstream/tools/certs/cacert.pem'
    config = root / 'output/wpt/server.config.json'
    inventory_path = root / 'output/wpt/inventory.json'
    fixture_path = root / 'output/wpt/fixture-readiness-v36-clean.json'
    require(core.file_digest(fixture_path) == '8ac07a9ac8d163cfd868d4172cf82d837020573c9999365faf84be6a789682e7',
            'The verified v36 fixture readiness receipt changed')
    require(fixture['host_map_mode'] == 'exact' and fixture['host_map'], 'Saved native host map is not exact')
    for item in fixture['host_map'].split(','):
        host, address = item.split('=', 1)
        require(bool(host) and address == '127.0.0.1', 'Only the retained exact loopback host map is allowed')
    require(reference['server_config_sha256'] == core.file_digest(config)
            and fixture['ssl_cert_sha256'] == core.file_digest(ca), 'Saved host map fixture identity changed')
    binding = {'generation_binding': generation_binding, 'lifecycle': lifecycle_binding,
               'install_report_sha256': core.file_digest(installed_path), 'binary_sha256': binary_hash,
               'bundle_sha256': installed['matching_bundle_contents_sha256'],
               'core_runner_sha256': PINS['full_runner.py'], 'host_map': fixture['host_map'],
               'inventory_sha256': core.file_digest(inventory_path), 'server_config_sha256': core.file_digest(config),
               'fixture_sha256': core.file_digest(fixture_path), 'ca_sha256': core.file_digest(ca),
               'reporter_sha256': core.file_digest(source / 'testharnessreport.js'),
               'monitor_sha256': core.file_digest(root / 'tools/monitor_process.py'),
               'host_reference_sha256': core.file_digest(reference_path)}
    if args.stage == 'http':
        cohort.mkdir(parents=True, exist_ok=False)
        core.write_json(cohort / 'binding.json', binding)
    else:
        require(load(cohort / 'binding.json') == binding, 'Smoke cohort inputs changed; preserve it and use a fresh cohort')
    if args.stage == 'https':
        prior = inspect_stage(core, entry, cohort, 'http', binding)
        require(prior['safe_to_continue'], 'HTTP smoke has not closed safely; inspect its retained diagnostics')
    inventory = load(inventory_path)
    origins = argparse.Namespace(origin='http://web-platform.test:8000', secure_origin='https://web-platform.test:18443',
                                 h2_origin='https://web-platform.test:9000', fixture_manifest=fixture_path,
                                 server_config=config, ca_file=ca)
    entry.validate_fixture(origins, inventory)
    prefix, count = '/url/', 6
    if args.stage == 'https':
        choices = sorted((row for row in inventory['tests'] if row['type'] == 'testharness'
                          and row['test'].startswith('/secure-contexts/')
                          and urllib.parse.urlsplit(core.test_url(row, origins)).scheme == 'https'),
                         key=lambda row: row['test'])
        require(bool(choices), 'No real HTTPS secure-context test exists in this inventory')
        prefix, count = choices[0]['test'], 1
    schedule, selection = core.select_schedule(inventory, 'testharness', [prefix], 0, count)
    require(len(schedule) == count, 'Smoke selection is incomplete')
    if args.stage == 'https':
        require(schedule[0]['test'] == prefix, 'HTTPS prefix did not reproduce the exact selected variant')
    urls = [core.test_url(row, origins) for row in schedule]
    expected_scheme = 'http' if args.stage == 'http' else 'https'
    require(all(urllib.parse.urlsplit(url).scheme == expected_scheme for url in urls), 'Smoke selected the wrong transport')
    del inventory
    destination = cohort / args.stage
    require(not destination.exists(), 'Stage already exists; never overwrite prior evidence')
    command = [sys.executable, str(source / 'ipad_runner.py'), 'run', '--repo', str(root),
               '--device', DEVICE, '--bundle-id', BUNDLE, '--inventory', str(inventory_path),
               '--server-config', str(config), '--fixture-manifest', str(fixture_path), '--ca-file', str(ca),
               '--host-map', fixture['host_map'], '--types', 'testharness', '--prefix', prefix,
               '--offset', '0', '--limit', str(count), '--timeout', '15', '--long-timeout', '65',
               '--origin', origins.origin, '--secure-origin', origins.secure_origin, '--h2-origin', origins.h2_origin,
               '--screenshot-first', '--output', str(destination)]
    plan = {'stage': args.stage, 'controller_pid': os.getpid(), 'started_unix': time.time(), 'argv': command,
            'cwd': str(root), 'schedule': schedule, 'selection': selection, 'urls': urls, 'binding': binding,
            'launcher_sha256': core.file_digest(__file__),
            'execution_note': 'exec preserves this controller PID; final invocation/cleanup status is in the stage run.json.'}
    with (cohort / ('launch-' + args.stage + '.json')).open('x') as output:
        json.dump(plan, output, indent=2)
        output.write('\n')
    print(json.dumps({'stage': args.stage, 'controller_pid': os.getpid(), 'output': str(destination),
                      'scheduled_files': len(schedule), 'execution_platform': 'ipad-simulator'}), flush=True)
    os.execv(sys.executable, command)


if __name__ == '__main__':
    raise SystemExit(main())
