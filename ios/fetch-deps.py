#!/usr/bin/env python3
"""Fetch pinned sources for build-deps.sh; build products stay out of Git.

Archives come from upstream HTTPS release endpoints. The generated manifest
records SHA-256 hashes for subsequent offline verification and troubleshooting.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import tarfile
import time
import urllib.request
import zipfile

SOURCES = {
    'openssl-3.5.4': 'https://github.com/openssl/openssl/releases/download/openssl-3.5.4/openssl-3.5.4.tar.gz',
    'brotli-1.1.0': 'https://github.com/google/brotli/archive/refs/tags/v1.1.0.tar.gz',
    'zstd-1.5.7': 'https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz',
    'libpng-1.6.50': 'https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.50.tar.gz',
    'libjpeg-turbo-3.1.1': 'https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.1.1/libjpeg-turbo-3.1.1.tar.gz',
    'libwebp-1.6.0': 'https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz',
    'giflib-5.2.2': 'https://downloads.sourceforge.net/project/giflib/giflib-5.x/giflib-5.2.2.tar.gz',
    'freetype-2.13.3': 'https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz',
    'harfbuzz-11.2.1': 'https://github.com/harfbuzz/harfbuzz/releases/download/11.2.1/harfbuzz-11.2.1.tar.xz',
    'dav1d-1.5.1': 'https://downloads.videolan.org/pub/videolan/dav1d/1.5.1/dav1d-1.5.1.tar.xz',
    'ffmpeg-9.0.2': 'https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz',
    'SDL3-3.4.16': 'https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz',
}
WGPU = {name: 'https://github.com/gfx-rs/wgpu-native/releases/download/v29.0.1.1/' + name + '.zip'
        for name in ('wgpu-ios-aarch64-simulator-release', 'wgpu-ios-aarch64-release')}


def safe_extract(archive, destination):
    destination.mkdir(parents=True, exist_ok=True)
    root = destination.resolve()
    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as source:
            for item in source.infolist():
                if not (root / item.filename).resolve().is_relative_to(root):
                    raise ValueError('unsafe archive path: ' + item.filename)
            source.extractall(root)
    else:
        with tarfile.open(archive) as source:
            for item in source.getmembers():
                path = (root / item.name).resolve()
                if not path.is_relative_to(root):
                    raise ValueError('unsafe archive path: ' + item.name)
                if item.issym() and not (path.parent / item.linkname).resolve().is_relative_to(root):
                    raise ValueError('unsafe archive symlink: ' + item.name)
                if item.islnk() and not (root / item.linkname).resolve().is_relative_to(root):
                    raise ValueError('unsafe archive hardlink: ' + item.name)
                if not (item.isfile() or item.isdir() or item.issym() or item.islnk()):
                    raise ValueError('unsupported archive member: ' + item.name)
            source.extractall(root)


def fetch(item, base, retries):
    name, url, kind = item
    archives = base / 'archives'
    archives.mkdir(parents=True, exist_ok=True)
    archive = archives / (name + ('.zip' if kind == 'wgpu' else '.tar'))
    partial = archive.with_suffix(archive.suffix + '.part')
    if not archive.exists():
        for attempt in range(retries):
            try:
                with urllib.request.urlopen(url, timeout=60) as response, partial.open('wb') as output:
                    while True:
                        chunk = response.read(1024 * 1024)
                        if not chunk:
                            break
                        output.write(chunk)
                partial.replace(archive)
                break
            except Exception:
                partial.unlink(missing_ok=True)
                if attempt + 1 == retries:
                    raise
                time.sleep(20)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    destination = base / ('wgpu' if kind == 'wgpu' else 'deps')
    expected = destination / name
    marker = base / 'manifests' / (name + '.json')
    prior = json.loads(marker.read_text()) if marker.exists() else {}
    if prior and prior.get('sha256') != digest:
        raise ValueError('cached archive hash changed: ' + name)
    if not expected.exists():
        safe_extract(archive, expected if kind == 'wgpu' else destination)
    if not expected.is_dir():
        raise ValueError('archive did not contain expected source directory: ' + name)
    result = {'name': name, 'url': url, 'sha256': digest, 'bytes': archive.stat().st_size,
              'directory': str(expected)}
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.write_text(json.dumps(result, indent=2) + '\n')
    print(name + ': ready', flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, default=Path('build-apple-toolchain'))
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--retries', type=int, default=3)
    parser.add_argument('packages', nargs='*')
    args = parser.parse_args()
    if not 1 <= args.jobs <= 8 or args.retries < 1:
        parser.error('jobs must be 1..8 and retries must be positive')
    requested = set(args.packages)
    known = set(SOURCES) | set(WGPU)
    if requested - known:
        parser.error('unknown packages: ' + ', '.join(sorted(requested - known)))
    items = [(name, url, kind) for kind, group in [('source', SOURCES), ('wgpu', WGPU)]
             for name, url in group.items() if not requested or name in requested]
    failures = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [(item[0], pool.submit(fetch, item, args.directory, args.retries)) for item in items]
        for name, future in futures:
            try:
                future.result()
            except Exception as exc:
                failures.append({'name': name, 'error': str(exc)})
    print(json.dumps({'requested': len(items), 'failed': failures}))
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
