#!/usr/bin/env python3
"""Fresh complete 36-file automated FileReader, FileReaderSync and FileAPI IDL measurement.

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
BASE_RECEIPT_SHA = 'f398f7f17afc266e22bcd1dcae7242e0241f14cfab897d43a5691a611e973ca7'
HISTORICAL_NATIVE_SHA = 'b5b2ee8c80685a644a998b8b2cdbe527a372e6c2d77b408a5d0d657c7e65151a'
READINESS_SHA = '8ac07a9ac8d163cfd868d4172cf82d837020573c9999365faf84be6a789682e7'
POLICY_SHA = '9e4954758177647d82cda5a26d39307cf07bcbfde21fc2e5349cd2065b29f8af'
ADOPTION_SHA = 'bb030858a2082b49dac1cd661ff2707de2168046dd2b435c2fd8dac06289fb3d'
INVENTORY_SHA = '9ffd9444520fd3158b7566f37600b0f10b56bbded1ccbd9b1b1ed8166a836053'
FIXED_TESTS = ['/FileAPI/FileReader/progress_event_bubbles_cancelable.html', '/FileAPI/FileReader/workers.html', '/FileAPI/FileReaderSync.worker.html', '/FileAPI/fileReader.any.html', '/FileAPI/fileReader.any.worker.html', '/FileAPI/idlharness.any.html', '/FileAPI/idlharness.any.worker.html', '/FileAPI/idlharness.html', '/FileAPI/idlharness.worker.html', '/FileAPI/reading-data-section/Determining-Encoding.any.html', '/FileAPI/reading-data-section/Determining-Encoding.any.worker.html', '/FileAPI/reading-data-section/FileReader-event-handler-attributes.any.html', '/FileAPI/reading-data-section/FileReader-event-handler-attributes.any.worker.html', '/FileAPI/reading-data-section/FileReader-multiple-reads.any.html', '/FileAPI/reading-data-section/FileReader-multiple-reads.any.worker.html', '/FileAPI/reading-data-section/filereader_abort.any.html', '/FileAPI/reading-data-section/filereader_abort.any.worker.html', '/FileAPI/reading-data-section/filereader_error.any.html', '/FileAPI/reading-data-section/filereader_error.any.worker.html', '/FileAPI/reading-data-section/filereader_events.any.html', '/FileAPI/reading-data-section/filereader_events.any.worker.html', '/FileAPI/reading-data-section/filereader_readAsArrayBuffer.any.html', '/FileAPI/reading-data-section/filereader_readAsArrayBuffer.any.worker.html', '/FileAPI/reading-data-section/filereader_readAsBinaryString.any.html', '/FileAPI/reading-data-section/filereader_readAsBinaryString.any.worker.html', '/FileAPI/reading-data-section/filereader_readAsDataURL.any.html', '/FileAPI/reading-data-section/filereader_readAsDataURL.any.worker.html', '/FileAPI/reading-data-section/filereader_readAsDataURL.html', '/FileAPI/reading-data-section/filereader_readAsText.any.html', '/FileAPI/reading-data-section/filereader_readAsText.any.worker.html', '/FileAPI/reading-data-section/filereader_readAsText_blob_type_charset.any.html', '/FileAPI/reading-data-section/filereader_readAsText_blob_type_charset.any.worker.html', '/FileAPI/reading-data-section/filereader_readystate.any.html', '/FileAPI/reading-data-section/filereader_readystate.any.worker.html', '/FileAPI/reading-data-section/filereader_result.any.html', '/FileAPI/reading-data-section/filereader_result.any.worker.html']
SELECTION_PREFIXES = ['/FileAPI/reading-data-section/', '/FileAPI/idlharness', '/FileAPI/FileReader', '/FileAPI/fileReader']
EXPECTED_SCHEDULE_SHA = '64867af042ed9a9af8da82848e5463e4a5257fce2def8ebe55233d5704764abb'
MANUAL_NOT_RUN = ['/FileAPI/FileReader/test_errors-manual.html', '/FileAPI/FileReader/test_notreadableerrors-manual.html', '/FileAPI/FileReader/test_securityerrors-manual.html', '/FileAPI/idlharness-manual.html', '/FileAPI/reading-data-section/filereader_file-manual.html', '/FileAPI/reading-data-section/filereader_file_img-manual.html']
EXPECTED_MANUAL_SHA = '1df2ed293eed05aeac0fcd56c4ea13becfa59509af89151503d5d85b8280130f'
NEW_GATE_NAMES = {
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

GENERATION_SOURCE_UPDATES = {'Makefile': '1aeeb251128527b5d9d6801bebcbfdd9ab23982c2df67e1b632225590ac64194', 'src/base/LICENSE.whatwg-encoding': '85dc6f5ccb57a6fe8c33d158f9fc8fc7ee5655a5d3db2cdd131c6a3d0f48a864', 'src/base/encoding.c': '4d2177b07c168b6ea066e99fc39745cc2cbcf58002acd07040d0900fc78a5864', 'src/base/encoding.h': '04b4c4ee093393f3233c7618a35b715fd7198c826f0a678b4f1d685dc70fc56e', 'src/base/encoding_decode_data.inc': '759a6b5cb49e450b6aab966e176a7f410c7cdcb0e140abee88ea009a2d8d9a65', 'src/base/encoding_decode_data.provenance.json': '0f14b700f94de0a760bb91c61baae2f5788e1b6fe58a1dbf4bb5832f007c854c', 'src/js/blob_state.h': '1f7bd86022791b425c987d722ada95f83460ca944d7204b6e6f14d2f665f4197', 'src/js/clone_brand.h': '5e14f2dd9c2e6bc2c8b6a5177f55a6df4851dc8d69da56f9262a3bdb86bbf5d6', 'src/js/file_reader.h': '9de18e7581fe162671ac7c9f69c3476aee88d796870d053a3c347376e61e4044', 'src/js/idl_state.h': 'c623c345f5e7e26916b19a0fe264ef057fc0314f4b84c56dbca93eccf0581619', 'src/js/jsnative.cc': 'ae24bd6967bd33fa6232c0a1febb988ce05293012a5bc544940a403be19f6742', 'src/js/prelude/01-events.js': 'bc65fcadc60bc8de66d99accd0ebdea841cc4ffbceea4e548581ce2994f76e0e', 'src/js/prelude/06w-filereader.js': '44fb45ca87dfdedd6458912d6bbf4b8258bc16ab0dcb8db59c00be2996cbe9c9', 'src/js/prelude/07-web.js': 'ad2f51aac039b96c3e6b4809e1e632ea45388b44557f2f50d26bbdda1ab3d489', 'src/js/prelude/08-window.js': 'e88f616ec67c1250be57a2c28dcf9779134bfb9a04a200b97b61bb136d60120a', 'src/js/worker.cc': '2ca1a0415886bf6a16dbb3113758efff987bd631004f780684f8947c97a673f8', 'tests/test_encoding.c': 'def09712b1477295a200312953399fc019af7f7fe74e559022e12f4b8ea5a4e1', 'tools/generate_encoding_decode_data.py': '805ecf476afdc402c009fcc6881c40e7a41965796e7507d21328c2c2f1c7eedf', 'tools/test_generate_encoding_decode_data.py': '44955896caf2b79f9f60c307096b1b83919f71a179e5a83e30c2299c856fd88b', 'tests/test_file_reader.cc': 'd5392ebe8485311d54b8c3de5afa6e584ede5aaa219585d86d39e5d598b3b355', 'tests/js/file-reader-regression.js': '870bce3480a234b96bf52ecf490fcacc118a43f38366b4e5b3529ab7c5a5ba8f', 'tests/js/file-reader-realm-regression.js': '46f34b63128311dc078a8e72db1fac3428b3ea5261780fb23b907275ef1fb589', 'tests/js/worker-file-reader-regression.js': '8583721257fa505451deea06197ec3239b4602fa9453b5c525fbe7d5b62cdfaf', 'tests/test_promise_rejections.cc': 'd14c48cff9de131a28d73a037b951687a5b3110dfa48360d0e99d246174887b0'}

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
    core = module(here / 'full_runner.py', CORE_SHA, 'file_reader_fixed_suite_core')
    policy = module(here / 'run_cft156_url77.py', POLICY_SHA, 'file_reader_fixed_suite_policy')
    adoption = module(here / 'adopt_cft156.py', ADOPTION_SHA, 'file_reader_fixed_suite_adoption')
    reporter = here / 'testharnessreport.js'
    require(digest(reporter) == REPORTER_SHA, 'Served reporter changed')
    inventory = REPO / 'output/wpt/inventory.json'
    require(digest(inventory) == INVENTORY_SHA, 'Pinned upstream inventory changed')
    inventory_value = json.loads(inventory.read_text())
    prefixes, expected_files = SELECTION_PREFIXES, 36
    schedule, selection = core.select_schedule(inventory_value, 'testharness', prefixes, 0, None)
    require(len(schedule) == expected_files and [row['test'] for row in schedule] == FIXED_TESTS
            and core.digest(schedule) == EXPECTED_SCHEDULE_SHA,
            'Selection is not the complete pinned FileReader/Sync/IDL automated schedule')
    manual_schedule, _ = core.select_schedule(inventory_value, 'manual', prefixes, 0, None)
    require([row['test'] for row in manual_schedule] == MANUAL_NOT_RUN
            and core.digest(manual_schedule) == EXPECTED_MANUAL_SHA,
            'The explicitly unrun manual portion differs from the pinned inventory')
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
            and os.access(native, os.X_OK), 'Immutable FileReader native snapshot changed')
    receipt_path = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-file-reader-snapshot.json'
    require(digest(receipt_path) == RECEIPT_SHA, 'Measured FileReader snapshot receipt changed')
    receipt = json.loads(receipt_path.read_text())
    require(receipt.get('sha256') == NATIVE_SHA and receipt.get('snapshot') == str(native)
            and receipt.get('bytes') == native.stat().st_size, 'FileReader package receipt differs from its snapshot')
    base_receipt_path = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-clone-brand-snapshot.json'
    require(digest(base_receipt_path) == BASE_RECEIPT_SHA, 'Prior private-clone generation receipt changed')
    expected_sources = dict(json.loads(base_receipt_path.read_text())['source_hashes'])
    expected_sources.update(GENERATION_SOURCE_UPDATES)
    require(receipt.get('schema') == 'lumen-file-reader-snapshot-v42'
            and receipt.get('source_hashes') == expected_sources
            and receipt.get('previous_snapshot_receipt_sha256') == BASE_RECEIPT_SHA
            and receipt.get('file_api_full_conformance_claimed') is False
            and receipt.get('encoding_git_object_chain_verified') is False
            and receipt.get('promise_rejection_checks') == 16,
            'New generation does not contain the exact gated FileReader source manifest')
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
            and not (package.get('owned_group_cleanup') or {}).get('error'), 'FileReader package did not complete safely')
    dependencies = {native, receipt_path, package_path, base_receipt_path, baseline, baseline.parent / 'schedule.json'} | frozen_evidence
    for label in ('query_body_gate_report', 'javascript_url_gate_report', 'iframe_bootstrap_gate_report',
                  'iframe_behavior_gate_report', 'url_idl_gate_report', 'form_enctype_gate_report', 'worker_security_gate_report',
                  'clone_brand_gate_report', 'file_reader_gate_report'):
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
                                   if label == 'clone_brand_gate_report' else
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
    output = (args.output or Path('output/wpt/gpt-6-astra-pro-devinmac3d-file-reader36-cft156-v42')).resolve()
    require(output.is_relative_to(REPO / 'output/wpt'), 'Fixed-suite evidence must remain in project output/wpt')
    common = {'suite': 'file-reader36', 'core_sha256': CORE_SHA, 'reporter_sha256': REPORTER_SHA, 'inventory_sha256': INVENTORY_SHA,
              'fixture_manifest_sha256': digest(readiness), 'reference_manifest_sha256': digest(reference_path),
              'reference_tree_sha256': reference['integrity']['tree_sha256'], 'native_sha256': NATIVE_SHA,
              'schedule_sha256': core.digest(schedule), 'selection': selection,
              'manual_not_run': MANUAL_NOT_RUN, 'manual_assertions_counted_as_passed': False,
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
        value['matched_file_reader_fixed_suite'] = {'inputs': common, 'observed_results': summaries,
            'classification': 'functional_only_reference_teardown_failed',
            'reference_performance_release_gate_passed': False, 'current_reference_incidents': incidents,
            'note': 'The explicit reference policy remains in force. All 36 automated files in the declared reading, reader/sync and FileAPI IDL selection are compared without assertion filtering or expectation changes. The six manual variants are explicitly unrun. This focused measurement is not a full WPT or FileAPI conformance claim.'}
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
