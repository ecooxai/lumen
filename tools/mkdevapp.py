#!/usr/bin/env python3
"""Create a fresh local macOS development bundle from its actual dependency floor.

The bundle uses this Mac's installed build dependencies. For distributable,
self-contained packaging use tools/mkdmg.py instead.
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--identifier', default='ai.ecoox.lumen.development')
    parser.add_argument('--minimum-system-version',
                        help='Optional conservative minimum; cannot understate the inspected dependency closure')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('A macOS development bundle must be created on macOS')
    root = Path(__file__).resolve().parents[1]
    source = root / 'build/lumen'
    output = args.output.resolve()
    if output.suffix != '.app' or output.exists():
        parser.error('--output must name a new .app directory')
    if not output.is_relative_to(root / 'out'):
        parser.error('Local development bundles must be beneath the project out directory')
    if not source.is_file():
        parser.error('Build build/lumen before packaging')
    if not args.identifier or not all(c.isalnum() or c in '.-' for c in args.identifier):
        parser.error('Invalid bundle identifier')
    closure = inspect_closure(root, source)
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
    source_before = sha256(source)
    shutil.copy2(source, executable)
    executable.chmod(0o755)
    if sha256(executable) != source_before:
        raise RuntimeError('Development executable copy verification failed')
    icon = root / 'assets/icon/Lumen.icns'
    if icon.is_file():
        shutil.copy2(icon, resources / 'Lumen.icns')
    icu = Path('/opt/homebrew/opt/v8/libexec/icudtl.dat')
    if icu.is_file():
        shutil.copy2(icu, resources / 'icudtl.dat')
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
    subprocess.run(['codesign', '--force', '--timestamp=none', '--sign', '-', str(output)],
                   check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    subprocess.run(['codesign', '--verify', '--strict', str(output)], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
    signed_closure = inspect_closure(root, executable)
    if version_tuple(signed_closure['maximumDeclaredMinimum']) > version_tuple(minimum):
        raise RuntimeError('The signed bundle dependency floor exceeds its declared minimum')
    if sha256(source) != source_before or any(sha256(Path(entry['path'])) != entry['sha256']
                                            for entry in closure['files']):
        raise RuntimeError('A source executable or dependency changed during packaging')
    print(json.dumps({'app': str(output), 'executable': str(executable),
                      'bundleIdentifier': args.identifier, 'developmentOnly': True,
                      'binaryBeforeBundleSigningSha256': source_before,
                      'binarySha256': sha256(executable), 'minimumSystemVersion': minimum,
                      'deploymentClosure': signed_closure,
                      'minimumDerivedFromActualDependencyClosure': args.minimum_system_version is None,
                      'olderMacOSRuntimeTested': False}))


if __name__ == '__main__':
    main()
