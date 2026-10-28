#!/usr/bin/env python3
"""Recover only reviewed, reproducible objects in this session's build directory.

No profiles, source, executables, snapshots, other builds, or directories are
removed. The default is inspection. An explicit --apply is required for cleanup.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

EXPECTED_BINARY = '5e91b1109980af527ce8e8e81b5f9352c45cb75c119b639bcc3df58faa90e20f'
BUILD_TOOLS = {'make', 'gmake', 'ninja', 'cmake', 'clang', 'clang++', 'cc', 'c++', 'ld', 'ld64', 'swift', 'swiftc'}


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def objects(root):
    build = root / 'build-apple-gpt6as'
    if not build.is_dir() or build.is_symlink() or not build.resolve().is_relative_to(root):
        raise RuntimeError('Expected real task build directory is missing or redirected')
    result = []
    for path in sorted(build.rglob('*.o')):
        if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(build.resolve()):
            raise RuntimeError('An object candidate is not a regular file inside the task build')
        stat = path.stat()
        result.append({'path': str(path.relative_to(root)), 'bytes': stat.st_size,
                       'mtime_ns': stat.st_mtime_ns, 'inode': stat.st_ino})
    return result


def active_builds(root):
    processes = subprocess.run(['ps', '-axo', 'pid=,comm='], capture_output=True, text=True,
                               timeout=5, check=True).stdout
    candidates = []
    for line in processes.splitlines():
        parts = line.strip().split(None, 1)
        if len(parts) == 2 and Path(parts[1]).name in BUILD_TOOLS:
            candidates.append((int(parts[0]), Path(parts[1]).name))
    if len(candidates) > 12:
        raise RuntimeError('Too many active build tools to establish a bounded safe cleanup')
    found = []
    for pid, name in candidates:
        result = subprocess.run(['lsof', '-a', '-p', str(pid), '-d', 'cwd', '-Fn'],
                                capture_output=True, text=True, timeout=2)
        paths = [line[1:] for line in result.stdout.splitlines() if line.startswith('n')]
        if not paths:
            # A tool can exit during inspection. A still-live unidentifiable
            # build process is grounds to stop, rather than guessing ownership.
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                continue
            raise RuntimeError('Cannot establish working directory of an active build tool')
        if any(Path(value).resolve().is_relative_to(root) for value in paths):
            found.append({'pid': pid, 'tool': name})
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--expected-count', type=int, default=47)
    parser.add_argument('--output', type=Path, default=Path('output/session-20261028/native-disk-recovery'))
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('This recovery targets only the reviewed original Mac')
    root = Path.cwd().resolve()
    binary = root / 'build-apple-gpt6as/lumen'
    if binary.is_symlink() or digest(binary) != EXPECTED_BINARY:
        parser.error('Task executable identity differs; no cleanup permitted')
    manifest = objects(root)
    if len(manifest) != args.expected_count:
        parser.error('Object count changed since review; inspect again before cleanup')
    builds = active_builds(root)
    if builds:
        parser.error('A build is active in this project; no cleanup permitted')
    before_free = shutil.disk_usage(root).free
    result = {'schemaVersion': 1, 'startedUTC': datetime.now(timezone.utc).isoformat(),
              'scope': 'Only task-owned reproducible build-apple-gpt6as/*.o; executable retained',
              'binarySHA256': EXPECTED_BINARY, 'objectManifest': manifest,
              'objectBytes': sum(row['bytes'] for row in manifest), 'freeBytesBefore': before_free,
              'apply': args.apply, 'removed': []}
    if not args.apply:
        print(json.dumps(result))
        return 0
    destination = args.output.resolve()
    if not destination.is_relative_to(root / 'output') or destination.exists():
        parser.error('Use a new output directory inside the project')
    destination.mkdir(parents=True, exist_ok=False)
    manifest_path = destination / 'before.json'
    with manifest_path.open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n'); stream.flush(); os.fsync(stream.fileno())
    # Revalidate the complete set immediately before removing any object.
    if objects(root) != manifest or active_builds(root):
        raise RuntimeError('Build state changed after the manifest was written; nothing removed')
    try:
        for row in manifest:
            path = root / row['path']
            stat = path.stat()
            if path.is_symlink() or stat.st_ino != row['inode'] or stat.st_size != row['bytes'] or stat.st_mtime_ns != row['mtime_ns']:
                raise RuntimeError('An object changed during cleanup; stopping before this object')
            path.unlink()
            result['removed'].append(row['path'])
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['freeBytesAfter'] = shutil.disk_usage(root).free
        result['binaryPreserved'] = digest(binary) == EXPECTED_BINARY
        result['finishedUTC'] = datetime.now(timezone.utc).isoformat()
        (destination / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps({'receipt': str(destination / 'receipt.json'), 'removedCount': len(result['removed']),
                          'freeMiBAfter': round(result['freeBytesAfter'] / 1048576, 1),
                          'binaryPreserved': result['binaryPreserved']}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
