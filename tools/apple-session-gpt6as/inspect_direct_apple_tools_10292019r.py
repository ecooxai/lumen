#!/usr/bin/env python3
"""Read-only vendor-tool/SDK inspection plus isolated C syntax probes.

This calls installed simctl directly to distinguish an xcrun/Xcode discovery
failure from simulator-service failure. It never repairs vendor files, changes
developer selection, grants entitlements, installs, boots, or launches an app.
"""
from pathlib import Path
import hashlib
import json
import os
import platform
import plistlib
import subprocess
import sys
import time
from datetime import datetime, timezone

ROOT = Path('/Users/devin/repos/lumen').resolve()
if platform.system() != 'Darwin' or Path.cwd().resolve() != ROOT:
    raise SystemExit('Run only from the verified Mac workspace')
OUTPUT = ROOT / 'out/10292019r-apple/direct-tools'
OUTPUT.mkdir(parents=True, exist_ok=True)
DEADLINE = time.monotonic() + 100
XCODE = Path('/Applications/Xcode-27.0-RC.app')
DEVELOPER = XCODE / 'Contents/Developer'
environment = dict(os.environ, DEVELOPER_DIR=str(DEVELOPER))


def run(name, argv, seconds=15):
    remaining = min(seconds, DEADLINE - time.monotonic())
    if remaining <= 0:
        return {'argv': argv, 'error': 'Inventory time budget exhausted; unrun'}
    try:
        process = subprocess.run(argv, env=environment, capture_output=True, text=True, timeout=remaining)
        stdout = OUTPUT / (name + '.stdout')
        stderr = OUTPUT / (name + '.stderr')
        stdout.write_text(process.stdout)
        stderr.write_text(process.stderr)
        value = {'argv': argv, 'return_code': process.returncode,
                 'stdout_file': str(stdout.relative_to(ROOT)), 'stderr_file': str(stderr.relative_to(ROOT)),
                 'stdout_sha256': hashlib.sha256(process.stdout.encode()).hexdigest(),
                 'stdout': process.stdout[:2500], 'stderr': process.stderr[:1800]}
        try:
            data = json.loads(process.stdout)
            if 'devices' in data:
                value['devices'] = {runtime: [{key: item.get(key) for key in ['name', 'udid', 'state', 'isAvailable', 'availabilityError']}
                                             for item in items[:30]] for runtime, items in data['devices'].items()}
                value['device_count'] = sum(len(items) for items in data['devices'].values())
                value.pop('stdout')
            elif 'runtimes' in data:
                value['runtimes'] = data['runtimes'][:20]
                value.pop('stdout')
        except (ValueError, TypeError):
            pass
        return value
    except (OSError, subprocess.TimeoutExpired) as error:
        return {'argv': argv, 'error': type(error).__name__ + ': ' + str(error)[:1200]}


def inspect(path):
    row = {'path': str(path), 'exists': path.exists()}
    if path.is_file():
        raw = path.read_bytes()
        row.update(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
        if path.suffix in ['.plist', '.xcsynspec']:
            try:
                parsed = plistlib.loads(raw)
                row['python_plist_parseable'] = True
                row['plist_top_type'] = type(parsed).__name__
            except (ValueError, plistlib.InvalidFileException) as error:
                row['python_plist_parseable'] = False
                row['plist_error'] = type(error).__name__
            # Apple's native parser also accepts old-style plists that Python
            # does not. Its verdict, and file size, are authoritative here.
            row['native_plist_check'] = run('plist-' + hashlib.sha256(str(path).encode()).hexdigest()[:10],
                ['/usr/bin/plutil', '-lint', str(path)], seconds=5)
    return row


result = {'started_utc': datetime.now(timezone.utc).isoformat(), 'xcode': str(XCODE),
          'scope': 'Direct installed tools and SDK headers; no simulator/app execution acceptance',
          'direct_tools': [], 'sdk_integrity': [], 'vendor_resource_integrity': []}
simctl = DEVELOPER / 'usr/bin/simctl'
result['direct_tools'].append(inspect(simctl))
if simctl.is_file() and os.access(simctl, os.X_OK):
    result['direct_simctl_file'] = run('simctl-file', ['/usr/bin/file', str(simctl)])
    result['direct_simctl_signature'] = run('simctl-signature', ['/usr/bin/codesign', '--verify', '--strict', str(simctl)])
    result['direct_devices'] = run('simctl-devices', [str(simctl), 'list', 'devices', 'available', '--json'])
    result['direct_runtimes'] = run('simctl-runtimes', [str(simctl), 'list', 'runtimes', '--json'])

syntax_source = OUTPUT / 'sdk_syntax_probe.c'
syntax_source.write_text('#include <stdint.h>\n#include <stddef.h>\nint main(void){uint64_t value=0;return (int)value;}\n')
clang = Path('/Library/Developer/CommandLineTools/usr/bin/clang')
for directory, target in [('iPhoneOS', 'arm64-apple-ios17.0'), ('iPhoneSimulator', 'arm64-apple-ios17.0-simulator')]:
    candidates = sorted((DEVELOPER / ('Platforms/' + directory + '.platform/Developer/SDKs')).glob('*.sdk'))
    real = sorted(set(path.resolve() for path in candidates if path.is_dir()))
    for sdk in real[:2]:
        row = {'sdk': str(sdk), 'target': target, 'files': [inspect(sdk / relative) for relative in
            ['SDKSettings.plist', 'SDKSettings.json', 'usr/include/stdint.h', 'usr/lib/libSystem.tbd']]}
        if clang.is_file():
            row['syntax_probe'] = run('sdk-' + directory + '-' + sdk.name,
                [sys.executable, str(ROOT / 'tools/monitor_process.py'), '--duration', '20', '--settle', '0',
                 '--output', str(OUTPUT / ('sdk-syntax-' + directory)), '--',
                 str(clang), '-target', target, '-isysroot', str(sdk), '-fsyntax-only', str(syntax_source)], seconds=25)
        result['sdk_integrity'].append(row)

for pattern in ['Contents/SharedFrameworks/*/Versions/*/Resources/Built-in Syntax Types.xcsynspec',
                'Contents/SharedFrameworks/*/Resources/Built-in Syntax Types.xcsynspec',
                'Contents/SharedFrameworks/AssetCatalogCocoaTouchFoundation.framework/Versions/*/Resources/Info.plist',
                'Contents/SharedFrameworks/AssetCatalogCocoaTouchFoundation.framework/Resources/Info.plist',
                'Contents/PlugIns/*/Contents/Resources/Built-in Syntax Types.xcsynspec']:
    for path in sorted(XCODE.glob(pattern))[:12]:
        result['vendor_resource_integrity'].append(inspect(path))
result['finished_utc'] = datetime.now(timezone.utc).isoformat()
result['elapsed_s'] = round(100 - (DEADLINE - time.monotonic()), 3)
path = OUTPUT / 'receipt.json'
path.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
