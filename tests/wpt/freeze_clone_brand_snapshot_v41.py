#!/usr/bin/env python3
"""Freeze the gated private-clone build; no compilation or browser launch."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys

ROOT = Path('/Users/devin/repos/lumen')
PREFIX = 'output/wpt/gpt-6-astra-pro-devinmac3d-'
BASE_RECEIPT = PREFIX + 'form-worker-security-snapshot.json'
BASE_RECEIPT_SHA = '4d1ffd4adadb1283c15b6730017fbcc0b5ffb904deb4c368b2f54e65957b183f'
BASE_NATIVE_SHA = '21a3dfddf8da1f6948be6b3f51184ec8a0800b1c9eca49b3cacd2ba610e680af'
SOURCE_UPDATES = {'Makefile': '3526b90ba87e129175921824737e347d188203b5952e22f54d03579c43aa16fe', 'src/js/jsnative.cc': '6e942e787e3f21a0231bbcdaab70eee199bc5148059bbd0f7b685b4fbaa1089b', 'src/js/prelude/08-window.js': 'd58090d056f4504c51af9ea660dd3b254ac0ee8e600ffce00befc006bf2bf923', 'src/js/worker.cc': '10ed67b8d40ba84a015c9de850385102aadf4f1c95852aeb7a2687448c2f56f4', 'src/js/frames.cc': 'f97d5a56c5cdbcbfca7e4c4e2cf42f72a9cbdd7a4747d42cfe47a66d873b1dee', 'src/js/windows.cc': '44149413b08a803f6e0edf025a99d5635e69bdd32868658d1b9471c3edd71c45', 'src/js/clone_brand.h': 'e745d8e39572738bba70f4ad5b3c2671c72a0fe7e35114cf3b66807f90dbecbc', 'tests/test_clone_brand.cc': '733afaecc0b0ed299a857d7df7376d19092cf9aa18e1ccfe9aeb1e929a21e672', 'tests/js/clone-brand-regression.js': '7bd547613a4807131c3f2161e0cc33bb9cef74d7140637469a1894880be82630', 'tests/js/clone-brand-window-regression.js': '5d23a06f1ae0d759dad1dd50a09476c288d203650bed082a95bfbe950f5ffdc6', 'tests/js/worker-clone-brand-regression.js': 'a2070a6ce1520b8a9b8b2dce8a7483b7dbb013a7618d64f364387ec37f958416'}
GATE_STAGES = {
    "clone_brand_gate_report": [
        ('test_clone_brand_window', 'RESULTS clone-brand PASS (77/77 checks)'),
        ('test_clone_brand_worker', 'RESULTS clone-brand PASS (64/64 checks)'),
        ('test_window_lifecycle', 'RESULTS window-lifecycle PASS (445/445 checks)'),
        ('test_worker_lifecycle', 'RESULTS worker-lifecycle PASS (262/262 checks)'),
        ('test_worker_exit', 'RESULTS worker-exit returning through the registered backstop'),
        ('test_worker_security', 'RESULTS worker-security PASS (51/51 checks)'),
        ('test_url_idl_window', 'RESULTS url-idl PASS (116/116 checks)'),
        ('test_url_idl_worker', 'RESULTS url-idl PASS (90/90 checks)'),
        ('test_url_dom', 'RESULTS url-dom PASS (287/287 checks)'),
        ('test_history_origin', 'RESULTS history-origin PASS (114/114 checks)'),
    ],
}



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

    require(digest(record(BASE_RECEIPT)) == BASE_RECEIPT_SHA, 'The approved form/security receipt changed')
    base = read(BASE_RECEIPT)
    require(base['sha256'] == BASE_NATIVE_SHA, 'The previous generation identity changed')
    source_hashes = dict(base['source_hashes'])
    source_hashes.update(SOURCE_UPDATES)
    for relative, expected in source_hashes.items():
        require(digest(record(relative, False)) == expected, 'Current source differs from gated generation: ' + relative)
    package_path = PREFIX + 'clone-brand-package-build.json'
    clean_monitor(package_path)
    gate_paths = {name: value for name, value in base.items() if name.endswith('_gate_report')}
    gate_paths['clone_brand_gate_report'] = PREFIX + 'clone-brand-green-gates.json'
    gate = read(gate_paths['clone_brand_gate_report'])
    clean_monitor(gate['build_monitor'])
    expected = GATE_STAGES['clone_brand_gate_report']
    require(gate.get('status') == 'passed' and [stage.get('name') for stage in gate.get('stages', [])] ==
            [name for name, _ in expected], 'The clone gate has an incomplete or different stage sequence')
    for stage, (_, result) in zip(gate['stages'], expected):
        require(stage.get('passed') is True and stage.get('natural_exit_code') == 0
                and stage.get('expected_result_present') is True,
                'A clone gate stage did not pass completely: ' + stage['name'])
        clean_monitor(stage['monitor'])
        log = record(stage['log'])
        require(log.stat().st_size <= 8 * 1024 * 1024, 'Native gate log exceeds the evidence bound')
        if result:
            require(result in log.read_text(errors='replace'), 'Complete native count is missing: ' + stage['name'])
    record(gate['red_receipt'])
    require(gate['source_receipt'] == 'output/wpt/clone-brand-production-installed.json'
            and gate['correction_receipt'] == 'output/wpt/clone-brand-rtti-correction-installed.json',
            'The gate does not reference the actual production/correction receipts')
    require(digest(record(gate['source_receipt'])) ==
            'c6deac00d3602881c94e8cb4153de051ecc243624babc0efb510146c19c9a8ad',
            'The actual clone production receipt changed')
    installed = read(gate['source_receipt'])
    require(installed.get('status') == 'installed' and installed.get('installed') is True
            and installed.get('phase') == 'production' and installed.get('error') is None
            and installed.get('installer_sha256') ==
            'db2137eb350fead3bf9f8807b6e19deb8ffb111a5ffebf147d8f9cb30c48a0f1',
            'The reviewed clone production installer did not complete')
    expected_postimages = {name: value for name, value in SOURCE_UPDATES.items() if not name.startswith('tests/')}
    expected_postimages['Makefile'] = '6b58b374f2220d53adf2ff18f5e2575802fe80f1dd2be8def04cf50b7ae6acc9'
    require(installed.get('planned_sha256') == expected_postimages
            and {name: item['after_sha256'] for name, item in installed.get('files', {}).items()} == expected_postimages,
            'Installed clone source receipt differs from the reviewed postimages')
    require(digest(record(gate['correction_receipt'])) ==
            '832fc373a6d97ad5227e3775c2e41f8288071b5a01c9306778dadcf7c099ddef',
            'The actual RTTI correction receipt changed')
    correction = read(gate['correction_receipt'])
    require(correction.get('status') == 'installed'
            and correction.get('installer_sha256') ==
            'dc9d2c0539d7e828b7b8a6f4d68def2d79ac3cf3046a20df7f131dc0d9a017fc'
            and correction.get('planned_makefile_sha256') == SOURCE_UPDATES['Makefile']
            and correction.get('files') == [{'path': 'Makefile',
                'before_sha256': expected_postimages['Makefile'],
                'after_sha256': SOURCE_UPDATES['Makefile'], 'bytes': 28318}],
            'The recorded correction is not the observed two-object build correction')
    source = record('build/lumen', False)
    require(os.access(source, os.X_OK), 'Built native executable is not executable')
    native_sha = digest(source)
    require(native_sha != BASE_NATIVE_SHA, 'The package is still the old pre-clone executable')
    target = source.with_name('lumen-wpt-' + native_sha[:16])
    receipt_path = ROOT / (PREFIX + 'clone-brand-snapshot.json')
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
    receipt = dict(schema='lumen-clone-brand-snapshot-v41', source=str(source), snapshot=str(target),
                   sha256=native_sha, bytes=target.stat().st_size, package_monitor=package_path,
                   source_hashes=source_hashes, previous_snapshot_receipt=BASE_RECEIPT,
                   previous_snapshot_receipt_sha256=BASE_RECEIPT_SHA, structured_clone_full_conformance_claimed=False,
                   input_records=inputs, **gate_paths)
    with receipt_path.open('x') as output:
        json.dump(receipt, output, indent=2, sort_keys=True)
        output.write('\n')
    print(json.dumps({'receipt': str(receipt_path), 'receipt_sha256': digest(receipt_path),
                      'snapshot': str(target), 'sha256': native_sha, 'bytes': target.stat().st_size}, sort_keys=True))


if __name__ == '__main__':
    main()
