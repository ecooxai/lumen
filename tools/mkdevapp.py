#!/usr/bin/env python3
"""Create a fresh local macOS development bundle without rewriting dependencies.

The bundle uses this Mac's installed build dependencies. For distributable,
self-contained packaging use tools/mkdmg.py instead.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--identifier', default='ai.ecoox.lumen.development')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('A macOS development bundle must be created on macOS')
    root = Path(__file__).resolve().parents[1]
    source = root / 'build/lumen'
    output = args.output.resolve()
    if output.suffix != '.app' or output.exists():
        parser.error('--output must name a new .app directory')
    if not source.is_file():
        parser.error('Build build/lumen before packaging')
    if not args.identifier or not all(c.isalnum() or c in '.-' for c in args.identifier):
        parser.error('Invalid bundle identifier')
    executable = output / 'Contents/MacOS/lumen'
    resources = output / 'Contents/Resources'
    executable.parent.mkdir(parents=True)
    resources.mkdir()
    shutil.copy2(source, executable)
    executable.chmod(0o755)
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
        'LSMinimumSystemVersion': '13.0', 'NSPrincipalClass': 'NSApplication',
        'NSHighResolutionCapable': True,
        'NSCameraUsageDescription': 'Allow websites you approve to use your camera.',
        'NSMicrophoneUsageDescription': 'Allow websites you approve to use your microphone.'
    }
    with (output / 'Contents/Info.plist').open('wb') as file:
        plistlib.dump(info, file)
    before = hashlib.sha256(executable.read_bytes()).hexdigest()
    subprocess.run(['codesign', '--force', '--timestamp=none', '--sign', '-', str(output)],
                   check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    subprocess.run(['codesign', '--verify', '--strict', str(output)], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    print(json.dumps({'app': str(output), 'executable': str(executable),
                      'bundleIdentifier': args.identifier, 'developmentOnly': True,
                      'binaryBeforeBundleSigningSha256': before,
                      'binarySha256': hashlib.sha256(executable.read_bytes()).hexdigest()}))


if __name__ == '__main__':
    main()
