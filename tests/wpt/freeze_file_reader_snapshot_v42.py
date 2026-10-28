#!/usr/bin/env python3
"""Freeze the gated shared FileReader build; no compilation or browser launch."""
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import sys

ROOT = Path('/Users/devin/repos/lumen')
PREFIX = 'output/wpt/gpt-6-astra-pro-devinmac3d-'
BASE_RECEIPT = PREFIX + 'clone-brand-snapshot.json'
BASE_RECEIPT_SHA = 'f398f7f17afc266e22bcd1dcae7242e0241f14cfab897d43a5691a611e973ca7'
BASE_NATIVE_SHA = '975f7677ff2e5afc0d748e37b404db3768da78093a24a5265d6356b054a2606b'
PRODUCTION_RECEIPT = 'output/wpt/file-reader-production-installed.json'
PRODUCTION_RECEIPT_SHA = 'fef75c4f37fafde221a638f98baf27a06bc284a32ff914422a629bcdddf4a528'
PRODUCTION_POSTIMAGES = {'Makefile': '1aeeb251128527b5d9d6801bebcbfdd9ab23982c2df67e1b632225590ac64194', 'src/base/LICENSE.whatwg-encoding': '85dc6f5ccb57a6fe8c33d158f9fc8fc7ee5655a5d3db2cdd131c6a3d0f48a864', 'src/base/encoding.c': '4d2177b07c168b6ea066e99fc39745cc2cbcf58002acd07040d0900fc78a5864', 'src/base/encoding.h': '04b4c4ee093393f3233c7618a35b715fd7198c826f0a678b4f1d685dc70fc56e', 'src/base/encoding_decode_data.inc': '759a6b5cb49e450b6aab966e176a7f410c7cdcb0e140abee88ea009a2d8d9a65', 'src/base/encoding_decode_data.provenance.json': '0f14b700f94de0a760bb91c61baae2f5788e1b6fe58a1dbf4bb5832f007c854c', 'src/js/blob_state.h': '1f7bd86022791b425c987d722ada95f83460ca944d7204b6e6f14d2f665f4197', 'src/js/clone_brand.h': '5e14f2dd9c2e6bc2c8b6a5177f55a6df4851dc8d69da56f9262a3bdb86bbf5d6', 'src/js/file_reader.h': '9de18e7581fe162671ac7c9f69c3476aee88d796870d053a3c347376e61e4044', 'src/js/idl_state.h': 'c623c345f5e7e26916b19a0fe264ef057fc0314f4b84c56dbca93eccf0581619', 'src/js/jsnative.cc': 'ae24bd6967bd33fa6232c0a1febb988ce05293012a5bc544940a403be19f6742', 'src/js/prelude/01-events.js': 'bc65fcadc60bc8de66d99accd0ebdea841cc4ffbceea4e548581ce2994f76e0e', 'src/js/prelude/06w-filereader.js': '44fb45ca87dfdedd6458912d6bbf4b8258bc16ab0dcb8db59c00be2996cbe9c9', 'src/js/prelude/07-web.js': 'ad2f51aac039b96c3e6b4809e1e632ea45388b44557f2f50d26bbdda1ab3d489', 'src/js/prelude/08-window.js': 'e88f616ec67c1250be57a2c28dcf9779134bfb9a04a200b97b61bb136d60120a', 'src/js/worker.cc': '2ca1a0415886bf6a16dbb3113758efff987bd631004f780684f8947c97a673f8', 'tests/test_encoding.c': 'def09712b1477295a200312953399fc019af7f7fe74e559022e12f4b8ea5a4e1', 'tools/generate_encoding_decode_data.py': '805ecf476afdc402c009fcc6881c40e7a41965796e7507d21328c2c2f1c7eedf', 'tools/test_generate_encoding_decode_data.py': '44955896caf2b79f9f60c307096b1b83919f71a179e5a83e30c2299c856fd88b'}
SOURCE_UPDATES = {'Makefile': '1aeeb251128527b5d9d6801bebcbfdd9ab23982c2df67e1b632225590ac64194', 'src/base/LICENSE.whatwg-encoding': '85dc6f5ccb57a6fe8c33d158f9fc8fc7ee5655a5d3db2cdd131c6a3d0f48a864', 'src/base/encoding.c': '4d2177b07c168b6ea066e99fc39745cc2cbcf58002acd07040d0900fc78a5864', 'src/base/encoding.h': '04b4c4ee093393f3233c7618a35b715fd7198c826f0a678b4f1d685dc70fc56e', 'src/base/encoding_decode_data.inc': '759a6b5cb49e450b6aab966e176a7f410c7cdcb0e140abee88ea009a2d8d9a65', 'src/base/encoding_decode_data.provenance.json': '0f14b700f94de0a760bb91c61baae2f5788e1b6fe58a1dbf4bb5832f007c854c', 'src/js/blob_state.h': '1f7bd86022791b425c987d722ada95f83460ca944d7204b6e6f14d2f665f4197', 'src/js/clone_brand.h': '5e14f2dd9c2e6bc2c8b6a5177f55a6df4851dc8d69da56f9262a3bdb86bbf5d6', 'src/js/file_reader.h': '9de18e7581fe162671ac7c9f69c3476aee88d796870d053a3c347376e61e4044', 'src/js/idl_state.h': 'c623c345f5e7e26916b19a0fe264ef057fc0314f4b84c56dbca93eccf0581619', 'src/js/jsnative.cc': 'ae24bd6967bd33fa6232c0a1febb988ce05293012a5bc544940a403be19f6742', 'src/js/prelude/01-events.js': 'bc65fcadc60bc8de66d99accd0ebdea841cc4ffbceea4e548581ce2994f76e0e', 'src/js/prelude/06w-filereader.js': '44fb45ca87dfdedd6458912d6bbf4b8258bc16ab0dcb8db59c00be2996cbe9c9', 'src/js/prelude/07-web.js': 'ad2f51aac039b96c3e6b4809e1e632ea45388b44557f2f50d26bbdda1ab3d489', 'src/js/prelude/08-window.js': 'e88f616ec67c1250be57a2c28dcf9779134bfb9a04a200b97b61bb136d60120a', 'src/js/worker.cc': '2ca1a0415886bf6a16dbb3113758efff987bd631004f780684f8947c97a673f8', 'tests/test_encoding.c': 'def09712b1477295a200312953399fc019af7f7fe74e559022e12f4b8ea5a4e1', 'tools/generate_encoding_decode_data.py': '805ecf476afdc402c009fcc6881c40e7a41965796e7507d21328c2c2f1c7eedf', 'tools/test_generate_encoding_decode_data.py': '44955896caf2b79f9f60c307096b1b83919f71a179e5a83e30c2299c856fd88b', 'tests/test_file_reader.cc': 'd5392ebe8485311d54b8c3de5afa6e584ede5aaa219585d86d39e5d598b3b355', 'tests/js/file-reader-regression.js': '870bce3480a234b96bf52ecf490fcacc118a43f38366b4e5b3529ab7c5a5ba8f', 'tests/js/file-reader-realm-regression.js': '46f34b63128311dc078a8e72db1fac3428b3ea5261780fb23b907275ef1fb589', 'tests/js/worker-file-reader-regression.js': '8583721257fa505451deea06197ec3239b4602fa9453b5c525fbe7d5b62cdfaf', 'tests/test_promise_rejections.cc': 'd14c48cff9de131a28d73a037b951687a5b3110dfa48360d0e99d246174887b0'}
GATE_STAGES = [('test_file_reader_window', 'RESULTS file-reader PASS (107/107 checks)'), ('test_file_reader_worker', 'RESULTS file-reader PASS (120/120 checks)'), ('test_encoding', 'encoding: 89/89 checks passed'), ('test_clone_brand_window', 'RESULTS clone-brand PASS (77/77 checks)'), ('test_clone_brand_worker', 'RESULTS clone-brand PASS (64/64 checks)'), ('test_worker_lifecycle', 'RESULTS worker-lifecycle PASS (262/262 checks)'), ('test_worker_exit', 'RESULTS worker-exit returning through the registered backstop'), ('test_worker_security', 'RESULTS worker-security PASS (51/51 checks)'), ('test_url_idl_window', 'RESULTS url-idl PASS (116/116 checks)'), ('test_url_idl_worker', 'RESULTS url-idl PASS (90/90 checks)'), ('test_url_dom', 'RESULTS url-dom PASS (287/287 checks)'), ('test_url_params', 'RESULTS url-params-body PASS (62/62 checks)'), ('test_history_origin', 'RESULTS history-origin PASS (114/114 checks)'), ('test_capture_policy', 'RESULTS capture-policy PASS (32/32 checks)'), ('test_beacon', 'beacon: 74/74 checks passed'), ('test_form_dialog', 'RESULTS form-dialog PASS (60/60 checks)'), ('test_promise_rejections', None), ('test_window_lifecycle', 'RESULTS window-lifecycle PASS (445/445 checks)')]
PROMISE_RESULT = re.compile(r'^RESULTS promise-rejections PASS \(([1-9][0-9]*)/([1-9][0-9]*) checks\)$', re.M)



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

    require(digest(record(BASE_RECEIPT)) == BASE_RECEIPT_SHA, 'The approved clone receipt changed')
    base = read(BASE_RECEIPT)
    require(base['sha256'] == BASE_NATIVE_SHA, 'The previous generation identity changed')
    source_hashes = dict(base['source_hashes'])
    source_hashes.update(SOURCE_UPDATES)
    for relative, expected in source_hashes.items():
        require(digest(record(relative, False)) == expected, 'Current source differs from gated generation: ' + relative)
    package_path = PREFIX + 'file-reader-package-build.json'
    clean_monitor(package_path)
    gate_paths = {name: value for name, value in base.items() if name.endswith('_gate_report')}
    gate_paths['file_reader_gate_report'] = PREFIX + 'file-reader-green-gates.json'
    gate = read(gate_paths['file_reader_gate_report'])
    require(digest(record(gate_paths['file_reader_gate_report'])) ==
            '3a1a5cbad44b978ad7f3dc70c123278c269dede6f5c3cb4f37a425cfcae18528',
            'The actual complete eighteen-stage FileReader GREEN receipt changed')
    clean_monitor(gate['build_monitor'])
    expected = GATE_STAGES
    observed_result_lines = {}
    promise_checks = None
    require(gate.get('status') == 'passed' and [stage.get('name') for stage in gate.get('stages', [])] ==
            [name for name, _ in expected], 'The FileReader gate has an incomplete or different stage sequence')
    for stage, (_, result) in zip(gate['stages'], expected):
        require(stage.get('passed') is True and stage.get('natural_exit_code') == 0
                and stage.get('expected_result_present') is True,
                'A FileReader gate stage did not pass completely: ' + stage['name'])
        clean_monitor(stage['monitor'])
        log = record(stage['log'])
        require(log.stat().st_size <= 8 * 1024 * 1024, 'Native gate log exceeds the evidence bound')
        text = log.read_text(errors='replace')
        if result is None:
            matches = PROMISE_RESULT.findall(text)
            require(len(matches) == 1 and matches[0][0] == matches[0][1],
                    'Promise-rejection gate lacks one complete positive equal count')
            promise_checks = int(matches[0][0])
            require(promise_checks == 16, 'The observed promise-rejections gate count changed')
            result = 'RESULTS promise-rejections PASS (' + str(promise_checks) + '/' + str(promise_checks) + ' checks)'
        require(result in text.splitlines(), 'Complete native count is missing: ' + stage['name'])
        observed_result_lines[stage['name']] = result
    record(gate['red_receipt'])
    require(gate['source_receipt'] == PRODUCTION_RECEIPT,
            'The gate does not reference the actual FileReader production receipt')
    require(digest(record(PRODUCTION_RECEIPT)) == PRODUCTION_RECEIPT_SHA,
            'The actual FileReader production receipt changed')
    installed = read(PRODUCTION_RECEIPT)
    require(installed.get('status') == 'installed' and installed.get('installed') is True
            and installed.get('phase') == 'production' and installed.get('error') is None
            and installed.get('installer_sha256') ==
            '63fb65c502567bf6d57f1e581e2ead16a03131b4f18284e5af9def480eb0b96b'
            and installed.get('manifest_sha256') ==
            'a0f496e85d664d8fc5a2b9aefbc6aa6bc934acddd00c58fb932daaaa53d86159',
            'The reviewed FileReader production installer did not complete')
    require(installed.get('planned_sha256') == PRODUCTION_POSTIMAGES
            and {name: item['after_sha256'] for name, item in installed.get('files', {}).items()} == PRODUCTION_POSTIMAGES,
            'Installed FileReader source receipt differs from the reviewed postimages')
    require(installed.get('expected_window_checks') == 107 and installed.get('expected_worker_checks') == 120
            and installed.get('expected_decoder_checks') == 89 and installed.get('git_object_chain_verified') is False,
            'The FileReader test or source-provenance contract changed')
    for relative, expected_sha in installed['input_sha256'].items():
        if relative.startswith('output/wpt/'):
            require(digest(record(relative)) == expected_sha,
                    'The recorded original decoder or clone evidence changed: ' + relative)
    source = record('build/lumen', False)
    require(os.access(source, os.X_OK), 'Built native executable is not executable')
    native_sha = digest(source)
    require(native_sha != BASE_NATIVE_SHA, 'The package is still the old pre-FileReader executable')
    target = source.with_name('lumen-wpt-' + native_sha[:16])
    receipt_path = ROOT / (PREFIX + 'file-reader-snapshot.json')
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
    receipt = dict(schema='lumen-file-reader-snapshot-v42', source=str(source), snapshot=str(target),
                   sha256=native_sha, bytes=target.stat().st_size, package_monitor=package_path,
                   source_hashes=source_hashes, previous_snapshot_receipt=BASE_RECEIPT,
                   previous_snapshot_receipt_sha256=BASE_RECEIPT_SHA, file_api_full_conformance_claimed=False,
                   native_gate_result_lines=observed_result_lines, promise_rejection_checks=promise_checks,
                   encoding_git_object_chain_verified=False,
                   input_records=inputs, **gate_paths)
    with receipt_path.open('x') as output:
        json.dump(receipt, output, indent=2, sort_keys=True)
        output.write('\n')
    print(json.dumps({'receipt': str(receipt_path), 'receipt_sha256': digest(receipt_path),
                      'snapshot': str(target), 'sha256': native_sha, 'bytes': target.stat().st_size}, sort_keys=True))


if __name__ == '__main__':
    main()
