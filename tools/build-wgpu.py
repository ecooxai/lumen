#!/usr/bin/env python3
"""Build pinned project-local macOS WebGPU with the upstream first-frame fix.

No Homebrew file is changed. The original native C ABI, default features and
Cargo.lock versions are preserved; only wgpu-hal's Metal visibility guard is
backported from gfx-rs/wgpu commit 05ae83af5ed55e826b91ea339f4210d586dfa337.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import tomllib
import urllib.request

VERSION = 'v29.0.1.1'
COMMIT = '6aed50955d934ac36049ba8d002034841633ae02'
HEADERS_COMMIT = '673658bc2bd70ec39fc55ebe6bb0173cf6d0a603'
LOCK_SHA = '9f780c3e7e792c19ae22e708cf899eb177b5696c9ab61215c6ae235adcecbdb0'
HAL_VERSION = '29.0.3'
HAL_SHA = '31f8e1a9e7a8512f276f7c62e018c7fa8d60954303fed2e5750114332049193f'
UPSTREAM_FIX = '05ae83af5ed55e826b91ea339f4210d586dfa337'


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args: list[str], **kwargs) -> None:
    print('+', ' '.join(args), flush=True)
    subprocess.run(args, check=True, **kwargs)


def output(args: list[str], **kwargs) -> str:
    return subprocess.check_output(args, text=True, **kwargs).strip()


def files(directory: Path) -> dict[str, str]:
    result = {}
    for path in sorted(directory.rglob('*')):
        if path.is_symlink():
            raise RuntimeError('Unexpected symlink in prepared source: ' + str(path))
        if path.is_file() and path.name != '.lumen-source-manifest.json':
            result[str(path.relative_to(directory))] = sha(path)
    return result


def download_hal(archives: Path, destination: Path) -> None:
    archives.mkdir(parents=True, exist_ok=True)
    archive = archives / ('wgpu-hal-' + HAL_VERSION + '.crate')
    if not archive.exists():
        url = 'https://static.crates.io/crates/wgpu-hal/' + archive.name
        with urllib.request.urlopen(url, timeout=45) as response:
            data = response.read(8 * 1024 * 1024 + 1)
        if len(data) > 8 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != HAL_SHA:
            raise RuntimeError('The downloaded HAL crate does not match Cargo.lock')
        temporary = archive.with_suffix('.download-' + str(os.getpid()))
        temporary.write_bytes(data)
        temporary.replace(archive)
    if sha(archive) != HAL_SHA:
        raise RuntimeError('Existing HAL archive differs from the pinned checksum; left unchanged')
    destination.mkdir(parents=True, exist_ok=False)
    with tarfile.open(archive) as source:
        for member in source.getmembers():
            parts = PurePosixPath(member.name).parts
            if (not parts or parts[0] != 'wgpu-hal-' + HAL_VERSION or '..' in parts
                    or PurePosixPath(member.name).is_absolute()
                    or member.issym() or member.islnk()
                    or not (member.isfile() or member.isdir())):
                raise RuntimeError('Unexpected HAL archive member')
            path = destination.joinpath(*parts[1:])
            if member.isdir():
                path.mkdir(parents=True, exist_ok=True)
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(member) as data:
                    path.write_bytes(data.read())


def prepare(root: Path, patch: Path) -> tuple[Path, dict]:
    source = root / 'build-deps' / 'sources' / ('wgpu-native-' + VERSION)
    source.parent.mkdir(parents=True, exist_ok=True)
    if not source.exists():
        run(['git', 'clone', '--branch', VERSION, '--depth', '1', '--recurse-submodules',
             '--shallow-submodules', 'https://github.com/gfx-rs/wgpu-native.git', str(source)])
    if output(['git', 'rev-parse', 'HEAD'], cwd=source) != COMMIT:
        raise RuntimeError('Existing native WebGPU source has a different revision; left unchanged')
    if output(['git', 'status', '--porcelain'], cwd=source):
        raise RuntimeError('Existing native WebGPU source is dirty; left unchanged')
    headers = source / 'ffi' / 'webgpu-headers'
    if output(['git', 'rev-parse', 'HEAD'], cwd=headers) != HEADERS_COMMIT:
        raise RuntimeError('Native WebGPU C headers do not match the pinned revision')
    if sha(source / 'Cargo.lock') != LOCK_SHA:
        raise RuntimeError('Original native WebGPU Cargo.lock does not match the pinned source')
    patch_sha = sha(patch)
    prepared = root / 'build-deps' / 'wgpu-work' / (VERSION + '-first-visible-' + patch_sha[:12])
    marker = prepared / '.lumen-source-manifest.json'
    if prepared.exists():
        if not marker.is_file():
            raise RuntimeError('Existing prepared source has no ownership manifest; left unchanged')
        record = json.loads(marker.read_text())
        if record.get('patchSha256') != patch_sha or record.get('files') != files(prepared):
            raise RuntimeError('Prepared source was modified outside this builder; left unchanged')
        return prepared / 'native', record
    prepared.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix='wgpu-prepare-', dir=prepared.parent))
    try:
        native = temporary / 'native'
        # Skip examples and repository metadata: only the library's own inputs
        # are needed. The pristine pinned checkout remains untouched.
        shutil.copytree(source, native, ignore=shutil.ignore_patterns('.git', 'target', 'examples'), symlinks=True)
        hal = temporary / 'hal'
        download_hal(root / 'build-deps' / 'archives', hal)
        # Apply outside the enclosing browser Git repository, so Git cannot
        # silently skip paths as belonging outside its current subdirectory.
        patch_env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(hal.parent))
        run(['git', 'apply', '--check', str(patch)], cwd=hal, env=patch_env)
        run(['git', 'apply', str(patch)], cwd=hal, env=patch_env)
        manifest = native / 'Cargo.toml'
        text = manifest.read_text()
        if '[patch.crates-io]' in text:
            raise RuntimeError('Pinned source unexpectedly contains a crates.io patch table')
        manifest.write_text(text + '\n# Lumen: upstream Metal first-frame backport only.\n'
                            '[patch.crates-io]\nwgpu-hal = { path = "../hal" }\n')
        lock = native / 'Cargo.lock'
        blocks = lock.read_text().split('[[package]]')
        count = 0
        for index, block in enumerate(blocks):
            if '\nname = "wgpu-hal"\n' in block:
                expected = ('source = "registry+https://github.com/rust-lang/crates.io-index"\n'
                            'checksum = "' + HAL_SHA + '"\n')
                if block.count(expected) != 1 or '\nversion = "' + HAL_VERSION + '"\n' not in block:
                    raise RuntimeError('The HAL Cargo.lock entry is not the expected pinned crate')
                blocks[index] = block.replace(expected, '', 1)
                count += 1
        if count != 1:
            raise RuntimeError('Expected exactly one HAL entry in Cargo.lock')
        lock.write_text('[[package]]'.join(blocks))
        original = tomllib.loads((source / 'Cargo.lock').read_text())
        expected_lock = json.loads(json.dumps(original))
        for package in expected_lock['package']:
            if package['name'] == 'wgpu-hal':
                del package['source']
                del package['checksum']
        if tomllib.loads(lock.read_text()) != expected_lock:
            raise RuntimeError('Prepared Cargo.lock changed unrelated dependencies')
        record = {'nativeVersion': VERSION, 'nativeCommit': COMMIT,
                  'headersCommit': HEADERS_COMMIT, 'originalCargoLockSha256': LOCK_SHA,
                  'resolvedCargoLockSha256': sha(lock), 'halVersion': HAL_VERSION,
                  'halCrateSha256': HAL_SHA, 'upstreamFixCommit': UPSTREAM_FIX,
                  'patchSha256': patch_sha, 'files': files(temporary)}
        (temporary / marker.name).write_text(json.dumps(record, indent=2) + '\n')
        temporary.rename(prepared)
        return prepared / 'native', record
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def publish(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() and sha(target) == sha(source):
        return
    temporary = target.with_name(target.name + '.new-' + str(os.getpid()))
    shutil.copyfile(source, temporary)
    temporary.replace(target)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('This macOS dependency builder requires macOS and Xcode')
    if not 1 <= args.jobs <= 8:
        parser.error('--jobs must be between 1 and 8')
    root = Path(__file__).resolve().parents[1]
    patch = root / 'tools' / 'patches' / 'wgpu-hal-29.0.3-macos-first-frame.patch'
    started = time.monotonic()
    native, source_record = prepare(root, patch)
    if args.prepare_only:
        public_source = {key: value for key, value in source_record.items() if key != 'files'}
        print(json.dumps({'prepared': str(native), 'source': public_source}, indent=2))
        return
    prefix = root / 'build-deps' / 'wgpu' / 'macos'
    manifest_path = prefix / 'manifest.json'
    if manifest_path.exists():
        previous = json.loads(manifest_path.read_text())
        for name, entry in previous['installedFiles'].items():
            path = prefix / name
            if not path.is_file() or sha(path) != entry['sha256']:
                raise RuntimeError('Existing installed WebGPU dependency was modified: ' + name)
    build = root / 'build-deps' / 'wgpu-build' / 'macos'
    env = os.environ.copy()
    env['CARGO_TARGET_DIR'] = str(build)
    env['MACOSX_DEPLOYMENT_TARGET'] = '13.0'
    run(['cargo', 'build', '--locked', '--release', '--lib', '--target', 'aarch64-apple-darwin',
         '--jobs', str(args.jobs)], cwd=native, env=env)
    if files(native.parent) != source_record['files']:
        raise RuntimeError('Cargo unexpectedly changed prepared source or dependency resolution')
    products = build / 'aarch64-apple-darwin' / 'release'
    prefix.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='wgpu-install-', dir=prefix.parent))
    try:
        dylib = stage / 'libwgpu_native.dylib'
        shutil.copyfile(products / dylib.name, dylib)
        # A project-local install name lets local app bundles find the exact
        # patched library. Release packaging can embed/rewrite it normally.
        run(['install_name_tool', '-id', str(prefix / 'lib' / dylib.name), str(dylib)])
        run(['codesign', '--force', '--sign', '-', '--timestamp=none', str(dylib)])
        run(['codesign', '--verify', '--strict', str(dylib)])
        inputs = {'lib/libwgpu_native.dylib': dylib,
                  'lib/libwgpu_native.a': products / 'libwgpu_native.a',
                  'include/wgpu.h': native / 'ffi' / 'wgpu.h',
                  'include/webgpu.h': native / 'ffi' / 'webgpu-headers' / 'webgpu.h'}
        for name, path in inputs.items():
            publish(path, prefix / name)
        license_dir = prefix / 'share' / 'licenses' / 'lumen-wgpu'
        for component, directory in [('wgpu-native', native), ('wgpu-hal', native.parent / 'hal')]:
            for name in ('LICENSE', 'LICENSE.MIT', 'LICENSE.APACHE', 'LICENSE-MIT', 'LICENSE-APACHE'):
                path = directory / name
                if path.is_file():
                    publish(path, license_dir / (component + '-' + name))
        installed = {name: {'sha256': sha(prefix / name), 'bytes': (prefix / name).stat().st_size}
                     for name in inputs}
        record = {key: value for key, value in source_record.items() if key != 'files'}
        record.update({'platform': 'macos-arm64', 'defaultFeaturesPreserved': True,
                       'minimumMacOS': '13.0', 'installedFiles': installed,
                       'compiler': output(['rustc', '--version']), 'cargo': output(['cargo', '--version']),
                       'seconds': round(time.monotonic() - started, 2)})
        temporary_manifest = stage / 'manifest.json'
        temporary_manifest.write_text(json.dumps(record, indent=2) + '\n')
        publish(temporary_manifest, manifest_path)
        print(json.dumps({'installed': str(prefix), 'manifest': record}, indent=2))
    finally:
        shutil.rmtree(stage)


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        raise SystemExit(str(exc))
