#!/usr/bin/env python3
"""Inspect real installed Lume apps and bounded project Apple artifacts.

Uses the verified native CoreSimulator simctl, not xcrun discovery. No app is
installed, launched, terminated, resigned, or altered by this inventory.
"""
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
    raise SystemExit('Run only from verified Mac workspace')
OUT = ROOT / 'out/10292019r-apple/mobile-artifacts'
OUT.mkdir(parents=True, exist_ok=True)
DEADLINE = time.monotonic() + 90
SIMCTL = '/Library/Developer/PrivateFrameworks/CoreSimulator.framework/Versions/A/Resources/bin/simctl'
DEVICE = '3DD498EB-4464-4ED7-9CD3-7667889DD1F6'
environment = dict(os.environ, DEVELOPER_DIR='/Applications/Xcode-27.0-RC.app/Contents/Developer')


def run(argv, data=None):
    timeout = min(15, DEADLINE - time.monotonic())
    if timeout <= 0:
        return {'argv': argv, 'unrun': 'inventory budget exhausted'}
    try:
        p = subprocess.run(argv, input=data, text=True, capture_output=True, env=environment, timeout=timeout)
        return {'argv': argv, 'return_code': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr[:2000]}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {'argv': argv, 'error': type(error).__name__ + ': ' + str(error)[:1000]}


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for part in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(part)
    return h.hexdigest()


def app_info(path):
    row = {'path': str(path)}
    info_path = path / 'Info.plist'
    if not info_path.is_file():
        info_path = path / 'Contents/Info.plist'
    if not info_path.is_file():
        row['error'] = 'No bundle Info.plist'; return row
    try:
        info = plistlib.loads(info_path.read_bytes())
        row['info'] = {key: info.get(key) for key in ['CFBundleIdentifier', 'CFBundleExecutable', 'CFBundleName',
            'CFBundleVersion', 'CFBundleShortVersionString', 'MinimumOSVersion', 'LSMinimumSystemVersion',
            'CFBundleSupportedPlatforms', 'UIDeviceFamily']}
        name = info.get('CFBundleExecutable')
        executable = path / 'Contents/MacOS' / name if info_path.parent.name == 'Contents' else path / name
        row['executable'] = str(executable)
        if executable.is_file():
            row['sha256'] = digest(executable)
            row['file'] = run(['/usr/bin/file', str(executable)])
            commands = run(['/usr/bin/otool', '-l', str(executable)])
            raw = commands.pop('stdout', '')
            rows = raw.splitlines()
            selected = []
            for n, line in enumerate(rows):
                if any(key in line for key in ['LC_BUILD_VERSION', 'LC_VERSION_MIN_', 'LC_UUID']):
                    selected.extend(rows[max(0, n - 1):n + 8])
            commands['platform_commands'] = selected
            row['mach_o'] = commands
            row['dependencies'] = run(['/usr/bin/otool', '-L', str(executable)])
            row['signature'] = run(['/usr/bin/codesign', '--verify', '--strict', str(path)])
            row['entitlements'] = run(['/usr/bin/codesign', '-d', '--entitlements', ':-', str(path)])
    except (OSError, ValueError, TypeError) as error:
        row['error'] = type(error).__name__ + ': ' + str(error)[:1000]
    return row


result = {'started_utc': datetime.now(timezone.utc).isoformat(), 'device': DEVICE,
          'scope': 'Artifact inventory only; no current-source execution or same-IPA proof',
          'simctl_signature': run(['/usr/bin/codesign', '--verify', '--strict', SIMCTL]),
          'installed_lumen': [], 'project_apps': [], 'ipa_files': [], 'dependency_candidates': []}
if result['simctl_signature'].get('return_code') != 0:
    raise SystemExit('Native simctl signature did not verify; refusing execution')
apps = run([SIMCTL, 'listapps', DEVICE])
raw = apps.pop('stdout', '')
(OUT / 'listapps.stdout').write_text(raw)
apps['stdout_sha256'] = hashlib.sha256(raw.encode()).hexdigest()
try:
    try:
        parsed = json.loads(raw)
    except ValueError:
        converted = run(['/usr/bin/plutil', '-convert', 'json', '-o', '-', '-'], raw)
        parsed = json.loads(converted.get('stdout', ''))
    apps['total_application_count'] = len(parsed)
    for identifier, values in parsed.items():
        if 'lume' in identifier.lower() or any('lume' in str(values.get(key, '')).lower() for key in ['CFBundleName', 'CFBundleDisplayName']):
            container = run([SIMCTL, 'get_app_container', DEVICE, identifier, 'app'])
            path = Path(container.get('stdout', '').strip())
            row = {'bundle_identifier': identifier, 'container': container}
            if container.get('return_code') == 0 and path.is_dir():
                row['app'] = app_info(path)
            result['installed_lumen'].append(row)
except (ValueError, TypeError, AttributeError) as error:
    apps['parse_error'] = type(error).__name__ + ': ' + str(error)[:500]
result['application_listing'] = apps

roots = sorted(set([p for p in ROOT.glob('build*') if p.is_dir()] +
                  [ROOT / name for name in ['out', 'output', 'deps', 'vendor', 'third_party'] if (ROOT / name).is_dir()]))
count = 0
found_apps = []
truncated = False
for base in roots:
    for directory, names, files in os.walk(base):
        path = Path(directory)
        count += len(names) + len(files)
        if count > 40000 or time.monotonic() > DEADLINE - 25:
            truncated = True; break
        depth = len(path.relative_to(base).parts)
        for name in list(names):
            child = path / name
            if name.endswith('.app'):
                if 'lume' in name.lower(): found_apps.append(child)
                names.remove(name)
            elif name.endswith('.xcframework'):
                result['dependency_candidates'].append(str(child)); names.remove(name)
        names[:] = [name for name in names if depth < 6 and name not in
                    ['.git', 'node_modules', 'wpt', 'web-platform-tests', 'target', '__pycache__']]
        for name in files:
            child = path / name
            if name.endswith('.ipa'):
                result['ipa_files'].append({'path': str(child), 'bytes': child.stat().st_size})
            if name in ['libv8_monolith.a', 'libv8.a', 'libv8_libbase.a', 'libv8_libplatform.a']:
                result['dependency_candidates'].append(str(child))
    if truncated: break
result['search'] = {'roots': [str(p) for p in roots], 'entries_examined': count,
                    'maximum_depth': 6, 'truncated': truncated, 'scope': 'Only selected project build/dependency roots'}
for path in sorted(set(found_apps), key=lambda p: p.stat().st_mtime, reverse=True)[:12]:
    if time.monotonic() > DEADLINE - 5: break
    result['project_apps'].append(app_info(path))
result['finished_utc'] = datetime.now(timezone.utc).isoformat()
(OUT / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
