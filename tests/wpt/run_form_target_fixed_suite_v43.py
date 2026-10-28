#!/usr/bin/env python3
"""Fresh unchanged nine-file form-upload and dedicated-worker measurement.

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
BASE_RECEIPT_SHA = '53141212eb423a3287b656eaa6b33b1e09d709408e9fb5b2d5fb7adf750ae9ad'
HISTORICAL_NATIVE_SHA = 'b5b2ee8c80685a644a998b8b2cdbe527a372e6c2d77b408a5d0d657c7e65151a'
READINESS_SHA = '8ac07a9ac8d163cfd868d4172cf82d837020573c9999365faf84be6a789682e7'
POLICY_SHA = '9e4954758177647d82cda5a26d39307cf07bcbfde21fc2e5349cd2065b29f8af'
ADOPTION_SHA = 'bb030858a2082b49dac1cd661ff2707de2168046dd2b435c2fd8dac06289fb3d'
INVENTORY_SHA = '9ffd9444520fd3158b7566f37600b0f10b56bbded1ccbd9b1b1ed8166a836053'
FORM_TESTS = ['/FileAPI/file/send-file-form-controls.html', '/FileAPI/file/send-file-form-iso-2022-jp.html', '/FileAPI/file/send-file-form-punctuation.html', '/FileAPI/file/send-file-form-utf-8.html', '/FileAPI/file/send-file-form-windows-1252.html', '/FileAPI/file/send-file-form-x-user-defined.html', '/FileAPI/file/send-file-form.html']
WORKER_TESTS = ['/secure-contexts/basic-dedicated-worker.html', '/secure-contexts/basic-dedicated-worker.https.html']
FIXED_TESTS = FORM_TESTS + WORKER_TESTS
FORM_GATE_SHA = 'a75dfc0398c9b514dc4c03d0d4f061a44fde4a6bced705f644fe2c783c98d06e'
NEW_GATE_NAMES = {
    'form_target_gate_report': ['test_form_navigation', 'test_form_dialog', 'test_url_dom', 'test_window_lifecycle', 'test_capture_policy', 'test_history_origin', 'test_file_reader_window'],
    'file_reader_gate_report': ['test_file_reader_window', 'test_file_reader_worker', 'test_encoding', 'test_clone_brand_window', 'test_clone_brand_worker', 'test_worker_lifecycle', 'test_worker_exit', 'test_worker_security', 'test_url_idl_window', 'test_url_idl_worker', 'test_url_dom', 'test_url_params', 'test_history_origin', 'test_capture_policy', 'test_beacon', 'test_form_dialog', 'test_promise_rejections', 'test_window_lifecycle'],
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

GENERATION_SOURCE_UPDATES = {'Makefile': '11d81f671368ac1f2a00e05bc8bf92d37fc295e7626c59328d3f87388be8a19d', 'src/js/form_navigation.h': 'bbe76d25418a7c4d185fc2b2957bfc58c5e8c2a5e9e71f90b20e6ab9218dc15c', 'src/js/frames.cc': '6358507fdd6835a900430d3b8e2d83ef9043d58c5b4ee9d2268040f60e8b5c88', 'src/js/js_int.h': '5c0f8b3faf31b0607e4410ad2678c1a41e928f81f774644001377c2f84dc379e', 'src/js/prelude/04-tags.js': 'b41753fa5179ded0455079601c0113a27e9f7949eec1ac68c90b5433eb681622', 'src/js/prelude/06b-activate.js': '41241a133cb83d5861660c50ff25621adf48a7af9ab2f4ddd38752602a7398f6', 'src/js/windows.cc': 'eb01f84a6dbfbb1cb0e3a848e5b3a2480d8b9aa140c0f6c6492708dbf3d86aff', 'src/js/windows.h': '87a033687a32177896cb7b66097f22b24737ba245637dd5a71e4def9de60cc54', 'src/net/csp.c': '4393e16a148cfe03ae7203c894d54cd5a0ab8db16efc4139ddbe0eb6353afe7b', 'tests/test_form_dialog.cc': 'f412336c7753f43adc64c65d74635cc7df4ca6a74dba2c40406d0489f145dbd9', 'tests/test_form_navigation.cc': 'a8d3624be4188dc58b6d523d589d7809fc43a64d4886791fafe492160ac64bdb'}

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
    core = module(here / 'full_runner.py', CORE_SHA, 'form_target_fixed_suite_core')
    policy = module(here / 'run_cft156_url77.py', POLICY_SHA, 'form_target_fixed_suite_policy')
    adoption = module(here / 'adopt_cft156.py', ADOPTION_SHA, 'form_target_fixed_suite_adoption')
    reporter = here / 'testharnessreport.js'
    require(digest(reporter) == REPORTER_SHA, 'Served reporter changed')
    inventory = REPO / 'output/wpt/inventory.json'
    require(digest(inventory) == INVENTORY_SHA, 'Pinned upstream inventory changed')
    inventory_value = json.loads(inventory.read_text())
    prefixes, expected_files = FIXED_TESTS, 9
    schedule, selection = core.select_schedule(inventory_value, 'testharness', prefixes, 0, None)
    require(len(schedule) == expected_files and [row['test'] for row in schedule] == FIXED_TESTS,
            'Selection is not the unchanged seven form files and two dedicated-worker files')
    original_form_run = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-form-worker9-cft156-v40/lumen/run.json'
    original_form_meta = json.loads(original_form_run.read_text())
    original_form_signature = original_form_meta['signature']
    require(original_form_signature['binary_sha256'] ==
            '21a3dfddf8da1f6948be6b3f51184ec8a0800b1c9eca49b3cacd2ba610e680af'
            and original_form_signature['inventory_sha256'] == INVENTORY_SHA
            and original_form_signature['runner_sha256'] == CORE_SHA
            and original_form_signature['reporter_sha256'] == REPORTER_SHA
            and original_form_meta.get('finished_schedule') is True
            and core.verified_schedule(original_form_run.parent, original_form_signature) == schedule,
            'Original v40 measured schedule differs from the new unfiltered nine-file schedule')
    baseline = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-url-native-shared-worker-v34/lumen/run.json'
    baseline_meta = json.loads(baseline.read_text())
    signature = baseline_meta['signature']
    require(signature['binary_sha256'] == HISTORICAL_NATIVE_SHA and signature['inventory_sha256'] == INVENTORY_SHA
            and baseline_meta.get('finished_schedule') is True,
            'Historical native fixture evidence differs or is incomplete')
    historical_schedule = core.verified_schedule(baseline.parent, signature)
    require(len(historical_schedule) == 77, 'Original fixture baseline lacks its complete URL77 schedule')
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
            and os.access(native, os.X_OK), 'Immutable form routing native snapshot changed')
    receipt_path = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-form-target-snapshot.json'
    require(digest(receipt_path) == RECEIPT_SHA, 'Measured form routing snapshot receipt changed')
    receipt = json.loads(receipt_path.read_text())
    require(receipt.get('sha256') == NATIVE_SHA and receipt.get('snapshot') == str(native)
            and receipt.get('bytes') == native.stat().st_size, 'Form routing package receipt differs from its snapshot')
    base_receipt_path = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-file-reader-snapshot.json'
    require(digest(base_receipt_path) == BASE_RECEIPT_SHA, 'Prior FileReader generation receipt changed')
    expected_sources = dict(json.loads(base_receipt_path.read_text())['source_hashes'])
    expected_sources.update(GENERATION_SOURCE_UPDATES)
    require(receipt.get('schema') == 'lumen-form-target-snapshot-v43'
            and receipt.get('source_hashes') == expected_sources
            and receipt.get('previous_snapshot_receipt_sha256') == BASE_RECEIPT_SHA
            and receipt.get('form_submission_full_conformance_claimed') is False
            and receipt.get('nested_worker_support_added') is False
            and receipt.get('cross_document_top_targets_supported') is False
            and receipt.get('new_auxiliary_targets_supported') is False
            and receipt.get('top_self_redirect_form_policy_added') is False
            and receipt.get('multipart_or_charset_conformance_claimed') is False
            and receipt.get('correction_receipt_sha256') ==
            'e9806a360bd5b722ff311a5f62419376ebb671ba8a0f077e5af4b1fd3e5ea63f'
            and receipt.get('production_receipt_sha256') ==
            '1bb32dc8b5ef6a9d27fbfe062ea4f992ebab8410ceb8e83a9ed27138ad440414',
            'New generation does not contain the exact gated form routing source/evidence manifest')
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
            and not (package.get('owned_group_cleanup') or {}).get('error'), 'Form routing package did not complete safely')
    dependencies = {native, receipt_path, package_path, base_receipt_path, baseline, baseline.parent / 'schedule.json',
                    original_form_run, original_form_run.parent / 'schedule.json'} | frozen_evidence
    for label in ('query_body_gate_report', 'javascript_url_gate_report', 'iframe_bootstrap_gate_report',
                  'iframe_behavior_gate_report', 'url_idl_gate_report', 'form_enctype_gate_report', 'worker_security_gate_report',
                  'clone_brand_gate_report', 'file_reader_gate_report', 'form_target_gate_report'):
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
                                   if label in ('clone_brand_gate_report', 'form_target_gate_report') else
                                   ('source_receipt', 'red_receipt', 'build_monitor') if label == 'file_reader_gate_report' else
                                   ('source_receipt', 'red_receipt'))
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
        if label == 'file_reader_gate_report':
            require(gate_path in frozen_evidence and digest(gate_path) ==
                    '3a1a5cbad44b978ad7f3dc70c123278c269dede6f5c3cb4f37a425cfcae18528',
                    'The actual eighteen-stage FileReader GREEN receipt changed')
            build_path = (REPO / gate['build_monitor']).resolve(strict=True)
            require(build_path in frozen_evidence, 'The FileReader build monitor is not frozen evidence')
            build = json.loads(build_path.read_text())
            cleanup = build.get('owned_group_cleanup') or {}
            require(build.get('natural_exit_code') == 0 and build.get('ended_by_monitor') is False
                    and not build.get('collector_error') and not build.get('violations')
                    and not cleanup.get('error') and not cleanup.get('sent_sigterm') and not cleanup.get('sent_sigkill'),
                    'The FileReader build monitor is not clean')
        if label == 'form_target_gate_report':
            require(gate_path in frozen_evidence and digest(gate_path) == FORM_GATE_SHA,
                    'The actual seven-stage form GREEN receipt changed')
            for field in ('source_receipt', 'red_receipt', 'correction_receipt', 'build_monitor'):
                require((REPO / gate[field]).resolve(strict=True) in frozen_evidence,
                        'The form gate input is not frozen evidence: ' + field)
            build = json.loads((REPO / gate['build_monitor']).read_text())
            cleanup = build.get('owned_group_cleanup') or {}
            require(build.get('natural_exit_code') == 0 and build.get('ended_by_monitor') is False
                    and not build.get('collector_error') and not build.get('violations')
                    and not cleanup.get('error') and not cleanup.get('sent_sigterm') and not cleanup.get('sent_sigkill'),
                    'The form build monitor is not clean')
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
    output = (args.output or Path('output/wpt/gpt-6-astra-pro-devinmac3d-form-target-worker9-cft156-v43')).resolve()
    require(output.is_relative_to(REPO / 'output/wpt'), 'Fixed-suite evidence must remain in project output/wpt')
    common = {'suite': 'form-target7-worker2', 'core_sha256': CORE_SHA, 'reporter_sha256': REPORTER_SHA, 'inventory_sha256': INVENTORY_SHA,
              'fixture_manifest_sha256': digest(readiness), 'reference_manifest_sha256': digest(reference_path),
              'reference_tree_sha256': reference['integrity']['tree_sha256'], 'native_sha256': NATIVE_SHA,
              'schedule_sha256': core.digest(schedule), 'selection': selection,
              'original_v40_schedule_sha256': core.digest(schedule),
              'original_v40_run_sha256': digest(original_form_run),
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
        value['matched_form_target_worker_fixed_suite'] = {'inputs': common, 'observed_results': summaries,
            'area_breakdown': area_breakdown, 'classification': 'functional_only_reference_teardown_failed',
            'reference_performance_release_gate_passed': False, 'current_reference_incidents': incidents,
            'note': 'The explicit reference policy remains in force. Every selected original file outcome remains in the report. Form upload and worker security are reported separately; this is not a full WPT pass rate. The complete original v40 selection remains unchanged. Nested workers and remaining serialization/encoding behavior are genuine outcomes, not declared fixed capabilities.'}
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
