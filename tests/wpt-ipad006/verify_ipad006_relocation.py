#!/usr/bin/env python3
"""Verify the already installed006 app and archive the relocated failed004 profile.

This does not install, launch, terminate, boot, shut down, erase, or delete.
The original stopped install and failed runtime records remain unchanged.
Historical profile evidence is inode/stat identity only. Content hashes were
first observed after relocation and are never represented as earlier hashes.
"""
import argparse
import fcntl
import hashlib
import importlib
import json
import os
from pathlib import Path
import platform
import stat
import subprocess
import sys
import time

ROOT = Path('/Users/devin/repos/lumen')
DEVICE = 'A77F68E4-972B-472B-80B8-F65E71D361E5'
BUNDLE = 'com.ecooxai.lumen'
GENERATION = Path('output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006')
FAILURE = GENERATION / 'install-identity-001/install-verify.json'
FAILURE_CANONICAL = 'fb225cf19b19d4f5508655b84366ea9d96b4499dd507fff5819ff230320ffe0f'
OLD_DATA = Path('/Users/devin/Library/Developer/CoreSimulator/Devices') / DEVICE / 'data/Containers/Data/Application/92A1643F-B58E-4827-9FB0-85461BF04167'
NEW_DATA = OLD_DATA.parent / 'BB2BD9D4-CFEC-4ADD-8770-5C2CBC873896'
PROFILE_RELATIVE = Path('tmp/lumen-wpt-n9ezxwc8')
OLD_PROFILE = OLD_DATA / PROFILE_RELATIVE
NEW_PROFILE = NEW_DATA / PROFILE_RELATIVE
OUTPUT = GENERATION / 'install-relocation-002'
ARCHIVE_NAME = 'retained-failed004-profile'
OBSERVED_HASHES = {
    'history.txt': '21342ed431698cc063c6972040761bf3ca66405d989f0e8141b532e6b3db2163',
    'wpt-ca.pem': '6760c932218bfe2703b10f2ea89a54b409f04d94dc5a1c9cf0f1fe7da8b30b52',
}
PINS = {
    'ipad_runner.py': 'a15d1454fe147c588b0b1597233e8d11694920541eb6ecfc14aa8f4858e331df',
    'ipad_adapter.py': '885d9aed25b8525f9370246f3bc87b05d72713052749cb9a06a0198a25434fc4',
    'full_runner.py': 'a7bc96c3697fb099ad63c145011bc434a66d865a713a62e5198731bed9600ac7',
    'ipad_generation.py': 'ddfc70cf9a2388208c1b2b47a840fc41fa228cc41615f614909d57d122d803dd',
    'ipad_lifecycle.py': 'fdf39fca49c5e72bbb05d132a07c6abbc79ae37d15b49e4c280e863d85a6bbbf',
    'install_verify_ipad.py': '8ac7ffebd69693ad58b1e5b7ac24da84bdf63e0bc2482504ff1232299de7206d',
}
NOTE = ('Exact pre-install/current filesystem metadata matches all six entries. '
        'The recorded absolute data-container path changed. File hashes were '
        'first observed after relocation; no historical content digest existed. '
        'The original install001 and runtime004 gates remain failed.')


