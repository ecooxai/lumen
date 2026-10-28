#!/usr/bin/env python3
"""Fresh nine-file measurement of gated form reflection and worker security.

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
BASE_RECEIPT_SHA = 'b3c15f76ca9881b0b3e8b86de0c9cbbc601e41a0328a42aaaa102c4c701e377e'
HISTORICAL_NATIVE_SHA = 'b5b2ee8c80685a644a998b8b2cdbe527a372e6c2d77b408a5d0d657c7e65151a'
READINESS_SHA = '8ac07a9ac8d163cfd868d4172cf82d837020573c9999365faf84be6a789682e7'
POLICY_SHA = '9e4954758177647d82cda5a26d39307cf07bcbfde21fc2e5349cd2065b29f8af'
ADOPTION_SHA = 'bb030858a2082b49dac1cd661ff2707de2168046dd2b435c2fd8dac06289fb3d'
INVENTORY_SHA = '9ffd9444520fd3158b7566f37600b0f10b56bbded1ccbd9b1b1ed8166a836053'
FORM_TESTS = [
    '/FileAPI/file/send-file-form-controls.html',
    '/FileAPI/file/send-file-form-iso-2022-jp.html',
    '/FileAPI/file/send-file-form-punctuation.html',
    '/FileAPI/file/send-file-form-utf-8.html',
    '/FileAPI/file/send-file-form-windows-1252.html',
    '/FileAPI/file/send-file-form-x-user-defined.html',
    '/FileAPI/file/send-file-form.html',
]
WORKER_TESTS = ['/secure-contexts/basic-dedicated-worker.html',
                '/secure-contexts/basic-dedicated-worker.https.html']
FIXED_TESTS = FORM_TESTS + WORKER_TESTS
NEW_GATE_NAMES = {
    'form_enctype_gate_report': ['test_form_dialog', 'test_url_dom'],
    'worker_security_gate_report': ['test_worker_security', 'test_worker_lifecycle',
                                   'test_url_idl_window', 'test_url_idl_worker', 'test_capture_policy'],
}
IDL_GATE_NAMES = ['test_url_idl_window', 'test_url_idl_worker', 'test_url_params',
                  'test_worker_lifecycle', 'test_worker_lifecycle_exit_with_worker',
                  'test_url_dom', 'test_history_origin', 'test_capture_policy', 'test_beacon']

GENERATION_SOURCE_UPDATES = {'Makefile': '4373777ca7df8beefc412b1e337acb1c30173da45a795431bb52e4d8b66cd0d4', 'src/js/prelude/03-element.js': '7642cb0921317c8c20cb3e1639fa018832ef9fd38b1b9133b8e6fada79c82019', 'src/js/prelude/04-tags.js': '602808f1955f948428fe91506e4613604fcfc3373d320f8405e4eb4469e3c5af', 'src/js/prelude/06b-activate.js': 'f9cdb122860ce7a8bb6cc4c7e7e68dffdfe0bc3533a13967a60156e8d3a9f71d', 'src/js/worker.cc': '5e2f2b23863d0f576b48f6e73dc3380566eaf42dab7128eb01923f35c358b2b1', 'tests/test_form_dialog.cc': '8656e9c92155059d2ce556104e8f4860d6c34688c2b7bf7e566656b8921a18d4', 'tests/test_worker_security.cc': 'f822adc9045443280d127c6d9b615659a11cd1bc28d65adee6253afe49ebcbff'}

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
    core = module(here / 'full_runner.py', CORE_SHA, 'form_worker_fixed_suite_core')
    policy = module(here / 'run_cft156_url77.py', POLICY_SHA, 'form_worker_fixed_suite_policy')
    adoption = module(here / 'adopt_cft156.py', ADOPTION_SHA, 'form_worker_fixed_suite_adoption')
    reporter = here / 'testharnessreport.js'
    require(digest(reporter) == REPORTER_SHA, 'Served reporter changed')
    inventory = REPO / 'output/wpt/inventory.json'
    require(digest(inventory) == INVENTORY_SHA, 'Pinned upstream inventory changed')
    inventory_value = json.loads(inventory.read_text())
    prefixes, expected_files = FIXED_TESTS, 9
    schedule, selection = core.select_schedule(inventory_value, 'testharness', prefixes, 0, None)
    require(len(schedule) == expected_files and [row['test'] for row in schedule] == FIXED_TESTS,
            'Selection is not the exact seven form files and two worker security files')
    baseline = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-url-native-shared-worker-v34/lumen/run.json'
    signature = json.loads(baseline.read_text())['signature']
    require(signature['binary_sha256'] == HISTORICAL_NATIVE_SHA and signature['inventory_sha256'] == INVENTORY_SHA,
            'Historical native fixture evidence differs')
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
            and os.access(native, os.X_OK), 'Immutable form/worker native snapshot changed')
    receipt_path = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-form-worker-security-snapshot.json'
    require(digest(receipt_path) == RECEIPT_SHA, 'Measured form/worker snapshot receipt changed')
    receipt = json.loads(receipt_path.read_text())
    require(receipt.get('sha256') == NATIVE_SHA and receipt.get('snapshot') == str(native)
            and receipt.get('bytes') == native.stat().st_size, 'Form/worker package receipt differs from its snapshot')
    base_receipt_path = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-blob-idl-snapshot.json'
    require(digest(base_receipt_path) == BASE_RECEIPT_SHA, 'Prior Blob/IDL generation receipt changed')
    expected_sources = dict(json.loads(base_receipt_path.read_text())['source_hashes'])
    expected_sources.update(GENERATION_SOURCE_UPDATES)
    require(receipt.get('schema') == 'lumen-form-worker-security-snapshot-v40'
            and receipt.get('source_hashes') == expected_sources
            and receipt.get('previous_snapshot_receipt_sha256') == BASE_RECEIPT_SHA
            and receipt.get('nested_worker_support_added') is False,
            'New generation does not contain the exact gated form/security source manifest')
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
            and not (package.get('owned_group_cleanup') or {}).get('error'), 'Form/worker package did not complete safely')
    dependencies = {native, receipt_path, package_path, base_receipt_path} | frozen_evidence
    for label in ('query_body_gate_report', 'javascript_url_gate_report', 'iframe_bootstrap_gate_report',
                  'iframe_behavior_gate_report', 'url_idl_gate_report', 'form_enctype_gate_report', 'worker_security_gate_report'):
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
                        'The form/security gate does not contain its exact native stage sequence')
                evidence_fields = ('source_receipt', 'red_receipt')
            for field in evidence_fields:
                evidence = (REPO / gate[field]).resolve(strict=True)
                require(evidence.is_file() and evidence.is_relative_to(REPO / 'output/wpt'),
                        'Native source/RED/correction evidence escaped project output')
                dependencies.add(evidence)
        else:
            require(gate.get('complete') is True and gate.get('stages'), 'Native gate report is incomplete: ' + label)
        dependencies.add(gate_path)
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
    output = (args.output or Path('output/wpt/gpt-6-astra-pro-devinmac3d-form-worker9-cft156-v40')).resolve()
    require(output.is_relative_to(REPO / 'output/wpt'), 'Fixed-suite evidence must remain in project output/wpt')
    common = {'suite': 'form7-worker2', 'core_sha256': CORE_SHA, 'reporter_sha256': REPORTER_SHA, 'inventory_sha256': INVENTORY_SHA,
              'fixture_manifest_sha256': digest(readiness), 'reference_manifest_sha256': digest(reference_path),
              'reference_tree_sha256': reference['integrity']['tree_sha256'], 'native_sha256': NATIVE_SHA,
              'schedule_sha256': core.digest(schedule), 'selection': selection,
              'native_dependencies': native_dependencies, 'launcher_sha256': digest(__file__)}
    if args.action == 'compare':
        summaries, incidents, engine_rows = {}, [], {}
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
            engine_rows[engine] = rows
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
        area_breakdown = {}
        for group, tests in (('form_upload', FORM_TESTS), ('worker_security', WORKER_TESTS)):
            selected = [row for row in schedule if row['test'] in tests]
            lr = [row for row in engine_rows['lumen'] if row['test'] in tests]
            cr = [row for row in engine_rows['chromium'] if row['test'] in tests]
            area_breakdown[group] = {'scheduled_files': len(selected), 'parity': core.parity(lr, cr, selected)}
            for engine, rows in (('lumen', lr), ('chromium', cr)):
                area_breakdown[group][engine] = {
                    'files': len(rows), 'fully_passing_files': sum(core.row_passes(row) for row in rows),
                    'harness_counts': dict(Counter(row['status'] for row in rows)),
                    'assertion_counts': dict(Counter(sub['status'] for row in rows for sub in row.get('subtests', [])))}
        value['matched_form_worker_fixed_suite'] = {'inputs': common, 'observed_results': summaries,
            'area_breakdown': area_breakdown, 'classification': 'functional_only_reference_teardown_failed',
            'reference_performance_release_gate_passed': False, 'current_reference_incidents': incidents,
            'note': 'The explicit reference policy remains in force. Every selected original file outcome remains in the report. Form upload and worker security are reported separately; this is not a full WPT pass rate. Nested workers and remaining submission behavior are genuine outcomes, not declared fixed capabilities.'}
        report.write_text(json.dumps(value, indent=2) + '\n')
        print(json.dumps({'report': str(report), 'results': summaries, 'reference_incidents': len(incidents), 'area_breakdown': area_breakdown}))
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
