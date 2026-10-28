#!/usr/bin/env python3
"""Record exact official CfT artifact integrity without changing its ad-hoc signing.

The initial Developer-ID-style bundle-seal check is preserved as failed evidence.
This helper verifies the pinned HTTPS archive and every extracted member instead;
it neither changes the browser nor alters macOS security settings.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import platform
import plistlib
import stat
import subprocess
import time
import zipfile

HELPER_SHA256 = '8b0900cb70ebb0e1626a2c2a4cd6b0562834c04efccb8f449ed29bc4858b1b78'
ARCHIVE_SHA256 = 'ad7f2b49a4dc679af286b1b1b386670d93ca7868079f69012d21ba0ac8293df3'
PREFIX = 'chrome-mac-arm64'
VERSION = '156.0.8078.47'
POLICY = 'official_https_archive_integrity_no_developer_id_seal'
CHUNK = 1024 * 1024


def require(condition, reason):
    if not condition:
        raise RuntimeError(reason)


def identity(metadata):
    return [metadata.st_dev, metadata.st_ino, metadata.st_size,
            metadata.st_mtime_ns, metadata.st_ctime_ns, metadata.st_mode]


def hash_file(path):
    before = path.lstat()
    require(stat.S_ISREG(before.st_mode), 'Expected an ordinary file: ' + str(path))
    fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0))
    with os.fdopen(fd, 'rb') as source:
        require(identity(os.fstat(source.fileno())) == identity(before), 'File changed before hashing')
        value = hashlib.file_digest(source, 'sha256').hexdigest()
        require(identity(os.fstat(source.fileno())) == identity(before), 'File changed during hashing')
    require(identity(path.lstat()) == identity(before), 'File changed after hashing')
    return value


def load_preparer():
    path = Path(__file__).with_name('prepare_cft156.py')
    require(hash_file(path) == HELPER_SHA256, 'The reviewed archive validator changed')
    spec = importlib.util.spec_from_file_location('cft_pinned_preparer', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def scan_tree(root):
    """Enumerate actual entries without following any directory symlink."""
    require(root.is_dir() and not root.is_symlink(), 'Extraction root is not an ordinary directory')
    result = {}
    pending = [root]
    while pending:
        directory = pending.pop()
        for path in directory.iterdir():
            metadata = path.lstat()
            relative = path.relative_to(root).as_posix()
            kind = stat.S_IFMT(metadata.st_mode)
            require(kind in (stat.S_IFREG, stat.S_IFDIR, stat.S_IFLNK), 'Unexpected extracted file type')
            result[relative] = {'identity': identity(metadata), 'kind': kind}
            if kind == stat.S_IFDIR:
                pending.append(path)
    return result


def verify_extracted_archive(archive_path, extracted, validator):
    """Compare the entire tree, including extra paths, permissions and symlinks."""
    deadline = time.monotonic() + 120
    plan = validator.archive_plan(archive_path)
    archive_before = identity(archive_path.lstat())
    tree_before = scan_tree(extracted)
    expected, explicit = {}, set()
    with zipfile.ZipFile(archive_path) as archive:
        for member in archive.infolist():
            require(time.monotonic() < deadline, 'Artifact integrity check exceeded 120 seconds')
            name = member.filename.rstrip('/')
            mode = member.external_attr >> 16
            kind = stat.S_IFMT(mode)
            if kind == 0:
                kind = stat.S_IFDIR if member.is_dir() else stat.S_IFREG
            entry = {'path': name, 'kind': kind}
            if kind != stat.S_IFLNK:
                require(not mode & (stat.S_ISUID | stat.S_ISGID), 'Privileged archive mode is unexpected')
                entry['permissions'] = stat.S_IMODE(mode)
            if kind == stat.S_IFLNK:
                entry['target'] = archive.read(member).decode('utf-8')
            elif kind == stat.S_IFREG:
                with archive.open(member) as source:
                    checksum, count = hashlib.sha256(), 0
                    for data in iter(lambda: source.read(CHUNK), b''):
                        require(time.monotonic() < deadline, 'Artifact integrity check exceeded 120 seconds')
                        checksum.update(data)
                        count += len(data)
                require(count == member.file_size, 'Archive member length changed')
                entry.update(size=count, sha256=checksum.hexdigest())
            expected[name] = entry
            explicit.add(name)
        for name in list(expected):
            for parent in PurePosixPath(name).parents:
                if str(parent) == '.':
                    continue
                key = parent.as_posix()
                if key in expected:
                    require(expected[key]['kind'] == stat.S_IFDIR, 'Member lies beneath a non-directory')
                else:
                    expected[key] = {'path': key, 'kind': stat.S_IFDIR}
    require(set(tree_before) == set(expected), 'Extraction has missing or additional archive paths')
    rows = []
    for name in sorted(expected):
        require(time.monotonic() < deadline, 'Artifact integrity check exceeded 120 seconds')
        entry, actual = expected[name], tree_before[name]
        require(entry['kind'] == actual['kind'], 'Extracted entry type differs: ' + name)
        path = extracted / name
        if 'permissions' in entry:
            require(stat.S_IMODE(path.lstat().st_mode) == entry['permissions'],
                    'Extracted permissions differ: ' + name)
        if entry['kind'] == stat.S_IFREG:
            require(path.lstat().st_size == entry['size'] and hash_file(path) == entry['sha256'],
                    'Extracted bytes differ: ' + name)
        elif entry['kind'] == stat.S_IFLNK:
            require(os.readlink(path) == entry['target'], 'Extracted symlink differs: ' + name)
        rows.append({**entry, 'explicit_archive_member': name in explicit,
                     'verified_identity': actual['identity']})
    require(scan_tree(extracted) == tree_before, 'Extraction changed while verifying its contents')
    require(identity(archive_path.lstat()) == archive_before, 'Archive changed while verifying extraction')
    content_rows = [{key: value for key, value in row.items() if key != 'verified_identity'} for row in rows]
    content_digest = hashlib.sha256(json.dumps(content_rows, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    return {'archive_plan': plan, 'tree_sha256': content_digest, 'entries': rows}


def command_evidence(argv, timeout=30):
    result = subprocess.run(argv, capture_output=True, text=True, errors='replace', timeout=timeout)
    return {'argv': argv, 'returncode': result.returncode,
            'stdout': result.stdout[-16000:], 'stderr': result.stderr[-16000:]}


def verify_reference(reference_path, repo, *, create=False):
    require(platform.system() == 'Darwin' and platform.machine() == 'arm64', 'Run only on the authorized arm64 Mac')
    repo = repo.resolve(strict=True)
    reference_path = reference_path.resolve(strict=True)
    preparation = reference_path.parent
    allowed = repo / 'output/wpt/reference-browsers'
    require(preparation.is_relative_to(allowed) and preparation != allowed,
            'Reference manifest must be in a project-local preparation directory')
    require(reference_path.name == ('reference.json' if create else 'artifact-integrity-v1.json'),
            'Unexpected reference record name')
    original = preparation / 'reference.json'
    original_hash = hash_file(original)
    record = json.loads(original.read_text())
    validator = load_preparer()
    require(record.get('status') == 'failed' and
            'code has no resources but signature indicates they must be present' in record.get('error', ''),
            'Original failure does not match the diagnosed upstream resource-seal shape')
    require(record['version'] == VERSION and record['revision'] == validator.REVISION and
            record['url'] == validator.URL and record['generation'] == validator.GENERATION and
            record['pinned_md5'] == validator.MD5 and record['pinned_bytes'] == validator.LENGTH,
            'Original official-artifact provenance differs')
    verified_download = [attempt for attempt in record['attempts'] if attempt.get('status') == 'verified']
    require(len(verified_download) == 1, 'Original download verification is ambiguous')
    download = verified_download[0]
    require(download['sha256'] == ARCHIVE_SHA256 and download['md5'] == validator.MD5 and
            download['bytes'] == validator.LENGTH, 'Original archive content evidence differs')
    archive = preparation / 'chrome-mac-arm64.zip'
    require(archive.stat().st_size == validator.LENGTH and hash_file(archive) == ARCHIVE_SHA256,
            'The exact verified official archive changed')
    extracted = preparation / 'extracted'
    tree = verify_extracted_archive(archive, extracted, validator)
    app = extracted / PREFIX / 'Google Chrome for Testing.app'
    info_path = app / 'Contents/Info.plist'
    info = plistlib.loads(info_path.read_bytes())
    require(info.get('CFBundleShortVersionString') == VERSION, 'Extracted version differs')
    executable = info.get('CFBundleExecutable')
    require(isinstance(executable, str) and executable not in ('', '.', '..') and '/' not in executable,
            'Invalid bundle executable name')
    binary = app / 'Contents/MacOS' / executable
    require(binary.resolve(strict=True).is_relative_to(app), 'Executable escapes its app')
    identity_evidence = command_evidence(['/usr/bin/codesign', '-dv', '--verbose=4', str(app)])
    strict = command_evidence(['/usr/bin/codesign', '--verify', '--strict', '--deep', str(app)], 120)
    text = identity_evidence['stdout'] + identity_evidence['stderr']
    require(identity_evidence['returncode'] == 0 and 'Signature=adhoc' in text and
            'linker-signed' in text and 'Info.plist=not bound' in text and
            'TeamIdentifier=not set' in text and 'Sealed Resources=none' in text,
            'Observed signing shape differs from the exact diagnosed test artifact')
    require(strict['returncode'] != 0 and 'code has no resources but signature indicates they must be present' in strict['stderr'],
            'Strict bundle verification no longer matches the diagnosed evidence')
    require(not any('_CodeSignature' in row['path'].split('/') or row['path'].endswith('/CodeResources')
                    for row in tree['entries']), 'This exact artifact unexpectedly includes resource seals')
    arch = command_evidence(['/usr/bin/lipo', '-archs', str(binary)])
    require(arch['returncode'] == 0 and 'arm64' in arch['stdout'].split(), 'No verified arm64 executable')
    result = {'schema': 1, 'status': 'official_artifact_verified', 'verification_policy': POLICY,
              'version': VERSION, 'revision': validator.REVISION, 'url': validator.URL,
              'generation': validator.GENERATION, 'archive_sha256': ARCHIVE_SHA256,
              'archive': str(archive), 'extracted': str(extracted), 'app': str(app),
              'binary': str(binary), 'binary_sha256': hash_file(binary), 'info_sha256': hash_file(info_path),
              'original_reference': str(original), 'original_reference_sha256': original_hash,
              'original_status': record['status'], 'original_error': record.get('error'),
              'strict_bundle_verification_passed': False, 'developer_id_verified': False,
              'signature_classification': 'ad_hoc_linker_signed_with_no_bundle_resource_seal',
              'signature_identity': identity_evidence, 'strict_bundle_verification': strict,
              'architectures': arch, 'integrity': tree, 'helper_sha256': hash_file(Path(__file__)),
              'browser_modified': False, 'platform_security_modified': False, 'browser_launched': False,
              'evidence_scope': 'Exact official HTTPS archive and complete extracted contents verified; no Developer ID or notarization claim',
              'packaging_source': {'url': 'https://github.com/GoogleChromeLabs/chrome-for-testing/issues/205',
                  'qualification': 'Maintainer says CfT packages are immutable and binaries are unsigned; discussion is Windows. Mac signing classification above is directly observed.'}}
    require(hash_file(original) == original_hash, 'Original failure evidence changed during verification')
    require(scan_tree(extracted) == {row['path']: {'identity': row['verified_identity'], 'kind': row['kind']}
                                     for row in tree['entries']},
            'Extracted reference changed during the signing observation')
    if not create:
        stored = json.loads(reference_path.read_text())
        for key in ('status', 'verification_policy', 'version', 'revision', 'url', 'generation', 'archive_sha256',
                    'archive', 'extracted', 'app', 'binary', 'binary_sha256', 'info_sha256',
                    'original_reference_sha256', 'signature_classification', 'helper_sha256'):
            require(stored.get(key) == result[key], 'Reference manifest mismatch: ' + key)
        require(stored['integrity']['tree_sha256'] == tree['tree_sha256'], 'Full extracted reference contents changed')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', required=True, type=Path)
    parser.add_argument('--preparation', required=True, type=Path)
    args = parser.parse_args()
    destination = args.preparation / 'artifact-integrity-v1.json'
    require(not destination.exists(), 'The integrity record already exists; preserve it')
    result = verify_reference(args.preparation / 'reference.json', args.repo, create=True)
    with destination.open('x') as target:
        json.dump(result, target, indent=2, sort_keys=True)
        target.write('\n')
    print(json.dumps({'status': result['status'], 'manifest': str(destination), 'version': result['version'],
                      'entries': len(result['integrity']['entries']),
                      'strict_bundle_verification_passed': False, 'browser_launched': False}))


if __name__ == '__main__':
    main()