def require(value, message):
    if not value:
        raise RuntimeError(message)


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def canonical(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def identity(value):
    return {'device': value.st_dev, 'inode': value.st_ino, 'mode': value.st_mode,
            'size': value.st_size, 'mtime_ns': value.st_mtime_ns, 'ctime_ns': value.st_ctime_ns}


def profile_tree(path):
    """Small exact profile inventory; never follow a symlink or read file bytes."""
    path = Path(path)
    require(path.is_dir() and not path.is_symlink() and path.resolve(strict=True) == path,
            'Profile/archive root is missing, aliased, or not a directory')
    records = {}
    def record(item):
        value = item.lstat()
        require(stat.S_ISREG(value.st_mode) or stat.S_ISDIR(value.st_mode),
                'Profile contains a symlink or special file')
        records[item.relative_to(path).as_posix()] = identity(value)
        require(len(records) <= 64, 'Profile entry bound exceeded')
        require(sum(v['size'] for v in records.values() if stat.S_ISREG(v['mode'])) <= 65536,
                'Profile byte bound exceeded')
    def fail(error):
        raise error
    record(path)
    for directory, directories, files in os.walk(path, followlinks=False, onerror=fail):
        for name in sorted(directories + files):
            record(Path(directory) / name)
    return records


def contents(path, expected_tree, expected_hashes):
    require(profile_tree(path) == expected_tree, 'Profile metadata changed')
    regular = {name: value for name, value in expected_tree.items() if stat.S_ISREG(value['mode'])}
    require(set(regular) == set(expected_hashes), 'Unexpected profile/archive file set')
    records = {}
    for name, value in sorted(regular.items()):
        fd = os.open(Path(path) / name, os.O_RDONLY | os.O_NOFOLLOW)
        with os.fdopen(fd, 'rb') as source:
            require(identity(os.fstat(source.fileno())) == value, 'File changed before hashing')
            observed = hashlib.file_digest(source, 'sha256').hexdigest()
            require(identity(os.fstat(source.fileno())) == value, 'File changed while hashing')
        require(observed == expected_hashes[name], 'Observed-after-relocation file digest changed: ' + name)
        records[name] = {'bytes': value['size'], 'sha256': observed}
    require(profile_tree(path) == expected_tree, 'Profile changed during content verification')
    return records


def archive_profile(source, destination, expected_tree, expected_hashes):
    source, destination = Path(source), Path(destination)
    observed = contents(source, expected_tree, expected_hashes)
    destination.mkdir(mode=0o700, exist_ok=False)
    directories = sorted((name for name, value in expected_tree.items() if stat.S_ISDIR(value['mode'])),
                         key=lambda name: (len(Path(name).parts), name))
    for name in directories:
        if name != '.':
            (destination / name).mkdir(mode=0o700, exist_ok=False)
    for name, expected in sorted(observed.items()):
        fd = os.open(source / name, os.O_RDONLY | os.O_NOFOLLOW)
        with os.fdopen(fd, 'rb') as before, (destination / name).open('xb') as copied:
            require(identity(os.fstat(before.fileno())) == expected_tree[name], 'Copy source changed')
            size = 0
            while True:
                chunk = before.read(16384)
                if not chunk:
                    break
                size += len(chunk)
                require(size <= expected['bytes'], 'Copy source grew')
                copied.write(chunk)
            require(size == expected['bytes'] and identity(os.fstat(before.fileno())) == expected_tree[name],
                    'Copy source changed during archive')
        os.chmod(destination / name, stat.S_IMODE(expected_tree[name]['mode']) & ~0o222)
    for name in reversed(directories):
        os.chmod(destination / name, stat.S_IMODE(expected_tree[name]['mode']) & ~0o222)
    archive_tree = profile_tree(destination)
    require(contents(destination, archive_tree, expected_hashes) == observed, 'Source/archive bytes differ')
    require(contents(source, expected_tree, expected_hashes) == observed, 'Source changed while archiving')
    return {'files': observed, 'relative_directories': sorted(directories), 'identity': archive_tree,
            'content_provenance': 'First observed after relocation; copied without changing the original profile',
            'historical_content_digest_available': False, 'read_only_copy': True}


def original_failure(root):
    path = root / FAILURE
    before = identity(path.stat())
    value = json.loads(path.read_text())
    require(canonical(value) == FAILURE_CANONICAL and identity(path.stat()) == before,
            'Exact original failed install evidence changed')
    require(value['status'] == 'stopped' and value['device'] == DEVICE and value['bundle_id'] == BUNDLE
            and value['error'] == 'RuntimeError: The retained failed004 profile is missing or changed',
            'Unexpected original install failure')
    require(value['retained_failure_profile']['path'] == str(OLD_PROFILE), 'Original profile path differs')
    observed = digest(path)
    require(identity(path.stat()) == before, 'Original failed install changed while hashing')
    return value, {'path': str(FAILURE), 'sha256': observed, 'canonical_sha256': FAILURE_CANONICAL,
                   'identity': before, 'original_status': value['status'], 'original_error': value['error']}


def registered_data():
    result = subprocess.run(['xcrun', 'simctl', 'get_app_container', DEVICE, BUNDLE, 'data'],
                            capture_output=True, text=True, timeout=20, check=True)
    require(result.stdout.strip() == str(NEW_DATA) and NEW_DATA.resolve(strict=True) == NEW_DATA,
            'Exact relocated data-container registration changed')
    return str(NEW_DATA)


def verify_receipt(root, path, generation_binding, lifecycle_binding):
    """Runtime preflight: retain strict metadata, archive, and original-failure guards."""
    path = Path(path)
    require(path == root / OUTPUT / 'install-verify.json' and path.resolve(strict=True) == path,
            'Use only the separately recorded relocation verification')
    before = identity(path.stat())
    report = json.loads(path.read_text())
    original, failed = original_failure(root)
    require(report['status'] == 'installed_and_verified_after_profile_relocation'
            and report['device'] == DEVICE and report['bundle_id'] == BUNDLE
            and report['generation_binding'] == generation_binding
            and report['lifecycle_evidence'] == lifecycle_binding
            and report['original_failed_install'] == failed
            and report['original_install_gate_passed'] is False,
            'Relocation proof differs from the actual generation or retained failed gate')
    relocation = report['profile_relocation']
    expected = original['retained_failure_profile']['identity_before']
    require(relocation['old_path'] == str(OLD_PROFILE) and relocation['new_path'] == str(NEW_PROFILE)
            and relocation['metadata_before'] == expected and relocation['metadata_after'] == expected
            and relocation['exact_metadata_preserved'] is True
            and relocation['historical_content_digest_available'] is False,
            'The original/relocated profile identities or provenance differ')
    require(not os.path.lexists(OLD_PROFILE), 'Old profile path unexpectedly reappeared')
    observed = contents(NEW_PROFILE, expected, OBSERVED_HASHES)
    require(relocation['observed_after_relocation_files'] == observed, 'Observed file evidence differs')
    archive = report['archive']
    archive_path = root / OUTPUT / ARCHIVE_NAME
    require(archive['path'] == str(archive_path.relative_to(root))
            and archive['historical_content_digest_available'] is False
            and archive['read_only_copy'] is True,
            'Archive path or provenance changed')
    require(contents(archive_path, archive['identity'], OBSERVED_HASHES) == observed == archive['files'],
            'Exclusive archive changed')
    require(identity(path.stat()) == before and digest(Path(__file__)) == report['verifier_sha256'],
            'Verification receipt or reviewed helper changed')
    qualification = {'verification_report': str(path.relative_to(root)), 'verification_sha256': digest(path),
                     'original_failed_install': failed, 'original_install_gate_passed': False,
                     'profile_old_path': str(OLD_PROFILE), 'profile_current_path': str(NEW_PROFILE),
                     'profile_metadata_preserved': True, 'archive_path': archive['path'],
                     'archive_files': observed, 'historical_content_digest_available': False,
                     'qualification': NOTE}
    return report, qualification


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    require(platform.system() == 'Darwin' and Path.cwd().resolve() == ROOT,
            'Run only on the authorized Mac in the actual project')
    source = ROOT / 'tests/wpt-ipad006'
    for name, expected in PINS.items():
        require(digest(source / name) == expected, 'Reviewed source changed: ' + name)
    sys.path.insert(0, str(source))
    core = importlib.import_module('full_runner')
    entry = importlib.import_module('ipad_runner')
    adapter = importlib.import_module('ipad_adapter')
    generation = importlib.import_module('ipad_generation')
    lifecycle = importlib.import_module('ipad_lifecycle')
    installer = importlib.import_module('install_verify_ipad')
    binding, lifecycle_binding = generation.verify_generation(ROOT), lifecycle.verify_lifecycle(ROOT)
    original, failed = original_failure(ROOT)
    child_monitor = ROOT / GENERATION / 'install-identity-001/simulator-install.json'
    measured = json.loads(child_monitor.read_text())
    require(measured['natural_exit_code'] == 0 and measured['ended_by_monitor'] is False
            and not measured['collector_error'] and not measured['violations']
            and measured['owned_group_cleanup'] == {'sent_sigterm': False, 'sent_sigkill': False, 'error': None},
            'The already completed install CLI was not clean')
    output = ROOT / OUTPUT
    require((ROOT / GENERATION).resolve(strict=True) == ROOT / GENERATION,
            'Generation evidence directory changed to an alias')
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    report = {'status': 'verifying', 'device': DEVICE, 'bundle_id': BUNDLE,
              'started_unix': time.time(), 'generation_binding': binding, 'lifecycle_evidence': lifecycle_binding,
              'original_failed_install': failed, 'original_install_gate_passed': False,
              'completed_install_monitor_sha256': digest(child_monitor), 'reviewed_sources': PINS,
              'verifier_sha256': digest(Path(__file__)), 'qualification': NOTE,
              'scope': 'Read-only installed-bundle/relocation verification plus exclusive project evidence copy; no app or simulator lifecycle actions'}
    code = 1
    try:
        with (ROOT / 'output/wpt' / ('ipad-' + DEVICE + '.lock')).open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            before = installer.preinstall_state(adapter)
            require(not before['running_instances'] and before['registered_app'], 'Selected iPad app must be installed and idle')
            report['device_state_before'] = before
            report['registered_data_container'] = registered_data()
            require(not os.path.lexists(OLD_PROFILE), 'Old profile path unexpectedly reappeared')
            expected = original['retained_failure_profile']['identity_before']
            observed = contents(NEW_PROFILE, expected, OBSERVED_HASHES)
            report['profile_relocation'] = {'old_path': str(OLD_PROFILE), 'new_path': str(NEW_PROFILE),
                'metadata_before': expected, 'metadata_after': profile_tree(NEW_PROFILE),
                'exact_metadata_preserved': True, 'observed_after_relocation_files': observed,
                'historical_content_digest_available': False}
            proof, guards = entry.verify_signed_bundles(ROOT / 'build-sim-ipad006/Lumen.app', Path(before['registered_app']))
            require(proof['matching_bundle_contents_sha256'] == generation.BUNDLE_SHA
                    and proof['critical_files_sha256']['source']['Lumen'] == generation.BINARY_SHA
                    and proof['critical_files_sha256']['installed']['Lumen'] == generation.BINARY_SHA,
                    'Installed/source signed006 bundle no longer matches its actual build')
            report['signed_bundle_provenance'] = proof
            core.write_json(output / 'install-verify.json', report)
            archive = archive_profile(NEW_PROFILE, output / ARCHIVE_NAME, expected, OBSERVED_HASHES)
            archive['path'] = str((output / ARCHIVE_NAME).relative_to(ROOT))
            report['archive'] = archive
            after = installer.preinstall_state(adapter)
            report['device_state_after'] = after
            require(not after['running_instances'] and before['registered_app'] == after['registered_app']
                    and registered_data() == report['registered_data_container'],
                    'Device/app/data registration changed during verification')
            entry.check_bundle_guards(guards)
            require(generation.verify_generation(ROOT) == binding and lifecycle.verify_lifecycle(ROOT) == lifecycle_binding,
                    'Generation or original lifecycle evidence changed')
            require(original_failure(ROOT)[1] == failed, 'Original failed install evidence changed')
            require(contents(NEW_PROFILE, expected, OBSERVED_HASHES) == archive['files'], 'Retained profile changed')
            report.update(status='installed_and_verified_after_profile_relocation',
                          source_executable_sha256=generation.BINARY_SHA, installed_executable_sha256=generation.BINARY_SHA,
                          matching_bundle_contents_sha256=generation.BUNDLE_SHA)
            code = 0
    except Exception as error:
        report.update(status='stopped', error=type(error).__name__ + ': ' + str(error))
    finally:
        report['finished_unix'] = time.time()
        core.write_json(output / 'install-verify.json', report)
    print(json.dumps({'status': report['status'], 'receipt': str(output / 'install-verify.json'),
                      'original_install_gate_passed': False, 'error': report.get('error')}))
    return code


if __name__ == '__main__':
    raise SystemExit(main())
