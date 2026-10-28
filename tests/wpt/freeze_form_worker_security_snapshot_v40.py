#!/usr/bin/env python3
"""Freeze the gated form and worker-security build; no compilation or launch."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys

ROOT = Path('/Users/devin/repos/lumen')
PREFIX = 'output/wpt/gpt-6-astra-pro-devinmac3d-'
BASE_RECEIPT = PREFIX + 'blob-idl-snapshot.json'
BASE_RECEIPT_SHA = 'b3c15f76ca9881b0b3e8b86de0c9cbbc601e41a0328a42aaaa102c4c701e377e'
BASE_NATIVE_SHA = '03b431e44afef5847035447d5570fbb5f2ccba430e8a3d63099ee0ab5be95388'
SOURCE_UPDATES = {
    'Makefile': '4373777ca7df8beefc412b1e337acb1c30173da45a795431bb52e4d8b66cd0d4',
    'src/js/prelude/03-element.js': '7642cb0921317c8c20cb3e1639fa018832ef9fd38b1b9133b8e6fada79c82019',
    'src/js/prelude/04-tags.js': '602808f1955f948428fe91506e4613604fcfc3373d320f8405e4eb4469e3c5af',
    'src/js/prelude/06b-activate.js': 'f9cdb122860ce7a8bb6cc4c7e7e68dffdfe0bc3533a13967a60156e8d3a9f71d',
    'src/js/worker.cc': '5e2f2b23863d0f576b48f6e73dc3380566eaf42dab7128eb01923f35c358b2b1',
    'tests/test_form_dialog.cc': '8656e9c92155059d2ce556104e8f4860d6c34688c2b7bf7e566656b8921a18d4',
    'tests/test_worker_security.cc': 'f822adc9045443280d127c6d9b615659a11cd1bc28d65adee6253afe49ebcbff',
}
GATE_STAGES = {
    'form_enctype_gate_report': [('test_form_dialog', 'RESULTS form-dialog PASS (60/60 checks)'),
                                 ('test_url_dom', 'RESULTS url-dom PASS (287/287 checks)')],
    'worker_security_gate_report': [('test_worker_security', 'RESULTS worker-security PASS (51/51 checks)'),
                                    ('test_worker_lifecycle', 'RESULTS worker-lifecycle PASS (262/262 checks)'),
                                    ('test_url_idl_window', 'RESULTS url-idl PASS (116/116 checks)'),
                                    ('test_url_idl_worker', 'RESULTS url-idl PASS (90/90 checks)'),
                                    ('test_capture_policy', 'RESULTS capture-policy PASS (32/32 checks)')],
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

    require(digest(record(BASE_RECEIPT)) == BASE_RECEIPT_SHA, 'The approved Blob/IDL receipt changed')
    base = read(BASE_RECEIPT)
    require(base['sha256'] == BASE_NATIVE_SHA, 'The previous generation identity changed')
    source_hashes = dict(base['source_hashes'])
    source_hashes.update(SOURCE_UPDATES)
    for relative, expected in source_hashes.items():
        require(digest(record(relative, False)) == expected, 'Current source differs from gated generation: ' + relative)
    package_path = PREFIX + 'form-worker-security-package-build.json'
    clean_monitor(package_path)
    gate_paths = {name: value for name, value in base.items() if name.endswith('_gate_report')}
    gate_paths.update(form_enctype_gate_report=PREFIX + 'form-enctype-green-gates.json',
                      worker_security_gate_report=PREFIX + 'worker-security-green-gates.json')
    require(digest(record(gate_paths['form_enctype_gate_report'])) ==
            '72e3b43a4fe66ad30b2c2ddfbda0187ec1816adc1422c4f280d55b7422c20e4d',
            'The actual form GREEN gate changed')
    for label, expected in GATE_STAGES.items():
        gate = read(gate_paths[label])
        require(gate.get('status') == 'passed' and [stage.get('name') for stage in gate.get('stages', [])] ==
                [name for name, _ in expected], 'The native gate has an incomplete or different stage sequence: ' + label)
        for stage, (_, result) in zip(gate['stages'], expected):
            require(stage.get('passed') is True and stage.get('natural_exit_code') == 0,
                    'A native gate stage failed: ' + stage['name'])
            clean_monitor(stage['monitor'])
            log = record(stage['log'])
            require(log.stat().st_size <= 8 * 1024 * 1024, 'Native gate log exceeds the bounded evidence size')
            if result:
                require(result in log.read_text(errors='replace'), 'Expected complete result is missing: ' + stage['name'])
        record(gate['red_receipt'])  # Preserve actual RED; it is not relabeled as GREEN.
        installed = read(gate['source_receipt'])
        installer = ('f35e927ee289fb6c420fcd6ad8163f0e2cf06bf6a823275066111325120a3f45'
                     if label == 'form_enctype_gate_report' else
                     '49f66e3e2c3c9d9b75c8cc36ffc8e8cb375efdb7c8df91d992605ce452849e9f')
        require(installed.get('status') == 'installed' and installed.get('phase') == 'production'
                and installed.get('error') is None and installed.get('installer_sha256') == installer,
                'The reviewed production installer did not complete: ' + label)
        expected_changed = (['src/js/prelude/04-tags.js', 'src/js/prelude/06b-activate.js']
                            if label == 'form_enctype_gate_report' else ['src/js/worker.cc'])
        require({row['path']: row['sha256'] for row in installed.get('changed', [])} ==
                {name: SOURCE_UPDATES[name] for name in expected_changed}, 'Installed source receipt differs: ' + label)
    source = record('build/lumen', False)
    require(os.access(source, os.X_OK), 'Built native executable is not executable')
    native_sha = digest(source)
    require(native_sha != BASE_NATIVE_SHA, 'The package is still the old pre-form/security executable')
    target = source.with_name('lumen-wpt-' + native_sha[:16])
    receipt_path = ROOT / (PREFIX + 'form-worker-security-snapshot.json')
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
    receipt = dict(schema='lumen-form-worker-security-snapshot-v40', source=str(source), snapshot=str(target),
                   sha256=native_sha, bytes=target.stat().st_size, package_monitor=package_path,
                   source_hashes=source_hashes, previous_snapshot_receipt=BASE_RECEIPT,
                   previous_snapshot_receipt_sha256=BASE_RECEIPT_SHA, nested_worker_support_added=False,
                   input_records=inputs, **gate_paths)
    with receipt_path.open('x') as output:
        json.dump(receipt, output, indent=2, sort_keys=True)
        output.write('\n')
    print(json.dumps({'receipt': str(receipt_path), 'receipt_sha256': digest(receipt_path),
                      'snapshot': str(target), 'sha256': native_sha, 'bytes': target.stat().st_size}, sort_keys=True))


if __name__ == '__main__':
    main()
