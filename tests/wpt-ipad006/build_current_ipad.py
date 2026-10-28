#!/usr/bin/env python3
"""Build the recorded current Lumen sources into a fresh simulator-only output.

Root runs this helper only on the authorized Mac through devinmac3d, under an
outer process monitor. The old build-sim/Lumen.app and closed iPad005 evidence
are read-only inputs. This helper never installs, launches, terminates, boots,
or selects a simulator. Build resource observations are not browser benchmarks.
"""
import argparse
import fcntl
import hashlib
import importlib
import json
import os
from pathlib import Path
import platform
import plistlib
import shutil
import stat
import subprocess
import sys
import time


ROOT = Path('/Users/devin/repos/lumen')
GENERATION = 'output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006'
BUILD = 'build-sim-ipad006'
DEVICE = 'A77F68E4-972B-472B-80B8-F65E71D361E5'
BUNDLE = 'com.ecooxai.lumen'
OLD_BINARY_SHA = 'b6bfb5ffa7ec86c144e4c49842ac911b60f40631d8a06fa87147e20333c0cd97'
OLD_BUNDLE_SHA = 'dbd002d685f1b7653baada76190419b3885a87685cfbf243be7e5ab68b6c3619'
NATIVE_SNAPSHOT = 'build/lumen-wpt-14814f31ee1f7771'
NATIVE_SHA = '14814f31ee1f7771ea3e159adb21f02c13058cb601ab1c5f7af27de9c4a6d538'
MONITOR_SHA = '36a0194c2d71c484841b31887773bf02f6f9e95d519132aeb5ee211ca3dde98a'
PREVIOUS = 'output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-005/runtime-smoke-001'
READERS = {
    'full_runner.py': 'a7bc96c3697fb099ad63c145011bc434a66d865a713a62e5198731bed9600ac7',
    'ipad_runner.py': '1876472b3552a425f9539ef9bb638cf86a6e006c3bc5f6ca603eb8305a39c4ba',
    'ipad_adapter.py': '885d9aed25b8525f9370246f3bc87b05d72713052749cb9a06a0198a25434fc4',
    'ipad_generation.py': '38d6f962059e932e12a158ae94df37ca289f434380e6478b1ede856b31fafb73',
    'ipad_lifecycle.py': 'fdf39fca49c5e72bbb05d132a07c6abbc79ae37d15b49e4c280e863d85a6bbbf',
}
SOURCE_ANCHORS = {
    'Makefile': '0e93d6518270a82cb4f4b99c2796b338c6aa22e21fd517fe334174e49ab447db',
    'src/app/main.c': 'd047f9098c92ab351f42fbcc86b91feddfdd1e44501911d3e38241cf284e1eab',
    'src/js/jsnative.cc': 'f465ab0fc4292e4a1726556160d55c0a15d409ab0e63957740b53c82216c4244',
    'src/js/prelude/06b-activate.js': '7a56c5318a90c3d76433081c8479ba687f6dffdd0ab0efdecc39040c06397b44',
    'src/js/prelude/06z-urlsearchparams.js': '1e279e4902982c3e8b5b6167a156f07ec200097ffb42dd646bee37e1bce02210',
    'src/js/prelude/07-web.js': 'ca995c5576d85800ad427e65b4f73e10881d4c5216e8d0b2d6f5dd0a1a71d4bc',
    'src/js/prelude/07b-net.js': 'c661304219604482fdc930fa4082c7fcb097c2def4af4789dea96ccc729361d5',
    'src/js/prelude/08-window.js': '190c5fd0dec2313e04ba3bc3fc7389a285707c575fa2a6595b7b4d626ec36a95',
    'src/js/windows.cc': '8391b1bce19985d0e2df9cd4a48fb821967bbef2fa71f3f76b6351cdb1f42a1a',
    'src/js/worker.cc': '1786763941fb8d85fdf170cea46ebd1adaa6262d978ae5f5f58b1c807040e768',
    'src/net/csp.c': '06afb3db1aa08538bb21ff797513b093d78fc49b8c4139e0348a5691466fffc4',
    'src/net/net.h': 'e74bbe14823bb09676d95e1f31e848cc285aebab87248a7b862b7bb06cd4b401',
}
OVERRIDE_ENV = ('BUILD', 'PLAT', 'MODE', 'JIT', 'IOSDEPS', 'V8DIR', 'V8OUT', 'SDK', 'SDKNAME',
                'RTC', 'RTC_PREFIX', 'WT', 'WT_PREFIX', 'CACERT', 'MAKEFLAGS', 'MFLAGS',
                'MAKEFILES', 'MAKEOVERRIDES', 'GNUMAKEFLAGS',
                'CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH',
                'DYLD_INSERT_LIBRARIES', 'DYLD_LIBRARY_PATH')
