"""Bounded read-only mobile file inventory after a CoreSimulator RPC stalls."""
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json
import plistlib
import sys

root = Path.cwd().resolve()
if sys.platform != 'darwin' or str(root) != '/Users/devin/repos/lumen':
    raise SystemExit('Run from the canonical Mac Lumen repository')
udid = '3DD498EB-4464-4ED7-9CD3-7667889DD1F6'  # Observed booted target, not inferred.
device = Path.home() / 'Library/Developer/CoreSimulator/Devices' / udid / 'data'
result = {'startedUTC': datetime.now(timezone.utc).isoformat(), 'deviceUDID': udid,
          'scope': 'Filesystem inventory only; no simulator RPC, app execution, signing or toolchain modification',
          'targetBundleIdentifier': 'com.ecooxai.lumen', 'installedAppCandidates': [], 'buildAppCandidates': [],
          'dependencies': [], 'mobileBuildClosureVerified': False}


def app_row(plist):
    try:
        info = plistlib.loads(plist.read_bytes())
        if info.get('CFBundleIdentifier') != 'com.ecooxai.lumen':
            return None
        executable_name = info.get('CFBundleExecutable', '')
        if not isinstance(executable_name, str) or '/' in executable_name or executable_name in ('', '.', '..'):
            return {'app': str(plist.parent), 'error': 'Invalid executable basename'}
        executable = plist.parent / executable_name
        row = {'app': str(plist.parent), 'platforms': info.get('CFBundleSupportedPlatforms'),
               'executablePresent': executable.is_file()}
        if executable.is_file():
            row['bytes'] = executable.stat().st_size
            if row['bytes'] <= 128 * 1024 * 1024:
                with executable.open('rb') as stream:
                    row['sha256'] = hashlib.file_digest(stream, 'sha256').hexdigest()
        return row
    except (OSError, ValueError, plistlib.InvalidFileException) as error:
        return {'app': str(plist.parent), 'error': type(error).__name__}


for base in [device / 'Containers/Bundle/Application', device / 'Applications']:
    for plist in sorted(base.glob('*/Lumen.app/Info.plist'))[:12]:
        row = app_row(plist)
        if row:
            result['installedAppCandidates'].append(row)
for pattern in ['build-sim*/Lumen.app/Info.plist', 'build-ios*/Lumen.app/Info.plist']:
    for plist in sorted(root.glob(pattern))[:12]:
        row = app_row(plist)
        if row:
            result['buildAppCandidates'].append(row)
libraries = ['ssl', 'crypto', 'z', 'brotlidec', 'brotlicommon', 'zstd', 'freetype', 'harfbuzz', 'SDL3',
             'wgpu_native', 'avcodec', 'avformat', 'avutil', 'swscale', 'swresample', 'png', 'jpeg',
             'webp', 'webpdemux', 'sharpyuv', 'gif', 'dav1d']
for base in [root / 'build-apple-toolchain', Path.home() / 'ios-deps']:
    for target in ['ios', 'sim']:
        prefix = base / target
        files = [prefix / 'include/png.h'] + [prefix / 'lib' / ('lib' + name + '.a') for name in libraries]
        result['dependencies'].append({'prefix': str(prefix), 'platform': target,
            'present': [str(path.relative_to(prefix)) for path in files if path.is_file() and path.stat().st_size],
            'missing': [str(path.relative_to(prefix)) for path in files if not path.is_file() or not path.stat().st_size]})
for base in [root / 'build-apple-toolchain/v8', Path.home() / 'src/v8ios/v8']:
    for target in ['ios-device', 'ios-simulator']:
        for suffix in ['', '-jit']:
            prefix = base / 'out' / (target + suffix)
            files = [prefix / 'obj/libv8_monolith.a', prefix / 'obj/v8_monolith.ninja', prefix / 'icudtl.dat']
            result['dependencies'].append({'prefix': str(prefix), 'platform': target + suffix,
                'present': [str(path.relative_to(prefix)) for path in files if path.is_file() and path.stat().st_size],
                'missing': [str(path.relative_to(prefix)) for path in files if not path.is_file() or not path.stat().st_size]})
result['finishedUTC'] = datetime.now(timezone.utc).isoformat()
output = root / 'out/10300110-apple/mobile-files.json'
output.parent.mkdir(parents=True, exist_ok=True)
if output.exists():
    raise SystemExit('Preserve the existing inventory receipt; select a new task path for a new observation')
output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
