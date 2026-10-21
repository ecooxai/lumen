#!/usr/bin/env python3
"""Build the pinned, data-channel-only native RTC dependency inside the project."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

VERSION = 'v0.24.6'
COMMIT = '6b1e2e620f1e37f0eafeee702eaea0043cb305fd'
SUBMODULES = {
    'deps/json': '55f93686c01528224f448c19128836e7df245f72',
    'deps/libjuice': 'b89c792e3612faf2f12cf35bcc56857313a06be3',
    'deps/libsrtp': '24b3bf8f19b6f5ab4cd2bcceb4f4064efca86fd5',
    'deps/plog': '94899e0b926ac1b0f4750bfbd495167b4a6ae9ef',
    'deps/usrsctp': 'fec583d54493f879d2ae44a743423bf8a04371ab',
}


def run(argv: list[str], **kwargs) -> None:
    print('+', ' '.join(argv), flush=True)
    subprocess.run(argv, check=True, **kwargs)


def output(argv: list[str], **kwargs) -> str:
    return subprocess.check_output(argv, text=True, **kwargs).strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=('macos', 'sim', 'ios'), default='macos')
    parser.add_argument('--jobs', type=int, default=3)
    args = parser.parse_args()
    if not 1 <= args.jobs <= 16:
        parser.error('--jobs must be between 1 and 16')
    if sys.platform != 'darwin':
        parser.error('This Apple dependency builder requires macOS and Xcode')
    root = Path(__file__).resolve().parents[1]
    source = root / 'build-deps' / 'sources' / ('libdatachannel-' + VERSION)
    build = root / 'build-deps' / 'rtc-build' / args.platform
    prefix = root / 'build-deps' / 'rtc' / args.platform
    source.parent.mkdir(parents=True, exist_ok=True)
    if not source.exists():
        run(['git', 'clone', '--branch', VERSION, '--depth', '1', '--recurse-submodules',
             '--shallow-submodules', 'https://github.com/paullouisageneau/libdatachannel.git', str(source)])
    if output(['git', 'rev-parse', 'HEAD'], cwd=source) != COMMIT:
        raise RuntimeError('Existing RTC source does not match the pinned commit; it was not modified')
    if output(['git', 'status', '--porcelain'], cwd=source):
        raise RuntimeError('Existing RTC source is dirty; it was not modified')
    for name, commit in SUBMODULES.items():
        if output(['git', 'rev-parse', 'HEAD'], cwd=source / name) != commit:
            raise RuntimeError('RTC submodule does not match pinned commit: ' + name)
    flags = [
        '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_SHARED_LIBS=OFF',
        '-DBUILD_SHARED_DEPS_LIBS=OFF', '-DPREFER_SYSTEM_LIB=OFF',
        '-DUSE_NICE=OFF', '-DUSE_GNUTLS=OFF', '-DUSE_MBEDTLS=OFF',
        '-DNO_MEDIA=ON', '-DNO_WEBSOCKET=ON', '-DNO_EXAMPLES=ON', '-DNO_TESTS=ON',
        '-DRTC_UPDATE_VERSION_HEADER=OFF', '-DCMAKE_POSITION_INDEPENDENT_CODE=ON',
        '-DCMAKE_INSTALL_PREFIX=' + str(prefix),
    ]
    if args.platform == 'macos':
        openssl = Path(output(['brew', '--prefix', 'openssl@3']))
        flags += ['-DOPENSSL_ROOT_DIR=' + str(openssl)]
    else:
        sdkname = 'iphonesimulator' if args.platform == 'sim' else 'iphoneos'
        sdk = output(['xcrun', '--sdk', sdkname, '--show-sdk-path'])
        default_deps = root / 'build-apple-toolchain' / args.platform
        if not (default_deps / 'include' / 'openssl' / 'ssl.h').exists():
            default_deps = Path.home() / 'ios-deps' / args.platform
        deps = Path(os.environ.get('IOSDEPS', str(default_deps))).resolve()
        for name in ('include/openssl/ssl.h', 'lib/libssl.a', 'lib/libcrypto.a'):
            if not (deps / name).is_file():
                raise RuntimeError('Missing target OpenSSL dependency: ' + str(deps / name))
        flags += [
            '-DCMAKE_SYSTEM_NAME=iOS', '-DCMAKE_OSX_ARCHITECTURES=arm64',
            '-DCMAKE_OSX_SYSROOT=' + sdk, '-DCMAKE_OSX_DEPLOYMENT_TARGET=16.0',
            '-DOPENSSL_ROOT_DIR=' + str(deps), '-DOPENSSL_INCLUDE_DIR=' + str(deps / 'include'),
            '-DOPENSSL_SSL_LIBRARY=' + str(deps / 'lib/libssl.a'),
            '-DOPENSSL_CRYPTO_LIBRARY=' + str(deps / 'lib/libcrypto.a'),
        ]
    run(['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja', *flags])
    run(['cmake', '--build', str(build), '--parallel', str(args.jobs)])
    run(['cmake', '--install', str(build)])
    archives = {}
    for name in ('libdatachannel.a', 'libjuice.a', 'libusrsctp.a'):
        path = prefix / 'lib' / name
        if not path.is_file():
            raise RuntimeError('Dependency build did not install ' + name)
        archives[name] = {'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    licenses = prefix / 'share' / 'licenses' / 'lumen-rtc'
    licenses.mkdir(parents=True, exist_ok=True)
    for name, directory in [('libdatachannel', source), ('libjuice', source / 'deps/libjuice'),
                            ('usrsctp', source / 'deps/usrsctp'), ('plog', source / 'deps/plog')]:
        for candidate in ('LICENSE', 'LICENSE.txt', 'COPYING', 'COPYING.md'):
            path = directory / candidate
            if path.is_file():
                shutil.copyfile(path, licenses / (name + '-' + candidate))
                break
    manifest = {'version': VERSION, 'commit': COMMIT, 'submodules': SUBMODULES,
                'platform': args.platform, 'mediaTransport': False, 'websocketBackend': False,
                'archives': archives, 'buildFlags': flags}
    (prefix / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'installed': str(prefix), 'platform': args.platform, 'archives': archives}))


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        raise SystemExit(str(exc))
