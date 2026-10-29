"""Bounded project-only search for existing IPA/mobile app delivery artifacts."""
from datetime import datetime, timezone
from pathlib import Path
import json
import os
import plistlib
import time


def main():
    root = Path.cwd().resolve()
    if str(root) != '/Users/devin/repos/lumen':
        raise SystemExit('Run from the observed canonical Mac repository')
    started = time.monotonic()
    bases = [root / name for name in ('out', 'output', 'ios', 'dist')]
    bases += [path for path in root.iterdir() if path.is_dir() and path.name.startswith(('build-ios', 'build-sim'))]
    report = {'startedUTC': datetime.now(timezone.utc).isoformat(),
        'scope': 'Known project delivery/build directories, max depth8/25000 entries/8 seconds; metadata only',
        'roots': [str(path.relative_to(root)) for path in bases if path.is_dir()],
        'ipaCandidates': [], 'mobileAppCandidates': [], 'entriesVisited': 0,
        'completeWithinDeclaredScope': True, 'executionOrSigningClaimed': False}
    excluded = {'node_modules', '.git', 'profile', 'profiles', 'launch-profile', 'third_party',
                'v8', 'target', 'chromium', 'chrome-linux64', 'wpt', 'web-platform-tests'}
    for base in bases:
        if not base.is_dir():
            continue
        for directory, directories, files in os.walk(base, followlinks=False):
            path = Path(directory)
            report['entriesVisited'] += len(directories) + len(files)
            if time.monotonic() - started > 8 or report['entriesVisited'] > 25000:
                report['completeWithinDeclaredScope'] = False
                break
            directories[:] = [name for name in directories if name not in excluded
                and not name.endswith('-profile') and not (path / name).is_symlink()]
            if len(path.relative_to(base).parts) >= 8:
                directories[:] = []
            if path.name.endswith('.app'):
                directories[:] = []
                plist = path / 'Info.plist'
                if plist.is_file() and not plist.is_symlink() and plist.stat().st_size <= 131072:
                    try:
                        info = plistlib.loads(plist.read_bytes())
                        if any('lume' in str(info.get(key, '')).lower() for key in
                                ('CFBundleIdentifier', 'CFBundleName', 'CFBundleDisplayName')):
                            name = info.get('CFBundleExecutable')
                            safe_name = isinstance(name, str) and name not in ('', '.', '..') and '/' not in name
                            report['mobileAppCandidates'].append({'path': str(path.relative_to(root)),
                                'bundleIdentifier': info.get('CFBundleIdentifier'),
                                'platforms': info.get('CFBundleSupportedPlatforms'),
                                'deviceFamilies': info.get('UIDeviceFamily'),
                                'executablePresent': safe_name and (path / name).is_file(),
                                'embeddedProvisioningProfilePresent': (path / 'embedded.mobileprovision').is_file()})
                    except (OSError, ValueError, plistlib.InvalidFileException) as error:
                        report['mobileAppCandidates'].append({'path': str(path.relative_to(root)), 'error': type(error).__name__})
            for name in files:
                if name.lower().endswith('.ipa'):
                    candidate = path / name
                    if candidate.is_symlink() or not candidate.resolve().is_relative_to(root):
                        continue
                    stat = candidate.stat()
                    report['ipaCandidates'].append({'path': str(candidate.relative_to(root)),
                        'bytes': stat.st_size, 'modifiedUTC': datetime.fromtimestamp(stat.st_mtime, timezone.utc).isoformat()})
        if not report['completeWithinDeclaredScope']:
            break
    report['finishedUTC'] = datetime.now(timezone.utc).isoformat()
    report['durationSeconds'] = round(time.monotonic() - started, 3)
    output = root / 'out/10300110-apple/existing-ipa-inventory.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x') as stream:
        json.dump(report, stream, indent=2); stream.write('\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