MAX_SOURCE_FILES = 4096
MAX_SOURCE_BYTES = 128 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def identity(path):
    info = Path(path).stat()
    return [info.st_dev, info.st_ino, info.st_mode, info.st_size,
            info.st_mtime_ns, info.st_ctime_ns]


def fingerprint(path, root):
    path = Path(path)
    require(not path.is_symlink() and path.resolve(strict=True).is_relative_to(root)
            and path.is_file(), 'Input is not an ordinary project-local file: ' + str(path))
    before = identity(path)
    with path.open('rb') as source:
        digest = hashlib.file_digest(source, 'sha256').hexdigest()
    require(identity(path) == before, 'Input changed while hashing: ' + str(path))
    return {'sha256': digest, 'bytes': before[3], 'identity': before}


def source_manifest(root):
    """Hash source/package inputs, not old or new build outputs."""
    paths = []
    for directory, children, files in os.walk(root / 'src', followlinks=False):
        require(all(not (Path(directory) / name).is_symlink() for name in children),
                'A source directory is a symlink')
        for name in files:
            path = Path(directory) / name
            if path.suffix in ('.c', '.cc', '.cpp', '.h', '.hpp', '.m', '.mm', '.js', '.inc'):
                paths.append(path)
    paths += [root / name for name in ('Makefile', 'ios/Info.plist', 'ios/prepare-v8-link.py',
                                      'tools/prepare-webtransport-link.py', 'assets/icon/lumen-256.png')]
    require(1 <= len(paths) <= MAX_SOURCE_FILES, 'Source inventory exceeds its file bound')
    require(sum(path.stat().st_size for path in paths) <= MAX_SOURCE_BYTES,
            'Source inventory exceeds its byte bound')
    return {path.relative_to(root).as_posix(): fingerprint(path, root) for path in sorted(paths)}


def check_source_manifest(root, expected):
    require(source_manifest(root) == expected, 'Source/package inputs changed during the build freeze')


def clean_monitor(measurement, returncode):
    cleanup = measurement.get('owned_group_cleanup') or {}
    require(returncode == 0 and measurement.get('natural_exit_code') == 0
            and measurement.get('ended_by_monitor') is False
            and not measurement.get('collector_error') and not measurement.get('violations')
            and not cleanup.get('error') and cleanup.get('sent_sigterm') is False
            and cleanup.get('sent_sigkill') is False,
            'Build did not complete naturally with clean monitoring; retain all evidence')


def bundle_manifest(entry, core, app, root):
    app = Path(app)
    require(not app.is_symlink() and app.resolve(strict=True).is_relative_to(root),
            'Source app path escaped the repository')
    guard = entry.bundle_tree(app)
    info = plistlib.loads((app / 'Info.plist').read_bytes())
    require(info.get('CFBundleIdentifier') == BUNDLE and info.get('CFBundleExecutable') == 'Lumen'
            and 'iPhoneSimulator' in info.get('CFBundleSupportedPlatforms', []),
            'Source bundle metadata does not identify the intended simulator app')
    entries = {}
    for name, value in sorted(guard.items()):
        mode = value['mode']
        if stat.S_ISREG(mode):
            entries[name] = {'kind': 'file', 'sha256': core.file_digest(app / name)}
        elif stat.S_ISLNK(mode):
            entries[name] = {'kind': 'symlink', 'target': value['link_target']}
        else:
            entries[name] = {'kind': 'directory'}
    require(all(name in entries and entries[name]['kind'] == 'file'
                for name in ('Lumen', 'Info.plist', '_CodeSignature/CodeResources', 'cacert.pem', 'icudtl.dat')),
            'Source bundle lacks a required signed resource')
    entry.check_bundle_guards({str(app): guard})
    return {'app': str(app), 'executable_sha256': entries['Lumen']['sha256'],
            'bundle_contents_sha256': core.digest(entries), 'entries': entries, 'guard': guard}


