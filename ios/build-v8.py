#!/usr/bin/env python3
"""Reproduce Lumen's pinned, jitless iOS V8 monolith with bounded parallelism.

Run on a Mac with Xcode: python3 ios/build-v8.py sim --fetch
Then: python3 ios/build-v8.py ios
Sources, tools, object files and manifests stay under build-apple-toolchain/.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

V8_TAG = '15.6.75.16'
PROJECT = Path(__file__).resolve().parents[1]


def run(argv, cwd, env=None):
    subprocess.run([str(a) for a in argv], cwd=cwd, env=env, check=True)


def gn_args(platform):
    return '\n'.join([
        'is_component_build=false', 'is_debug=false', 'symbol_level=0',
        'target_os="ios"', 'target_cpu="arm64"', 'v8_target_cpu="arm64"',
        'target_environment="' + ('simulator' if platform == 'sim' else 'device') + '"',
        'ios_deployment_target="16.0"', 'ios_enable_code_signing=false',
        'use_custom_libcxx=false', 'v8_monolithic=true',
        'v8_use_external_startup_data=false', 'v8_enable_sandbox=false',
        'v8_enable_pointer_compression=true', 'v8_jitless=true',
        'v8_enable_sparkplug=false', 'v8_enable_maglev=false',
        'v8_enable_turbofan=false', 'v8_enable_webassembly=true',
        'v8_enable_drumbrake=true', 'v8_use_metagen_instance_types=false',
        'treat_warnings_as_errors=false', 'use_remoteexec=false', '',
    ])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('platform', choices=['sim', 'ios'])
    p.add_argument('--root', type=Path, default=PROJECT / 'build-apple-toolchain')
    p.add_argument('--jobs', type=int, default=3)
    p.add_argument('--fetch', action='store_true', help='fetch pinned V8 and its declared dependencies')
    args = p.parse_args()
    if args.jobs < 1:
        p.error('--jobs must be positive')
    base = args.root.resolve()
    if not base.is_relative_to(PROJECT):
        p.error('--root must stay within this project')
    base.mkdir(parents=True, exist_ok=True)
    v8 = base / 'v8'
    depot = base / 'depot_tools'
    env = os.environ.copy()
    env['DEPOT_TOOLS_UPDATE'] = '0'
    env['PATH'] = str(depot) + os.pathsep + env.get('PATH', '')
    if args.fetch:
        if not depot.exists():
            run(['git', 'clone', '--depth', '1',
                 'https://chromium.googlesource.com/chromium/tools/depot_tools.git', depot], base)
        if not v8.exists():
            run(['git', 'clone', '--depth', '1', '--branch', V8_TAG,
                 'https://chromium.googlesource.com/v8/v8.git', v8], base)
        config = base / '.gclient'
        if not config.exists():
            config.write_text("solutions=[{'name':'v8','url':'https://chromium.googlesource.com/v8/v8.git',"
                              "'deps_file':'DEPS','managed':False,'custom_deps':{},"
                              "'custom_vars':{'checkout_instrumented_libraries':False}}]\n"
                              "target_os=['ios','mac']\n")
        run([depot / 'gclient', 'sync', '--no-history', '--shallow', '--nohooks',
             '-j' + str(args.jobs)], base, env)
    if not v8.is_dir():
        p.error('V8 sources are missing; run with --fetch first')
    rev = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=v8, text=True).strip()
    pinned = subprocess.check_output(['git', 'rev-parse', V8_TAG + '^{commit}'], cwd=v8, text=True).strip()
    if rev != pinned:
        p.error('V8 checkout does not match the pinned tag ' + V8_TAG)
    patch = PROJECT / 'ios/patches/v8-15.6-drumbrake-scope.patch'
    applied = subprocess.run(['git', 'apply', '--reverse', '--check', str(patch)], cwd=v8,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    if not applied:
        run(['git', 'apply', '--check', patch], v8)
        run(['git', 'apply', patch], v8)
    gn = v8 / 'buildtools/mac/gn'
    ninja = v8 / 'third_party/ninja/ninja'
    if not gn.exists() or not ninja.exists():
        p.error('V8 build tools are missing; run with --fetch first')
    output = v8 / 'out' / ('ios-simulator' if args.platform == 'sim' else 'ios-device')
    output.mkdir(parents=True, exist_ok=True)
    config_text = gn_args(args.platform)
    (output / 'args.gn').write_text(config_text)
    run([gn, 'gen', output], v8, env)
    run([ninja, '-C', output, '-j' + str(args.jobs), 'v8_monolith'], v8, env)
    archive = output / 'obj/libv8_monolith.a'
    sha = hashlib.sha256()
    with archive.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            sha.update(block)
    manifest = {'v8_tag': V8_TAG, 'v8_revision': rev, 'platform': args.platform,
                'jitless': True, 'gn_args': config_text, 'archive_bytes': archive.stat().st_size,
                'archive_sha256': sha.hexdigest()}
    (output / 'lumen-build.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Built', args.platform, 'V8', V8_TAG, archive.stat().st_size // (1024 * 1024), 'MiB')


if __name__ == '__main__':
    main()
