#!/usr/bin/env python3
"""Bounded, read-only Apple toolchain discovery; never changes xcode-select.

Run from the verified Mac repository. The CLI developer directory alone does
not establish whether another full Xcode installation exists. This inventories
only developer application/SDK/runtime/profile locations, not user documents.
"""
import glob
import json
import os
from pathlib import Path
import platform
import plistlib
import subprocess
import time
from datetime import datetime, timezone


ROOT = Path('/Users/devin/repos/lumen').resolve()
if platform.system() != 'Darwin' or Path.cwd().resolve() != ROOT:
    raise SystemExit('Run only from the verified /Users/devin/repos/lumen Mac workspace')
DEADLINE = time.monotonic() + 90


def run(argv, *, developer=None, seconds=12):
    seconds = min(seconds, DEADLINE - time.monotonic())
    if seconds <= 0:
        return {'argv': argv, 'error': 'Read-only inventory time budget exhausted; operation unrun'}
    environment = dict(os.environ)
    if developer:
        environment['DEVELOPER_DIR'] = str(developer)
    try:
        result = subprocess.run(argv, env=environment, capture_output=True, text=True, timeout=seconds)
        return {'argv': argv, 'return_code': result.returncode,
                'stdout': result.stdout[:3000], 'stderr': result.stderr[:1500]}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {'argv': argv, 'error': type(error).__name__ + ': ' + str(error)[:1000]}


result = {'started_utc': datetime.now(timezone.utc).isoformat(),
          'scope': 'Read-only installed Xcode, iOS SDK, simulator, and dependency inventory',
          'root': str(ROOT), 'system': platform.platform(), 'architecture': platform.machine(),
          'active_developer': run(['/usr/bin/xcode-select', '-p']),
          'git': run(['/usr/bin/git', 'status', '--short', '--branch'])}

applications = set()
for pattern in ['/Applications/Xcode*.app', '/Applications/*/Xcode*.app',
                str(Path.home() / 'Applications/Xcode*.app'), '/Volumes/*/Applications/Xcode*.app']:
    applications.update(Path(value).resolve() for value in glob.glob(pattern))
spotlight = run(['/usr/bin/mdfind', "kMDItemCFBundleIdentifier == 'com.apple.dt.Xcode'"], seconds=15)
result['xcode_spotlight'] = spotlight
if spotlight.get('return_code') == 0:
    applications.update(Path(value).resolve() for value in spotlight['stdout'].splitlines()
                        if value.endswith('.app') and Path(value).is_dir())
result['application_limit'] = 8
result['applications'] = []
for application in sorted(applications)[:8]:
    developer = application / 'Contents/Developer'
    row = {'application': str(application), 'developer': str(developer),
           'exists': developer.is_dir(),
           'sdk_paths': sorted(str(p) for p in developer.glob('Platforms/*.platform/Developer/SDKs/*.sdk'))}
    try:
        info = plistlib.loads((application / 'Contents/Info.plist').read_bytes())
        row['bundle_identifier'] = info.get('CFBundleIdentifier')
        row['version'] = info.get('CFBundleShortVersionString')
    except (OSError, ValueError) as error:
        row['plist_error'] = type(error).__name__
    if developer.is_dir():
        row['version_command'] = run(['/usr/bin/xcodebuild', '-version'], developer=developer)
        row['iphoneos'] = run(['/usr/bin/xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], developer=developer)
        row['iphonesimulator'] = run(['/usr/bin/xcrun', '--sdk', 'iphonesimulator', '--show-sdk-path'], developer=developer)
        row['simulator_devices'] = run(['/usr/bin/xcrun', 'simctl', 'list', 'devices', 'available', '--json'], developer=developer)
    result['applications'].append(row)

result['simulator_runtime_paths'] = sorted(set(glob.glob('/Library/Developer/CoreSimulator/Profiles/Runtimes/*.simruntime') +
    glob.glob(str(Path.home() / 'Library/Developer/CoreSimulator/Profiles/Runtimes/*.simruntime'))))
result['device_set_plist_exists'] = (Path.home() / 'Library/Developer/CoreSimulator/Devices/device_set.plist').is_file()
result['provisioning_profile_counts'] = {}
for location in [Path.home() / 'Library/MobileDevice/Provisioning Profiles',
                 Path.home() / 'Library/Developer/Xcode/UserData/Provisioning Profiles']:
    result['provisioning_profile_counts'][str(location)] = len(list(location.glob('*.mobileprovision'))) if location.is_dir() else None

dependency_roots = [ROOT / 'build-apple-toolchain', Path.home() / 'ios-deps',
                    Path.home() / 'src/v8ios/v8', Path('/opt/homebrew/opt/v8')]
result['dependency_roots'] = []
for location in dependency_roots:
    result['dependency_roots'].append({'path': str(location), 'exists': location.exists(),
        'children': sorted(p.name for p in location.iterdir())[:40] if location.is_dir() else []})
result['finished_utc'] = datetime.now(timezone.utc).isoformat()
output = ROOT / 'out/10292019r-apple/installed-xcode-inventory.json'
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
