#!/usr/bin/env python3
"""Build the pinned inactive transport package; no browser or network gate is run."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def macos_deployment_target(target, requested=None):
    """Keep the desktop target independent of the installed Xcode SDK version."""
    if target != 'aarch64-apple-darwin':
        if requested is not None:
            raise ValueError('--macos-deployment-target is only valid for the macOS target.')
        return None
    # The inspected desktop application, RTC and Homebrew closure require 26.0.
    # An explicit option permits a separately audited dependency closure later.
    value = '26.0' if requested is None else requested
    if not isinstance(value, str) or not re.fullmatch(r'[1-9][0-9]*\.(?:0|[1-9][0-9]*)(?:\.(?:0|[1-9][0-9]*))?', value):
        raise ValueError('The macOS deployment target must be a numeric major.minor[.patch] version.')
    parts = tuple(int(part) for part in value.split('.'))
    if not 11 <= parts[0] <= 65535 or any(part > 255 for part in parts[1:]):
        raise ValueError('The arm64 macOS deployment target is outside the Mach-O version bounds.')
    return value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--target', default='aarch64-apple-darwin')
    parser.add_argument('--profile', choices=['debug', 'release'], default='debug')
    parser.add_argument('--macos-deployment-target', help='Explicit macOS minimum; the project default is 26.0, independent of the Xcode SDK and ambient environment.')
    args = parser.parse_args()
    platforms = {'aarch64-apple-darwin': 'macos', 'aarch64-apple-ios-sim': 'sim', 'aarch64-apple-ios': 'ios'}
    if args.target not in platforms:
        raise SystemExit('Unsupported explicit Apple target.')
    try:
        minimum_macos = macos_deployment_target(args.target, args.macos_deployment_target)
    except ValueError as error:
        parser.error(str(error))
    crate = Path(__file__).resolve().parent
    repo = crate.parents[2]
    if crate != repo / 'src/net/webtransport-native':
        raise SystemExit('Run only from the project-owned transport source directory.')
    minimum_free_bytes = 4 * 1024 * 1024 * 1024
    if shutil.disk_usage(repo).free < minimum_free_bytes:
        raise SystemExit('Build preflight refused: at least 4GiB free disk is required.')
    env = os.environ.copy()
    env.update({'CARGO_BUILD_JOBS': '2', 'CARGO_TARGET_DIR': str(repo / 'build-deps/webtransport/target'),
        'CARGO_INCREMENTAL': '0', 'CARGO_PROFILE_DEV_DEBUG': '0', 'CARGO_PROFILE_TEST_DEBUG': '0'})
    if minimum_macos is not None:
        # cc's default otherwise follows the SDK version, unlike rustc/clang.
        env['MACOSX_DEPLOYMENT_TARGET'] = minimum_macos
    prefix = repo / 'build-deps/webtransport' / platforms[args.target]
    command_base = ['--locked', '--offline', '--target', args.target, '--jobs', '2']
    if args.profile == 'release': command_base += ['--release']
    if args.target == 'aarch64-apple-darwin':
        subprocess.run(['cargo', 'test', *command_base, '--lib'], cwd=crate, env=env, check=True)
        subprocess.run(['cargo', 'build', *command_base, '--lib', '--example', 'ffi_gate'], cwd=crate, env=env, check=True)
    else:
        subprocess.run(['cargo', 'build', *command_base, '--lib'], cwd=crate, env=env, check=True)
    output = subprocess.run(['cargo', 'rustc', *command_base, '--lib', '--', '--print', 'native-static-libs'], cwd=crate, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=True)
    sys.stdout.write(output.stdout)
    matches = re.findall(r'native-static-libs:\s*([^\n]+)', output.stdout)
    if not matches: raise SystemExit('Rust did not report native linker requirements.')
    native_args = shlex.split(matches[-1])
    if not native_args: raise SystemExit('Empty native link requirements require review.')
    archive = Path(env['CARGO_TARGET_DIR']) / args.target / args.profile / 'liblumen_webtransport_native.a'
    if not archive.is_file(): raise SystemExit('Expected static archive is absent.')
    (prefix / 'lib').mkdir(parents=True, exist_ok=True)
    (prefix / 'include').mkdir(exist_ok=True)
    shutil.copy2(archive, prefix / 'lib' / archive.name)
    shutil.copy2(crate / 'include/lumen_wt.h', prefix / 'include/lumen_wt.h')
    sources = {str(path.relative_to(crate)): sha(path) for path in [crate/'Cargo.toml', crate/'Cargo.lock', crate/'src/lib.rs', crate/'include/lumen_wt.h', crate/'examples/ffi_gate.rs']}
    source_hash = hashlib.sha256(json.dumps(sources, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    metadata = json.loads(subprocess.check_output(['cargo', 'metadata', '--locked', '--offline', '--format-version', '1'], cwd=crate, env=env, text=True))
    dependency_licenses = []
    for package in sorted(metadata['packages'], key=lambda item: (item['name'], item['version'])):
        if package.get('source') is None: continue
        dependency_licenses.append({'name': package['name'], 'version': package['version'], 'license': package.get('license'), 'source': package.get('source')})
    provenance = {'schemaVersion': 1, 'cargoLockSHA256': sha(crate/'Cargo.lock'), 'packages': dependency_licenses, 'dangerousCertificateVerifierEnabled': False}
    (prefix / 'dependency-provenance.json').write_text(json.dumps(provenance, indent=2)+'\n')
    manifest = {'schemaVersion': 1, 'library': 'lumen-webtransport-native', 'bridgeVersion': '0.1.0', 'version': '0.7.2', 'backend': 'wtransport-0.7.2', 'target': args.target, 'profile': args.profile,
        'archiveSHA256': sha(prefix/'lib'/archive.name), 'headerSHA256': sha(prefix/'include/lumen_wt.h'), 'cargoLockSHA256': sha(crate/'Cargo.lock'), 'sourceSHA256': source_hash, 'sourceFiles': sources, 'nativeLinkArgs': native_args,
        'jobs': 2, 'runtimeWorkerThreads': 2, 'runtimeBlockingThreadLimit': 2, 'minimumFreeDiskBytes': minimum_free_bytes, 'incrementalCompilation': False, 'debugInformation': False,
        'compiler': subprocess.check_output(['rustc','--version'], text=True).strip(),
        'browserConstructorExposed': False, 'realNetworkGateRequired': True, 'provenanceSHA256': sha(prefix/'dependency-provenance.json')}
    if minimum_macos is not None:
        manifest['deploymentTarget'] = {'platform': 'macos', 'minimumVersion': minimum_macos,
            'environmentVariable': 'MACOSX_DEPLOYMENT_TARGET',
            'selection': 'explicit-option' if args.macos_deployment_target is not None else 'project-default'}
    (prefix/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps(manifest))


if __name__ == '__main__':
    main()
