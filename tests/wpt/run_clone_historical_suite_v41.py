#!/usr/bin/env python3
"""Fresh two-file measurement of private URL structured-clone behavior.

Both engines use the repeat-aware v36 reporter and fresh fixture evidence. The
reference watchdog incident remains an explicit functional-only qualification.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

REPO = Path('/Users/devin/repos/lumen')
CORE_SHA = 'a7bc96c3697fb099ad63c145011bc434a66d865a713a62e5198731bed9600ac7'
REPORTER_SHA = 'dd0c222e2680c8621ac42dc98746b15891fa4e14d9f99f4c909dbc2ca2558328'
BASE_RECEIPT_SHA = '4d1ffd4adadb1283c15b6730017fbcc0b5ffb904deb4c368b2f54e65957b183f'
HISTORICAL_NATIVE_SHA = 'b5b2ee8c80685a644a998b8b2cdbe527a372e6c2d77b408a5d0d657c7e65151a'
READINESS_SHA = '8ac07a9ac8d163cfd868d4172cf82d837020573c9999365faf84be6a789682e7'
POLICY_SHA = '9e4954758177647d82cda5a26d39307cf07bcbfde21fc2e5349cd2065b29f8af'
ADOPTION_SHA = 'bb030858a2082b49dac1cd661ff2707de2168046dd2b435c2fd8dac06289fb3d'
INVENTORY_SHA = '9ffd9444520fd3158b7566f37600b0f10b56bbded1ccbd9b1b1ed8166a836053'
FIXED_TESTS = ['/url/historical.any.html', '/url/historical.any.worker.html']
NEW_GATE_NAMES = {
    'form_enctype_gate_report': ['test_form_dialog', 'test_url_dom'],
    'worker_security_gate_report': ['test_worker_security', 'test_worker_lifecycle',
                                   'test_url_idl_window', 'test_url_idl_worker', 'test_capture_policy'],
    'clone_brand_gate_report': ['test_clone_brand_window', 'test_clone_brand_worker',
                                'test_window_lifecycle', 'test_worker_lifecycle', 'test_worker_exit',
                                'test_worker_security', 'test_url_idl_window', 'test_url_idl_worker',
                                'test_url_dom', 'test_history_origin'],
}
IDL_GATE_NAMES = ['test_url_idl_window', 'test_url_idl_worker', 'test_url_params',
                  'test_worker_lifecycle', 'test_worker_lifecycle_exit_with_worker',
                  'test_url_dom', 'test_history_origin', 'test_capture_policy', 'test_beacon']

GENERATION_SOURCE_UPDATES = {'Makefile': '3526b90ba87e129175921824737e347d188203b5952e22f54d03579c43aa16fe', 'src/js/jsnative.cc': '6e942e787e3f21a0231bbcdaab70eee199bc5148059bbd0f7b685b4fbaa1089b', 'src/js/prelude/08-window.js': 'd58090d056f4504c51af9ea660dd3b254ac0ee8e600ffce00befc006bf2bf923', 'src/js/worker.cc': '10ed67b8d40ba84a015c9de850385102aadf4f1c95852aeb7a2687448c2f56f4', 'src/js/frames.cc': 'f97d5a56c5cdbcbfca7e4c4e2cf42f72a9cbdd7a4747d42cfe47a66d873b1dee', 'src/js/windows.cc': '44149413b08a803f6e0edf025a99d5635e69bdd32868658d1b9471c3edd71c45', 'src/js/clone_brand.h': 'e745d8e39572738bba70f4ad5b3c2671c72a0fe7e35114cf3b66807f90dbecbc', 'tests/test_clone_brand.cc': '733afaecc0b0ed299a857d7df7376d19092cf9aa18e1ccfe9aeb1e929a21e672', 'tests/js/clone-brand-regression.js': '7bd547613a4807131c3f2161e0cc33bb9cef74d7140637469a1894880be82630', 'tests/js/clone-brand-window-regression.js': '5d23a06f1ae0d759dad1dd50a09476c288d203650bed082a95bfbe950f5ffdc6', 'tests/js/worker-clone-brand-regression.js': 'a2070a6ce1520b8a9b8b2dce8a7483b7dbb013a7618d64f364387ec37f958416'}

def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def module(path, expected, name):
    require(digest(path) == expected, 'Reviewed source changed: ' + str(path))
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('lumen', 'chromium', 'compare'))
    parser.add_argument('--native-sha256', required=True)
    parser.add_argument('--receipt-sha256', required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--fixture-manifest', type=Path, default=Path('output/wpt/fixture-readiness-v36-clean.json'))
    args = parser.parse_args()
    NATIVE_SHA, RECEIPT_SHA = args.native_sha256, args.receipt_sha256
    require(platform.system() == 'Darwin', 'Run only on the authorized Mac through devinmac3d')
    require(all(len(value) == 64 and all(char in '0123456789abcdef' for char in value)
                for value in (NATIVE_SHA, RECEIPT_SHA)),
            'The actual post-gate native snapshot has not been pinned yet')
    os.chdir(REPO)
    here = REPO / 'tests/wpt'
    core = module(here / 'full_runner.py', CORE_SHA, 'clone_historical_fixed_suite_core')
    policy = module(here / 'run_cft156_url77.py', POLICY_SHA, 'clone_historical_fixed_suite_policy')
    adoption = module(here / 'adopt_cft156.py', ADOPTION_SHA, 'clone_historical_fixed_suite_adoption')
    reporter = here / 'testharnessreport.js'
    require(digest(reporter) == REPORTER_SHA, 'Served reporter changed')
    inventory = REPO / 'output/wpt/inventory.json'
    require(digest(inventory) == INVENTORY_SHA, 'Pinned upstream inventory changed')
    inventory_value = json.loads(inventory.read_text())
    prefixes, expected_files = FIXED_TESTS, 2
    schedule, selection = core.select_schedule(inventory_value, 'testharness', prefixes, 0, None)
    require(len(schedule) == expected_files and [row['test'] for row in schedule] == FIXED_TESTS,
            'Selection is not the exact original historical window and worker pair')
    baseline = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-url-native-shared-worker-v34/lumen/run.json'
    baseline_meta = json.loads(baseline.read_text())
    signature = baseline_meta['signature']
    require(signature['binary_sha256'] == HISTORICAL_NATIVE_SHA and signature['inventory_sha256'] == INVENTORY_SHA
            and baseline_meta.get('finished_schedule') is True,
            'Historical native fixture evidence differs or is incomplete')
    historical_schedule = core.verified_schedule(baseline.parent, signature)
    require([row for row in historical_schedule if row['test'] in FIXED_TESTS] == schedule,
            'The selected official pair differs from the original measured URL77 schedule')
    native_fixture = signature['native_fixture']
    require(native_fixture['host_map_mode'] == 'exact' and native_fixture['host_map'], 'Exact native fixture map is absent')
    config = REPO / 'output/wpt/server.config.json'
    ca = REPO / 'output/wpt/upstream/tools/certs/cacert.pem'
    readiness = args.fixture_manifest.resolve(strict=True)
    require(digest(readiness) == READINESS_SHA, 'Fresh v36 fixture evidence changed')
    fixture = json.loads(readiness.read_text())
    require(digest(config) == signature['server_config_sha256'] and digest(ca) == native_fixture['ssl_cert_sha256'],
            'Server or CA differs from the original measured case')
    require(fixture.get('fixture_ready') is True and fixture.get('server_identity_error') is None
            and fixture['reporter_sha256'] == REPORTER_SHA and fixture['server_config_sha256'] == digest(config)
            and fixture['ca_sha256'] == digest(ca) and fixture['wpt_revision'] == inventory_value['wpt_revision']
            and fixture['manifest_sha256'] == inventory_value['manifest_sha256'], 'Fresh reporter fixture did not pass readiness')
    observed = subprocess.run(['ps', '-p', str(fixture['server_pid']), '-o', 'lstart='], text=True,
                              capture_output=True, timeout=5, check=True).stdout.strip()
    require(observed == fixture['server_start_identity'], 'Verified fixture process was replaced')
    reference_path = REPO / 'output/wpt/reference-browsers/cft-156.0.8078.47-mac-arm64-001/artifact-integrity-v1.json'
    reference = adoption.verify_reference(reference_path, REPO)
    native = REPO / ('build/lumen-wpt-' + NATIVE_SHA[:16])
    require(not native.is_symlink() and digest(native) == NATIVE_SHA and not native.stat().st_mode & 0o222
            and os.access(native, os.X_OK), 'Immutable clone native snapshot changed')
    receipt_path = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-clone-brand-snapshot.json'
    require(digest(receipt_path) == RECEIPT_SHA, 'Measured clone snapshot receipt changed')
    receipt = json.loads(receipt_path.read_text())
    require(receipt.get('sha256') == NATIVE_SHA and receipt.get('snapshot') == str(native)
            and receipt.get('bytes') == native.stat().st_size, 'Clone package receipt differs from its snapshot')
    base_receipt_path = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-form-worker-security-snapshot.json'
    require(digest(base_receipt_path) == BASE_RECEIPT_SHA, 'Prior form/security generation receipt changed')
    expected_sources = dict(json.loads(base_receipt_path.read_text())['source_hashes'])
    expected_sources.update(GENERATION_SOURCE_UPDATES)
    require(receipt.get('schema') == 'lumen-clone-brand-snapshot-v41'
            and receipt.get('source_hashes') == expected_sources
            and receipt.get('previous_snapshot_receipt_sha256') == BASE_RECEIPT_SHA
            and receipt.get('structured_clone_full_conformance_claimed') is False,
            'New generation does not contain the exact gated clone source manifest')
    frozen_evidence = set()
    for name, saved in receipt.get('input_records', {}).items():
        path = Path(name)
        require(path.is_absolute() and path.is_relative_to(REPO), 'Snapshot input record escaped the project')
        if path.is_relative_to(REPO / 'output/wpt'):
            require(not path.is_symlink() and path.resolve(strict=True).is_relative_to(REPO / 'output/wpt'),
                    'Snapshot evidence input escaped the project')
            st = path.stat()
            require(saved['identity'] == [st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns, st.st_ctime_ns]
                    and digest(path) == saved['sha256'], 'Frozen gate evidence changed: ' + name)
            frozen_evidence.add(path)
    require(base_receipt_path in frozen_evidence, 'Snapshot lacks its recorded previous receipt')
    package_path = (REPO / receipt['package_monitor']).resolve(strict=True)
    require(package_path.is_relative_to(REPO / 'output/wpt'), 'Package monitor escaped project output')
    package = json.loads(package_path.read_text())
    require(package.get('natural_exit_code') == 0 and package.get('ended_by_monitor') is False
            and not package.get('collector_error') and not package.get('violations')
            and not (package.get('owned_group_cleanup') or {}).get('error'), 'Clone package did not complete safely')
    dependencies = {native, receipt_path, package_path, base_receipt_path, baseline, baseline.parent / 'schedule.json'} | frozen_evidence
    for label in ('query_body_gate_report', 'javascript_url_gate_report', 'iframe_bootstrap_gate_report',
                  'iframe_behavior_gate_report', 'url_idl_gate_report', 'form_enctype_gate_report', 'worker_security_gate_report',
                  'clone_brand_gate_report'):
        gate_path = (REPO / receipt[label]).resolve(strict=True)
        require(gate_path.is_relative_to(REPO / 'output/wpt'), 'Native gate evidence escaped project output')
        gate = json.loads(gate_path.read_text())
        direct_monitor_schema = label in ('iframe_behavior_gate_report', 'url_idl_gate_report') or label in NEW_GATE_NAMES
        if direct_monitor_schema:
            require(gate.get('status') == 'passed' and gate.get('stages'),
                    'The actual native gate has not passed: ' + label)
            if label == 'iframe_behavior_gate_report':
                require(len(gate['stages']) == 9, 'The Blob/data-URI gate does not contain its nine stages')
                evidence_fields = ('source_receipt', 'ctype_correction_receipt',
                                   'retained_owner_test_correction_receipt', 'red_receipt')
            elif label == 'url_idl_gate_report':
                require([stage.get('name') for stage in gate['stages']] == IDL_GATE_NAMES,
                        'The IDL gate does not contain its nine declared native stages')
                evidence_fields = ('source_receipt', 'red_receipt')
            else:
                require([stage.get('name') for stage in gate['stages']] == NEW_GATE_NAMES[label],
                        'The native gate does not contain its exact declared stage sequence')
                evidence_fields = (('source_receipt', 'red_receipt', 'correction_receipt', 'build_monitor')
                                   if label == 'clone_brand_gate_report' else ('source_receipt', 'red_receipt'))
            for field in evidence_fields:
                evidence = (REPO / gate[field]).resolve(strict=True)
                require(evidence.is_file() and evidence.is_relative_to(REPO / 'output/wpt'),
                        'Native source/RED/correction evidence escaped project output')
                dependencies.add(evidence)
        else:
            require(gate.get('complete') is True and gate.get('stages'), 'Native gate report is incomplete: ' + label)
        dependencies.add(gate_path)
        if label == 'clone_brand_gate_report':
            require(digest(gate_path) == '49b4ccfa89e234dce9f4839076617ff6e88b6643ff91e2bcc7a0fe1035d9b013',
                    'The actual ten-stage clone GREEN receipt changed')
            build = json.loads((REPO / gate['build_monitor']).read_text())
            cleanup = build.get('owned_group_cleanup') or {}
            require(build.get('natural_exit_code') == 0 and build.get('ended_by_monitor') is False
                    and not build.get('collector_error') and not build.get('violations')
                    and not cleanup.get('error') and not cleanup.get('sent_sigterm') and not cleanup.get('sent_sigkill'),
                    'The corrected clone build monitor is not clean')
        for stage in gate['stages']:
            if direct_monitor_schema:
                require(stage.get('passed') is True and stage.get('natural_exit_code') == 0,
                        'Native gate stage did not pass: ' + str(stage.get('name')))
                monitor_path = (REPO / stage['monitor']).resolve(strict=True)
            else:
                require(stage.get('returncode') == 0 and stage.get('natural_exit_code') == 0
                        and stage.get('ended_by_monitor') is False and not stage.get('collector_error')
                        and not stage.get('owned_group_cleanup_error') and not stage.get('violations'),
                        'Native gate stage did not complete safely: ' + str(stage.get('stage')))
                monitor_path = (REPO / stage['report']).resolve(strict=True)
            require(monitor_path.is_relative_to(REPO / 'output/wpt'), 'Native gate monitor escaped project output')
            monitor = json.loads(monitor_path.read_text())
            require(monitor.get('natural_exit_code') == 0 and monitor.get('ended_by_monitor') is False
                    and not monitor.get('collector_error') and not monitor.get('violations')
                    and not (monitor.get('owned_group_cleanup') or {}).get('error'),
                    'Native gate monitor contradicts its stage report')
            dependencies.add(monitor_path)
    for name in ('icudtl.dat', 'cacert.pem'):
        candidate = native.parent / name
        if candidate.is_file():
            dependencies.add(candidate.resolve(strict=True))
    linked = subprocess.run(['otool', '-L', str(native)], capture_output=True, text=True, timeout=15, check=True).stdout
    for line in linked.splitlines()[1:]:
        candidate = line.strip().split(' (compatibility version', 1)[0]
        if candidate.startswith(('/usr/lib/', '/System/Library/')):
            continue
        require(candidate.startswith('/'), 'Unresolved native dependency: ' + candidate)
        dependencies.add(Path(candidate).resolve(strict=True))
    def recorded(path):
        before = path.stat()
        identity = [before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns]
        value = {'path': str(path), 'sha256': digest(path), 'identity': identity}
        after = path.stat()
        require(identity == [after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns],
                'Native generation input changed while hashing: ' + str(path))
        return value
    native_dependencies = [recorded(path) for path in sorted(dependencies)]
    output = (args.output or Path('output/wpt/gpt-6-astra-pro-devinmac3d-clone-historical2-cft156-v41')).resolve()
    require(output.is_relative_to(REPO / 'output/wpt'), 'Fixed-suite evidence must remain in project output/wpt')
    common = {'suite': 'clone-historical2', 'core_sha256': CORE_SHA, 'reporter_sha256': REPORTER_SHA, 'inventory_sha256': INVENTORY_SHA,
              'fixture_manifest_sha256': digest(readiness), 'reference_manifest_sha256': digest(reference_path),
              'reference_tree_sha256': reference['integrity']['tree_sha256'], 'native_sha256': NATIVE_SHA,
              'schedule_sha256': core.digest(schedule), 'selection': selection,
              'native_dependencies': native_dependencies, 'launcher_sha256': digest(__file__)}
    if args.action == 'compare':
        summaries, incidents = {}, []
        for engine, expected_binary in (('lumen', NATIVE_SHA), ('chromium', reference['binary_sha256'])):
            directory = output / engine
            launch = json.loads((output / (engine + '.launch.json')).read_text())
            require(launch['inputs'] == common, 'Fixed-suite launch inputs changed')
            meta = json.loads((directory / 'run.json').read_text())
            actual = meta['signature']
            require(actual['engine'] == engine and actual['binary_sha256'] == expected_binary and actual['runner_sha256'] == CORE_SHA
                    and actual['reporter_sha256'] == REPORTER_SHA and actual['fixture_manifest_sha256'] == common['fixture_manifest_sha256'],
                    'Fixed-suite engine identity or reporter differs')
            require(meta.get('finished_schedule') and core.verified_schedule(directory, actual) == schedule,
                    'Fixed-suite run did not complete its exact schedule')
            rows = list(core.iter_jsonl(directory / 'results.jsonl'))
            require(len(rows) == expected_files, 'Focused run does not contain every selected official file')
            for index, row in enumerate(rows):
                require((row['type'], row['test']) == (schedule[index]['type'], schedule[index]['test'])
                        and row['status'] != 'INFRA_ERROR'
                        and (row.get('reporter_state') or {}).get('status') == 'ATTACHED',
                        'Focused report is incomplete or malformed: ' + row['test'])
                cleanup = row.get('cleanup') or {}
                require(not cleanup.get('error') and not cleanup.get('errors') and not cleanup.get('profile_error')
                        and not core.measurement_failure(row), 'Focused case cleanup or telemetry failed: ' + row['test'])
            known = engine == 'chromium' and policy.known_reference_teardown(meta)
            stops = [key for key, value in meta.items() if key.startswith('stopped_for_') and value
                     and not (known and key == 'stopped_for_cleanup_failure')]
            require(not stops, 'Fixed-suite run has other unsafe stop conditions')
            if engine == 'chromium':
                require((meta.get('browser_version') or {}).get('product', '').split('/')[-1] == '156.0.8078.47',
                        'Actual reference version changed')
                require(bool(meta.get('chromium_sessions')), 'Reference telemetry session evidence is absent')
                process = (meta.get('chromium_cleanup') or {}).get('process') or {}
                if known:
                    require(len(meta.get('chromium_sessions', [])) == 1, 'Known incident must belong to one fresh session')
                    log = Path(meta['chromium_sessions'][0]) / 'chromium-process.log'
                    require(log.resolve(strict=True).is_relative_to(directory.resolve()), 'Reference log escaped this Fixed-suite run')
                    with log.open('rb') as source:
                        source.seek(max(0, log.stat().st_size - 65536)); tail = source.read(65536)
                    require(b'Teardown watchdog expired; recording dump and terminating.' in tail,
                            'Reference exit2 has no exact watchdog diagnostic')
                    incidents.append({'engine': engine, 'cleanup': meta['chromium_cleanup'], 'browser_log_sha256': digest(log)})
                else:
                    require(not (meta.get('chromium_cleanup') or {}).get('errors') and not process.get('error')
                            and not process.get('teardown_error') and process.get('root_exit_code') == 0
                            and process.get('root_exit_before_signal_cleanup') == 0 and process.get('root_exit_observation_verified') is True
                            and process.get('sent_sigterm') is False and process.get('sent_sigkill') is False
                            and process.get('process_group_gone') is True and process.get('remaining_group_members') == {},
                            'Reference cleanup is outside the existing explicit functional-only policy')
                for session in meta.get('chromium_sessions', []):
                    metrics = json.loads((Path(session) / 'chromium.metrics-summary.json').read_text())
                    require(not core.measurement_failure({'metrics': metrics}), 'Reference session telemetry failed')
            assertions = Counter(item['status'] for row in rows for item in row.get('subtests', []))
            summaries[engine] = {'cases': [{'test': row['test'], 'harness_status': row['status'],
                                           'assertion_counts': dict(Counter(item['status'] for item in row.get('subtests', []))),
                                           'reporter_diagnostics': row.get('reporter_diagnostics')} for row in rows],
                                 'files': len(rows), 'fully_passing_files': sum(core.row_passes(row) for row in rows),
                                 'harness_counts': dict(Counter(row['status'] for row in rows)),
                                 'assertion_counts': dict(assertions),
                                 'identical_completion_repeats': sum((row.get('reporter_diagnostics') or {}).get(
                                     'identical_completion_repeats', 0) for row in rows)}
        report = output / 'comparison.json'
        require(not report.exists(), 'Preserve the existing matched report')
        core.compare_command(argparse.Namespace(lumen=output / 'lumen', chromium=output / 'chromium', output=report))
        value = json.loads(report.read_text())
        value['matched_private_clone_fixed_suite'] = {'inputs': common, 'observed_results': summaries,
            'classification': 'functional_only_reference_teardown_failed',
            'reference_performance_release_gate_passed': False, 'current_reference_incidents': incidents,
            'note': 'The explicit reference policy remains in force. The exact original historical window and worker files are compared without expectation changes. This focused measurement is not a full WPT or structured-clone conformance claim.'}
        report.write_text(json.dumps(value, indent=2) + '\n')
        print(json.dumps({'report': str(report), 'results': summaries, 'reference_incidents': len(incidents)}))
        return
    directory = output / args.action
    launch = output / (args.action + '.launch.json')
    require(not directory.exists() and not launch.exists(), 'Use a fresh Fixed-suite run; retain prior evidence')
    command = [sys.executable, str(here / 'full_runner.py'), 'run', '--engine', args.action,
               '--inventory', str(inventory), '--types', 'testharness',
               '--output', str(directory), '--timeout', '15', '--long-timeout', '65',
               '--origin', signature['origin'], '--secure-origin', signature['secure_origin'], '--h2-origin', signature['h2_origin'],
               '--server-config', str(config), '--fixture-manifest', str(readiness), '--strict-infrastructure']
    for test in prefixes:
        command.extend(['--prefix', test])
    if args.action == 'lumen':
        command.extend(['--binary', str(native)])
    else:
        smoke = json.loads((REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-cft156-url-smoke-v34/chromium/run.json').read_text())
        flags = smoke['signature']['chromium_args']
        require(len(flags) == 2 and sum(flag.startswith('--host-resolver-rules=') for flag in flags) == 1
                and sum(flag.startswith('--ignore-certificate-errors-spki-list=') for flag in flags) == 1,
                'Reference fixture flags changed')
        command.extend(['--binary', reference['binary']])
        for flag in flags:
            command.append('--chromium-arg=' + flag)
    os.environ.update(LUMEN_HOST_MAP=native_fixture['host_map'], LUMEN_HOST_MAP_MODE='exact', SSL_CERT_FILE=str(ca))
    for key in ('LUMEN_PRE_FILE', 'LUMEN_SHOT', 'LUMEN_SHOT_MS', 'LUMEN_NO_JS'):
        os.environ.pop(key, None)
    output.mkdir(parents=True, exist_ok=True)
    with launch.open('x') as destination:
        json.dump({'inputs': common, 'argv': command, 'engine': args.action}, destination, indent=2)
        destination.write('\n')
    print(json.dumps({'engine': args.action, 'scheduled_files': expected_files, 'output': str(directory)}), flush=True)
    os.execv(sys.executable, command)


if __name__ == '__main__':
    main()
