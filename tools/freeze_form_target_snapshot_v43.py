#!/usr/bin/env python3
"""Freeze the gated existing-frame form navigation build; no compilation or browser launch."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys

ROOT = Path('/Users/devin/repos/lumen')
PREFIX = 'output/wpt/gpt-6-astra-pro-devinmac3d-'
BASE_RECEIPT = PREFIX + 'file-reader-snapshot.json'
BASE_RECEIPT_SHA = '53141212eb423a3287b656eaa6b33b1e09d709408e9fb5b2d5fb7adf750ae9ad'
BASE_NATIVE_SHA = '894c50e016c9118a4432996a1f754890098308093573bd4ceb4e9581ccfa39ee'
PRODUCTION_RECEIPT = 'output/wpt/form-target-production-after-link-correction-installed.json'
PRODUCTION_RECEIPT_SHA = '1bb32dc8b5ef6a9d27fbfe062ea4f992ebab8410ceb8e83a9ed27138ad440414'
CORRECTION_RECEIPT = 'output/wpt/form-target-link-correction-installed.json'
CORRECTION_RECEIPT_SHA = 'e9806a360bd5b722ff311a5f62419376ebb671ba8a0f077e5af4b1fd3e5ea63f'
GATE_SHA = 'a75dfc0398c9b514dc4c03d0d4f061a44fde4a6bced705f644fe2c783c98d06e'
PRODUCTION_POSTIMAGES = {'src/js/form_navigation.h': 'bbe76d25418a7c4d185fc2b2957bfc58c5e8c2a5e9e71f90b20e6ab9218dc15c', 'src/js/windows.cc': 'eb01f84a6dbfbb1cb0e3a848e5b3a2480d8b9aa140c0f6c6492708dbf3d86aff', 'src/js/js_int.h': '5c0f8b3faf31b0607e4410ad2678c1a41e928f81f774644001377c2f84dc379e', 'src/js/windows.h': '87a033687a32177896cb7b66097f22b24737ba245637dd5a71e4def9de60cc54', 'src/js/frames.cc': '6358507fdd6835a900430d3b8e2d83ef9043d58c5b4ee9d2268040f60e8b5c88', 'src/js/prelude/06b-activate.js': '41241a133cb83d5861660c50ff25621adf48a7af9ab2f4ddd38752602a7398f6', 'src/js/prelude/04-tags.js': 'b41753fa5179ded0455079601c0113a27e9f7949eec1ac68c90b5433eb681622', 'src/net/csp.c': '4393e16a148cfe03ae7203c894d54cd5a0ab8db16efc4139ddbe0eb6353afe7b'}
SOURCE_UPDATES = {'src/js/form_navigation.h': 'bbe76d25418a7c4d185fc2b2957bfc58c5e8c2a5e9e71f90b20e6ab9218dc15c', 'src/js/windows.cc': 'eb01f84a6dbfbb1cb0e3a848e5b3a2480d8b9aa140c0f6c6492708dbf3d86aff', 'src/js/js_int.h': '5c0f8b3faf31b0607e4410ad2678c1a41e928f81f774644001377c2f84dc379e', 'src/js/windows.h': '87a033687a32177896cb7b66097f22b24737ba245637dd5a71e4def9de60cc54', 'src/js/frames.cc': '6358507fdd6835a900430d3b8e2d83ef9043d58c5b4ee9d2268040f60e8b5c88', 'src/js/prelude/06b-activate.js': '41241a133cb83d5861660c50ff25621adf48a7af9ab2f4ddd38752602a7398f6', 'src/js/prelude/04-tags.js': 'b41753fa5179ded0455079601c0113a27e9f7949eec1ac68c90b5433eb681622', 'src/net/csp.c': '4393e16a148cfe03ae7203c894d54cd5a0ab8db16efc4139ddbe0eb6353afe7b', 'Makefile': '11d81f671368ac1f2a00e05bc8bf92d37fc295e7626c59328d3f87388be8a19d', 'tests/test_form_navigation.cc': 'a8d3624be4188dc58b6d523d589d7809fc43a64d4886791fafe492160ac64bdb', 'tests/test_form_dialog.cc': 'f412336c7753f43adc64c65d74635cc7df4ca6a74dba2c40406d0489f145dbd9'}
GATE_STAGES = [('test_form_navigation', 'RESULTS form-navigation PASS (67/67 checks)'), ('test_form_dialog', 'RESULTS form-dialog PASS (60/60 checks)'), ('test_url_dom', 'RESULTS url-dom PASS (287/287 checks)'), ('test_window_lifecycle', 'RESULTS window-lifecycle PASS (445/445 checks)'), ('test_capture_policy', 'RESULTS capture-policy PASS (32/32 checks)'), ('test_history_origin', 'RESULTS history-origin PASS (114/114 checks)'), ('test_file_reader_window', 'RESULTS file-reader PASS (107/107 checks)')]



def require(value, message):
    if not value:
        raise RuntimeError(message)


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def path_in_repo(relative, evidence=False):
    candidate = ROOT / relative
    require(not candidate.is_symlink(), 'Symlink input is not accepted: ' + str(candidate))
    path = candidate.resolve(strict=True)
    require(path.is_file() and path.is_relative_to(ROOT / 'output/wpt' if evidence else ROOT),
            'Input escaped the authorized project: ' + str(candidate))
    return path


def main():
    require(sys.platform == 'darwin' and Path.cwd().resolve() == ROOT, 'Use the authorized Mac repository')
    inputs = {}

    def record(relative, evidence=True):
        path = path_in_repo(relative, evidence)
        before = path.stat()
        value = digest(path)
        identity = (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns)
        after = path.stat()
        require(identity == (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns),
                'Input changed while being recorded: ' + str(path))
        saved = {'sha256': value, 'identity': identity}
        require(str(path) not in inputs or inputs[str(path)] == saved,
                'Previously recorded input changed: ' + str(path))
        inputs[str(path)] = saved
        return path

    def read(relative):
        return json.loads(record(relative).read_text())

    def clean_monitor(relative):
        value = read(relative)
        cleanup = value.get('owned_group_cleanup') or {}
        require(value.get('natural_exit_code') == 0 and value.get('ended_by_monitor') is False
                and not value.get('collector_error') and not value.get('violations')
                and not cleanup.get('error') and not cleanup.get('sent_sigterm') and not cleanup.get('sent_sigkill'),
                'Monitor did not close naturally and safely: ' + str(relative))
        return value

    require(digest(record(BASE_RECEIPT)) == BASE_RECEIPT_SHA, 'The approved FileReader receipt changed')
    base = read(BASE_RECEIPT)
    require(base['sha256'] == BASE_NATIVE_SHA, 'The previous generation identity changed')
    # Keep the completed reader/clone gate artifacts as immutable dependencies;
    # current source files instead follow the explicitly updated source map.
    for name, saved in base.get('input_records', {}).items():
        path = Path(name)
        require(path.is_absolute() and path.is_relative_to(ROOT), 'Prior input escaped the project')
        if path.is_relative_to(ROOT / 'output/wpt'):
            observed = record(name)
            current = inputs[str(observed)]
            require(current['sha256'] == saved['sha256']
                    and current['identity'] == tuple(saved['identity']),
                    'Previously frozen FileReader evidence changed: ' + name)
    source_hashes = dict(base['source_hashes'])
    source_hashes.update(SOURCE_UPDATES)
    for relative, expected in source_hashes.items():
        require(digest(record(relative, False)) == expected, 'Current source differs from gated generation: ' + relative)
    package_path = PREFIX + 'form-target-package-build.json'
    clean_monitor(package_path)
    gate_paths = {name: value for name, value in base.items() if name.endswith('_gate_report')}
    gate_paths['form_target_gate_report'] = PREFIX + 'form-target-green-gates.json'
    gate = read(gate_paths['form_target_gate_report'])
    require(digest(record(gate_paths['form_target_gate_report'])) == GATE_SHA,
            'The actual complete seven-stage form GREEN receipt changed')
    clean_monitor(gate['build_monitor'])
    expected = GATE_STAGES
    observed_result_lines = {}
    require(gate.get('status') == 'passed' and [stage.get('name') for stage in gate.get('stages', [])] ==
            [name for name, _ in expected], 'The form navigation gate has an incomplete or different stage sequence')
    for stage, (_, result) in zip(gate['stages'], expected):
        require(stage.get('passed') is True and stage.get('natural_exit_code') == 0
                and stage.get('expected_result_present') is True,
                'A form navigation gate stage did not pass completely: ' + stage['name'])
        clean_monitor(stage['monitor'])
        log = record(stage['log'])
        require(log.stat().st_size <= 8 * 1024 * 1024, 'Native gate log exceeds the evidence bound')
        text = log.read_text(errors='replace')
        require(result in text.splitlines(), 'Complete native count is missing: ' + stage['name'])
        observed_result_lines[stage['name']] = result
    require(gate['red_receipt'] == PREFIX + 'form-target-red-relinked-run.json',
            'The gate does not retain the complete corrected RED run')
    record(gate['red_receipt'])
    require(gate['source_receipt'] == PRODUCTION_RECEIPT and gate['correction_receipt'] == CORRECTION_RECEIPT,
            'The gate does not reference the actual form production/correction receipts')
    require(digest(record(PRODUCTION_RECEIPT)) == PRODUCTION_RECEIPT_SHA,
            'The actual form production receipt changed')
    installed = read(PRODUCTION_RECEIPT)
    require(installed.get('schema') == 'lumen-form-target-link-install-v1'
            and installed.get('status') == 'installed' and installed.get('phase') == 'production'
            and installed.get('error') is None
            and installed.get('installer_sha256') ==
            'c65d0dfa499521cc96c616aa012fbe06ffbb182ae54b07dca40428e12806fe0f'
            and installed.get('config_sha256') ==
            'aa522b220837fa83650c448958fe7a13d9a7d803b77870a154086a6e1fb9edd4'
            and installed.get('original_manifest_sha256') ==
            '563eea884aae79941190cbee2c7998162b9382ead87d94a1499801ffa0374f57',
            'The reviewed form production continuation did not complete')
    require({item['path']: item['sha256'] for item in installed.get('changed', [])} == PRODUCTION_POSTIMAGES
            and installed.get('expected_production_sha256') ==
            {name: value for name, value in PRODUCTION_POSTIMAGES.items() if name != 'src/js/form_navigation.h'},
            'Form production source receipt differs from the original reviewed postimages')
    require(installed.get('expected_form_navigation_checks') == 67
            and installed.get('existing_form_dialog_checks') == 60
            and installed.get('corrected_test_sha256') == SOURCE_UPDATES['tests/test_form_navigation.cc']
            and installed.get('corrected_makefile_sha256') == SOURCE_UPDATES['Makefile']
            and installed.get('assertion_main_sha256') ==
            '3c96ba805c99e150a7c92b4f772a00858f41ed34a79f269c74d6f576393003e3'
            and installed.get('original_failed_build_preserved') is True,
            'The corrected complete RED fixture contract changed')
    require(digest(record(CORRECTION_RECEIPT)) == CORRECTION_RECEIPT_SHA
            and installed.get('correction_receipt_sha256') == CORRECTION_RECEIPT_SHA,
            'The additive correction receipt changed')
    for relative, expected_sha in installed['input_sha256'].items():
        if relative.startswith('output/wpt/'):
            require(digest(record(relative)) == expected_sha,
                    'Recorded original package, correction or RED evidence changed: ' + relative)
    source = record('build/lumen', False)
    require(os.access(source, os.X_OK), 'Built native executable is not executable')
    native_sha = digest(source)
    require(native_sha != BASE_NATIVE_SHA, 'The package is still the old pre-form-routing executable')
    target = source.with_name('lumen-wpt-' + native_sha[:16])
    receipt_path = ROOT / (PREFIX + 'form-target-snapshot.json')
    require(not target.exists() and not target.is_symlink() and not receipt_path.exists()
            and not receipt_path.is_symlink(), 'Preserve every prior snapshot and receipt')

    def unchanged():
        for name, saved in inputs.items():
            path = Path(name)
            require(not path.is_symlink(), 'Input became a symlink: ' + name)
            stat = path.stat()
            require(saved['identity'] == (stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns)
                    and saved['sha256'] == digest(path), 'Generation input changed: ' + name)

    unchanged()
    with source.open('rb') as src, target.open('xb') as dst:
        shutil.copyfileobj(src, dst, 1 << 20)
        dst.flush()
        os.fsync(dst.fileno())
    os.chmod(target, 0o555)
    require(digest(target) == native_sha, 'Immutable snapshot copy differs from the built executable')
    unchanged()
    receipt = dict(schema='lumen-form-target-snapshot-v43', source=str(source), snapshot=str(target),
                   sha256=native_sha, bytes=target.stat().st_size, package_monitor=package_path,
                   source_hashes=source_hashes, previous_snapshot_receipt=BASE_RECEIPT,
                   previous_snapshot_receipt_sha256=BASE_RECEIPT_SHA,
                   native_gate_result_lines=observed_result_lines,
                   form_submission_full_conformance_claimed=False, nested_worker_support_added=False,
                   cross_document_top_targets_supported=False, new_auxiliary_targets_supported=False,
                   top_self_redirect_form_policy_added=False, multipart_or_charset_conformance_claimed=False,
                   correction_receipt=CORRECTION_RECEIPT, correction_receipt_sha256=CORRECTION_RECEIPT_SHA,
                   production_receipt=PRODUCTION_RECEIPT, production_receipt_sha256=PRODUCTION_RECEIPT_SHA,
                   input_records=inputs, **gate_paths)
    with receipt_path.open('x') as output:
        json.dump(receipt, output, indent=2, sort_keys=True)
        output.write('\n')
    print(json.dumps({'receipt': str(receipt_path), 'receipt_sha256': digest(receipt_path),
                      'snapshot': str(target), 'sha256': native_sha, 'bytes': target.stat().st_size}, sort_keys=True))


if __name__ == '__main__':
    main()
