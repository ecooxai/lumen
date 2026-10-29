#!/usr/bin/env python3
"""Inspect only Lume bundle metadata after a timed-out simulator app listing."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import os
import platform
import plistlib
import subprocess
import time

ROOT = Path('/Users/devin/repos/lumen').resolve()
if platform.system() != 'Darwin' or Path.cwd().resolve() != ROOT:
    raise SystemExit('Run only from verified Mac repository')
OUT = ROOT / 'out/10292019r-apple/simulator-bundles'
OUT.mkdir(parents=True, exist_ok=True)
DEADLINE = time.monotonic() + 60
ENV = dict(os.environ, DEVELOPER_DIR='/Library/Developer/CommandLineTools')
OTOOL = '/Library/Developer/CommandLineTools/usr/bin/otool'
DEVICE = '3DD498EB-4464-4ED7-9CD3-7667889DD1F6'


def run(argv):
    timeout = min(8, DEADLINE - time.monotonic())
    if timeout <= 0:
        return {'argv': argv, 'unrun': 'time budget exhausted'}
    try:
        p = subprocess.run(argv, env=ENV, capture_output=True, text=True, timeout=timeout)
        return {'argv': argv, 'return_code': p.returncode, 'stdout': p.stdout[:12000], 'stderr': p.stderr[:1500]}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {'argv': argv, 'error': type(error).__name__ + ': ' + str(error)[:1000]}


def inspect(app, info=None):
    row = {'app': str(app), 'scope': 'Bundle bytes only, not current source or successful execution'}
    try:
        plist = app / 'Info.plist'
        if not plist.exists(): plist = app / 'Contents/Info.plist'
        info = info or plistlib.loads(plist.read_bytes())
        row['info'] = {key: info.get(key) for key in ['CFBundleIdentifier', 'CFBundleExecutable', 'CFBundleName',
            'CFBundleVersion', 'CFBundleSupportedPlatforms', 'UIDeviceFamily', 'MinimumOSVersion', 'LSMinimumSystemVersion']}
        name = info['CFBundleExecutable']
        binary = app / 'Contents/MacOS' / name if plist.parent.name == 'Contents' else app / name
        row['binary'] = str(binary)
        with binary.open('rb') as source:
            row['sha256'] = hashlib.file_digest(source, 'sha256').hexdigest()
        row['file'] = run(['/usr/bin/file', str(binary)])
        commands = run([OTOOL, '-l', str(binary)])
        lines = commands.pop('stdout', '').splitlines()
        commands['platform_commands'] = [part for n, line in enumerate(lines)
            if 'LC_BUILD_VERSION' in line or 'LC_VERSION_MIN_' in line for part in lines[n:n+7]]
        row['platform'] = commands
        row['dependencies'] = run([OTOOL, '-L', str(binary)])
        row['signature'] = run(['/usr/bin/codesign', '--verify', '--strict', str(app)])
    except (OSError, ValueError, KeyError, TypeError) as error:
        row['error'] = type(error).__name__ + ': ' + str(error)[:1000]
    return row


data = Path.home() / 'Library/Developer/CoreSimulator/Devices' / DEVICE / 'data'
folders = [data / 'Containers/Bundle/Application', data / 'Applications']
result = {'started_utc': datetime.now(timezone.utc).isoformat(), 'device': DEVICE,
          'prior_api_listing': 'Timed out after15s; empty output was not treated as no installed app',
          'folders': [], 'lume_bundles': [], 'native_mac_control': []}
for folder in folders:
    row = {'path': str(folder), 'exists': folder.is_dir(), 'app_count': 0}
    try:
        candidates = list(folder.glob('*/*.app')) + list(folder.glob('*.app'))
        row['app_count'] = len(candidates)
        for app in candidates[:100]:
            info = plistlib.loads((app / 'Info.plist').read_bytes())
            if 'lume' in app.name.lower() or any('lume' in str(info.get(key, '')).lower() for key in
                       ['CFBundleIdentifier', 'CFBundleName', 'CFBundleDisplayName']):
                result['lume_bundles'].append(inspect(app, info))
    except (OSError, ValueError) as error:
        row['error'] = type(error).__name__ + ': ' + str(error)[:700]
    result['folders'].append(row)
control = ROOT / 'out/GPT6-Astra-Pro-browser-engine-devinmac3d-Lumen-37447be5593f.app'
if control.is_dir(): result['native_mac_control'].append(inspect(control))
result['finished_utc'] = datetime.now(timezone.utc).isoformat()
(OUT / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
