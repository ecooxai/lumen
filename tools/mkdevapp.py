#!/usr/bin/env python3
"""Create a fresh local macOS development bundle from its actual dependency floor.

By default the bundle uses this Mac's installed build dependencies. Opt into
--bundle-dependencies to copy and relocate the inspected prebuilt closure. Both
modes create an ad-hoc development app; neither performs notarization.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys

from macos_deployment_closure import inspect_closure, sha256, version_text, version_tuple
from macos_bundle_dependencies import embed_dependencies, require_embedded_closure


def portable_resources(closure, ca_bundle=None):
    candidates = {Path(row['path']).parent / 'icudtl.dat' for row in closure['files']
                  if Path(row['path']).name.startswith('libv8')}
    candidates = {path.resolve() for path in candidates if path.is_file() and path.stat().st_size}
    if len(candidates) != 1:
        raise ValueError('The inspected prebuilt V8 closure must identify exactly one nonempty ICU data file')
    if ca_bundle is None:
        ca_bundle = next((path for path in [Path('/opt/homebrew/etc/ca-certificates/cert.pem'),
                       Path('/opt/homebrew/etc/openssl@3/cert.pem'), Path('/etc/ssl/cert.pem')]
                          if path.is_file() and path.stat().st_size), None)
    if ca_bundle is None or not Path(ca_bundle).is_file():
        raise ValueError('A trusted CA bundle is required for the self-contained development app')
    ca_bundle = Path(ca_bundle).resolve()
    if ca_bundle.stat().st_size > 4 * 1024 * 1024:
        raise ValueError('The selected CA bundle exceeds the bounded certificate resource size')
    content = ca_bundle.read_bytes()
    if b'-----BEGIN CERTIFICATE-----' not in content or b'PRIVATE KEY-----' in content:
        raise ValueError('The selected CA resource must contain certificates and no private keys')
    return {'icudtl.dat': candidates.pop(), 'cacert.pem': ca_bundle}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--binary', type=Path, default=Path('build/lumen'),
                        help='Tested executable inside this project; defaults to build/lumen')
    parser.add_argument('--expected-binary-sha256',
                        help='Require the selected executable to match its recorded test artifact SHA-256')
    parser.add_argument('--identifier', default='ai.ecoox.lumen.development')
    parser.add_argument('--bundle-dependencies', action='store_true',
                        help='Embed the inspected dylibs, matching V8 ICU data and a CA bundle; ad-hoc development app only')
    parser.add_argument('--ca-bundle', type=Path,
                        help='Explicit CA certificate resource for --bundle-dependencies; defaults to the installed trusted CA bundle')
    parser.add_argument('--minimum-system-version',
                        help='Optional conservative minimum; cannot understate the inspected dependency closure')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('A macOS development bundle must be created on macOS')
    root = Path(__file__).resolve().parents[1]
    source = (args.binary if args.binary.is_absolute() else root / args.binary).resolve()
    if not source.is_relative_to(root):
        parser.error('--binary must resolve to a file inside this project')
    output = args.output.resolve()
    if output.suffix != '.app' or output.exists():
        parser.error('--output must name a new .app directory')
    if not output.is_relative_to(root / 'out'):
        parser.error('Local development bundles must be beneath the project out directory')
    if not source.is_file():
        parser.error('Build the selected --binary executable before packaging')
    if not args.identifier or not all(c.isalnum() or c in '.-' for c in args.identifier):
        parser.error('Invalid bundle identifier')
    expected = args.expected_binary_sha256
    if expected is not None:
        if len(expected) != 64 or any(c not in '0123456789abcdefABCDEF' for c in expected):
            parser.error('--expected-binary-sha256 must contain exactly 64 hexadecimal characters')
        expected = expected.lower()
    source_before = sha256(source)
    if expected is not None and source_before != expected:
        parser.error('The selected executable does not match --expected-binary-sha256')
    closure = inspect_closure(root, source)
    if args.ca_bundle is not None and not args.bundle_dependencies:
        parser.error('--ca-bundle requires --bundle-dependencies')
    selected_resources = portable_resources(closure, args.ca_bundle) if args.bundle_dependencies else {}
    minimum = closure['maximumDeclaredMinimum']
    if args.minimum_system_version is not None:
        try:
            requested = version_text(args.minimum_system_version)
        except ValueError as error:
            parser.error(str(error))
        if version_tuple(requested) < version_tuple(minimum):
            parser.error('The requested bundle minimum is below an actual dependency declaration')
        minimum = requested
    executable = output / 'Contents/MacOS/lumen'
    resources = output / 'Contents/Resources'
    executable.parent.mkdir(parents=True)
    resources.mkdir()
    shutil.copy2(source, executable)
    executable.chmod(0o755)
    if sha256(executable) != source_before:
        raise RuntimeError('Development executable copy verification failed')
    icon = root / 'assets/icon/Lumen.icns'
    if icon.is_file():
        shutil.copy2(icon, resources / 'Lumen.icns')
    icu = Path('/opt/homebrew/opt/v8/libexec/icudtl.dat')
    if not args.bundle_dependencies and icu.is_file():
        selected_resources['icudtl.dat'] = icu.resolve()
    resource_receipts = []
    for name, path in selected_resources.items():
        digest = sha256(path)
        target = resources / name
        shutil.copy2(path, target)
        if sha256(target) != digest or sha256(path) != digest:
            raise RuntimeError('A runtime resource changed during bundle copy: ' + name)
        resource_receipts.append({'name': name, 'source': str(path), 'sha256': digest, 'bytes': target.stat().st_size})
    info = {
        'CFBundleName': 'Lumen', 'CFBundleDisplayName': 'Lumen',
        'CFBundleIdentifier': args.identifier, 'CFBundleExecutable': 'lumen',
        'CFBundlePackageType': 'APPL', 'CFBundleVersion': '1',
        'CFBundleShortVersionString': '0.1.0', 'CFBundleIconFile': 'Lumen',
        'LSMinimumSystemVersion': minimum, 'NSPrincipalClass': 'NSApplication',
        'NSHighResolutionCapable': True,
        'NSCameraUsageDescription': 'Allow websites you approve to use your camera.',
        'NSMicrophoneUsageDescription': 'Allow websites you approve to use your microphone.'
    }
    with (output / 'Contents/Info.plist').open('wb') as file:
        plistlib.dump(info, file)
    embedded = embed_dependencies(root, closure, output) if args.bundle_dependencies else None
    subprocess.run(['codesign', '--force', '--timestamp=none', '--sign', '-', str(output)],
                   check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    subprocess.run(['codesign', '--verify', '--strict', *(['--deep'] if embedded else []), str(output)], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
    signed_closure = inspect_closure(root, executable)
    if embedded:
        require_embedded_closure(signed_closure, output)
        for row in embedded['images']:
            row['bundledSha256'] = sha256(Path(row['target']))
        embedded['deploymentClosure'] = signed_closure
    if version_tuple(signed_closure['maximumDeclaredMinimum']) > version_tuple(minimum):
        raise RuntimeError('The signed bundle dependency floor exceeds its declared minimum')
    if sha256(source) != source_before or any(sha256(Path(entry['path'])) != entry['sha256']
                                            for entry in closure['files']):
        raise RuntimeError('A source executable or dependency changed during packaging')
    for resource in resource_receipts:
        if (sha256(Path(resource['source'])) != resource['sha256']
                or sha256(resources / resource['name']) != resource['sha256']):
            raise RuntimeError('A runtime resource changed during packaging')
    print(json.dumps({'app': str(output), 'executable': str(executable),
                      'bundleIdentifier': args.identifier, 'developmentOnly': True,
                      'sourceExecutable': str(source),
                      'expectedBinarySha256Verified': expected is not None,
                      'binaryBeforeBundleSigningSha256': source_before,
                      'binarySha256': sha256(executable), 'minimumSystemVersion': minimum,
                      'deploymentClosure': signed_closure,
                      'embeddedDependencies': embedded,
                      'runtimeResources': resource_receipts,
                      'notarized': False,
                      'minimumDerivedFromActualDependencyClosure': args.minimum_system_version is None,
                      'olderMacOSRuntimeTested': False}))


if __name__ == '__main__':
    main()