def dependency_guards(root):
    """Record existing selected library/header identities without rebuilding them."""
    prefixes = [root / 'build-apple-toolchain/sim/include', root / 'build-apple-toolchain/sim/lib',
                root / 'build-apple-toolchain/v8/include',
                root / 'build-apple-toolchain/v8/third_party/icu/source/common']
    records = {}
    for prefix in prefixes:
        require(prefix.is_dir(), 'Existing simulator dependency directory is missing: ' + str(prefix))
        for directory, children, files in os.walk(prefix, followlinks=False):
            for name in children + files:
                path = Path(directory) / name
                info = path.lstat()
                require(stat.S_ISDIR(info.st_mode) or stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode),
                        'Unexpected dependency file kind')
                if stat.S_ISLNK(info.st_mode):
                    require(path.resolve(strict=True).is_relative_to(root / 'build-apple-toolchain'),
                            'Simulator dependency symlink escapes the existing toolchain')
                records[str(path)] = [info.st_dev, info.st_ino, info.st_mode, info.st_size,
                                      info.st_mtime_ns, info.st_ctime_ns,
                                      os.readlink(path) if stat.S_ISLNK(info.st_mode) else None]
                require(len(records) <= 20000, 'Dependency inventory exceeds its entry bound')
    for relative in ('build-apple-toolchain/v8/out/ios-simulator/obj/libv8_monolith.a',
                     'build-apple-toolchain/v8/out/ios-simulator/obj/v8_monolith.ninja',
                     'build-apple-toolchain/v8/out/ios-simulator/icudtl.dat',
                     'build-apple-toolchain/v8/third_party/llvm-build/Release+Asserts/bin/llvm-objcopy'):
        path = root / relative
        require(path.is_file(), 'Required existing simulator dependency is missing: ' + relative)
        records[str(path)] = identity(path)
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(GENERATION) / 'build-identity-001')
    args = parser.parse_args()
    require(platform.system() == 'Darwin' and Path.cwd().resolve() == ROOT,
            'Execute only in the authorized Mac checkout through devinmac3d')
    root = ROOT.resolve(strict=True)
    output = (root / args.output).resolve()
    require(output.is_relative_to(root / GENERATION) and not output.exists(),
            'Use a fresh build evidence directory inside iPad006')
    target = root / BUILD
    require(not target.exists() and not target.is_symlink(), 'The isolated006 build output must be fresh')
    unexpected = [name for name in OVERRIDE_ENV if os.environ.get(name)]
    require(not unexpected, 'Unrecorded build environment overrides are present: ' + ', '.join(unexpected))
    require(shutil.disk_usage(root).free >= 3 * 1024**3, 'Less than3GiB free before the separate simulator build')
    source = root / 'tests/wpt-ipad005'
    for name, expected in READERS.items():
        require(fingerprint(source / name, root)['sha256'] == expected, 'Reviewed read-only helper changed: ' + name)
    require(fingerprint(root / 'tools/monitor_process.py', root)['sha256'] == MONITOR_SHA,
            'Approved process monitor changed')
    sys.path.insert(0, str(source))
    entry = importlib.import_module('ipad_runner')
    core = importlib.import_module('full_runner')
    prior_generation = importlib.import_module('ipad_generation')
    lifecycle = importlib.import_module('ipad_lifecycle')
    require(entry.AUTHORIZED_DEVICE == DEVICE, 'Read-only helper device identity changed')
    prior_binding = prior_generation.verify_generation(root)
    lifecycle_proof = lifecycle.verify_lifecycle(root)
    prior_report_path = root / PREVIOUS / 'smoke-report.json'
    prior_report = json.loads(prior_report_path.read_text())
    require(prior_report.get('execution_platform') == 'ipad-simulator'
            and prior_report.get('binding', {}).get('generation_binding') == prior_binding
            and prior_report.get('binding', {}).get('lifecycle') == lifecycle_proof,
            'Closed iPad005 report has different generation/lifecycle inputs')
    for name, count in (('http', 6), ('https', 1)):
        stage = prior_report.get('stages', {}).get(name, {})
        require(stage.get('safe_to_continue') is True and stage.get('status') == 'closed-complete'
                and stage.get('run_state', {}).get('finished_schedule') is True
                and stage.get('run_state', {}).get('all_scheduled_rows_recorded') is True,
                'Prior iPad005 ' + name + ' stage has not closed safely')
        require(len(json.loads((root / PREVIOUS / name / 'schedule.json').read_text())) == count,
                'Prior iPad005 schedule length changed')
    output.mkdir(parents=True, exist_ok=False)
    report = {'status': 'preparing', 'started_unix': time.time(), 'device': DEVICE,
              'bundle_id': BUNDLE, 'generation': GENERATION, 'build_output': BUILD,
              'helper_sha256': fingerprint(Path(__file__), root)['sha256'],
              'monitor_sha256': MONITOR_SHA, 'old_generation_binding': prior_binding,
              'old_lifecycle_evidence': lifecycle_proof, 'installed': False, 'runtime_tests_executed': False,
              'scope': 'Current-source simulator build only; build telemetry is not browser performance or same-IPA-on-Mac evidence.'}

    def save():
        core.write_json(output / 'build-identity.json', report)

    save()
    exit_code = 1
    try:
        with (root / 'output/wpt/ipad006-build.lock').open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            require(not target.exists(), 'Another build created the isolated output')
            before = source_manifest(root)
            for name, expected in SOURCE_ANCHORS.items():
                require(before[name]['sha256'] == expected, 'Current14814f31 source anchor changed: ' + name)
            snapshot = fingerprint(root / NATIVE_SNAPSHOT, root)
            require(snapshot['sha256'] == NATIVE_SHA and not (snapshot['identity'][2] & 0o222),
                    'Current Mac immutable snapshot identity changed')
            old = bundle_manifest(entry, core, root / 'build-sim/Lumen.app', root)
            require(old['executable_sha256'] == OLD_BINARY_SHA and old['bundle_contents_sha256'] == OLD_BUNDLE_SHA,
                    'The retained build003 signed source bundle changed')
            old_evidence = {str(path): fingerprint(path, root) for path in
                            [prior_report_path, root / PREVIOUS / 'binding.json',
                             *[root / PREVIOUS / stage / name for stage in ('http', 'https')
                               for name in ('run.json', 'schedule.json', 'results.jsonl')]]}
            dependencies = dependency_guards(root)
            report.update(status='source_frozen_building', source_manifest=before,
                          source_manifest_sha256=core.digest(before), native_snapshot=snapshot,
                          native_snapshot_path=NATIVE_SNAPSHOT, old_source_bundle=old,
                          old_evidence=old_evidence, dependency_identity_guards=dependencies,
                          dependency_scope='Existing library/header stat identities; not a new toolchain build or comprehensive dependency content attestation.')
            command = ['make', '-j3', 'PLAT=sim', 'BUILD=' + BUILD, 'app']
            prefix = output / 'simulator-build'
            report['build_command'] = command
            report['build_monitor'] = str(prefix.with_suffix('.json').relative_to(root))
            save()
            check_source_manifest(root, before)
            entry.check_bundle_guards({old['app']: old['guard']})
            require(dependency_guards(root) == dependencies, 'Toolchain input identities changed before compilation')
            invocation = [sys.executable, str(root / 'tools/monitor_process.py'), '--duration', '1800',
                          '--settle', '0', '--output', str(prefix), '--', *command]
            completed = subprocess.run(invocation, cwd=root, check=False)
            measurement = json.loads(prefix.with_suffix('.json').read_text())
            report['monitor_return_code'] = completed.returncode
            report['build_measurement_sha256'] = fingerprint(prefix.with_suffix('.json'), root)['sha256']
            report['build_measurement'] = measurement
            save()
            clean_monitor(measurement, completed.returncode)
            check_source_manifest(root, before)
            require(dependency_guards(root) == dependencies, 'Toolchain input identities changed during compilation')
            require(fingerprint(root / NATIVE_SNAPSHOT, root) == snapshot, 'Immutable Mac snapshot changed during compilation')
            require(bundle_manifest(entry, core, root / 'build-sim/Lumen.app', root) == old,
                    'Retained build003 signed source bundle changed during compilation')
            for path, expected in old_evidence.items():
                require(fingerprint(Path(path), root) == expected, 'Closed005 evidence changed during compilation')
            app = target / 'Lumen.app'
            signed = bundle_manifest(entry, core, app, root)
            with (output / 'source-signature-verify.log').open('x') as log:
                subprocess.run(['codesign', '--verify', '--strict', '--deep', str(app)],
                               cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=60, check=True)
            with (output / 'source-signature.txt').open('x') as log:
                subprocess.run(['codesign', '-dv', '--verbose=4', str(app)],
                               cwd=root, stdout=log, stderr=subprocess.STDOUT, timeout=30, check=True)
            entry.check_bundle_guards({str(app): signed['guard']})
            report.update(status='current_source_signed_simulator_build_verified', signed_source_bundle=signed,
                          source_signature_verified=True, source_inputs_unchanged=True,
                          old_source_and_evidence_unchanged=True)
            exit_code = 0
    except Exception as error:
        report.update(status='stopped_no_install', error=type(error).__name__ + ': ' + str(error))
    finally:
        report['finished_unix'] = time.time()
        save()
    print(json.dumps({key: report.get(key) for key in
                      ('status', 'error', 'build_output', 'source_signature_verified',
                       'old_source_and_evidence_unchanged', 'installed')}))
    return exit_code


if __name__ == '__main__':
    raise SystemExit(main())
