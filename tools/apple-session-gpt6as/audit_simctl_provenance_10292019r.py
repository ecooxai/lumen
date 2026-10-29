#!/usr/bin/env python3
"""Read-only audit of an unexpected unsigned simctl wrapper.

Never executes the wrapper or a discovered backing program. Device plists and
wrapper output are inventory claims, not proof of simulator/app execution.
"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import os
import platform
import plistlib
import re
import subprocess
import time

ROOT = Path('/Users/devin/repos/lumen').resolve()
if platform.system() != 'Darwin' or Path.cwd().resolve() != ROOT:
    raise SystemExit('Run only from the verified Mac workspace')
OUT = ROOT / 'out/10292019r-apple/simctl-provenance'
OUT.mkdir(parents=True, exist_ok=True)
DEADLINE = time.monotonic() + 60


def run(argv, stdout_limit=5000):
    timeout = min(8, DEADLINE - time.monotonic())
    if timeout <= 0:
        return {'argv': argv, 'unrun': 'audit budget exhausted'}
    try:
        value = subprocess.run(argv, text=True, capture_output=True, timeout=timeout)
        return {'argv': argv, 'return_code': value.returncode,
                'stdout': value.stdout[:stdout_limit] if stdout_limit is not None else value.stdout,
                'stderr': value.stderr[:1500]}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {'argv': argv, 'error': type(error).__name__ + ': ' + str(error)[:800]}


def file_info(path, signature=False):
    value = {'path': str(path), 'exists': path.exists()}
    if path.is_file():
        stat = path.stat()
        value.update(bytes=stat.st_size, uid=stat.st_uid, gid=stat.st_gid,
                     modified_utc=datetime.fromtimestamp(stat.st_mtime, timezone.utc).isoformat(),
                     resolved_path=str(path.resolve()), file=run(['/usr/bin/file', str(path)]))
        if signature:
            value['signature'] = run(['/usr/bin/codesign', '--verify', '--strict', str(path)])
    return value


wrapper = Path('/Applications/Xcode-27.0-RC.app/Contents/Developer/usr/bin/simctl')
result = {'started_utc': datetime.now(timezone.utc).isoformat(),
          'scope': 'Wrapper text, vendor tools, simulator runtime files and actual process inventory only',
          'wrapper': file_info(wrapper), 'backing_candidates': [], 'device_plists': [], 'runtime_files': []}
if wrapper.is_file():
    data = wrapper.read_bytes()
    result['wrapper']['sha256'] = hashlib.sha256(data).hexdigest()
    (OUT / 'observed-simctl-wrapper.txt').write_bytes(data)
    if len(data) <= 16384 and data.startswith(b'#!'):
        text = data.decode('utf-8', 'replace')
        # Preserve ordinary short source; suppress long encoded strings.
        result['wrapper']['source'] = re.sub(r'[A-Za-z0-9+/=]{200,}',
            lambda match: match[0][:50] + '[encoded data omitted, bytes=' + str(len(match[0])) + ']' + match[0][-50:], text)
        candidates = set(re.findall(r'(/[A-Za-z0-9_./+-]*simctl[A-Za-z0-9_./+-]*)', text))
        candidates.update(['/Library/Developer/PrivateFrameworks/CoreSimulator.framework/Versions/A/Resources/bin/simctl',
                           '/Applications/Xcode-27.0-RC.app/Contents/Developer/usr/bin/simctl.real',
                           '/Applications/Xcode-27.0-RC.app/Contents/Developer/usr/bin/simctl.bin'])
        for value in sorted(candidates)[:12]:
            path = Path(value)
            if path != wrapper:
                result['backing_candidates'].append(file_info(path, signature=True))

for value in ['/usr/bin/uname', '/bin/ps', '/Library/Developer/CommandLineTools/usr/bin/clang']:
    result.setdefault('system_tool_formats', []).append(file_info(Path(value), signature=True))
processes = run(['/bin/ps', '-A', '-o', 'pid=,ppid=,stat=,lstart=,comm='], stdout_limit=None)
if processes.get('return_code') == 0:
    relevant = [line for line in processes.pop('stdout').splitlines() if any(token in line for token in
                ['CoreSimulator', 'Simulator.app', 'launchd_sim', 'SpringBoard', '/lumen', '/Lumen'])]
    processes['relevant_processes'] = relevant[:80]
    processes['note'] = 'Executable paths only, no command arguments. Absence is a point-in-time observation.'
result['actual_process_inventory'] = processes

for udid in ['3DD498EB-4464-4ED7-9CD3-7667889DD1F6', 'A77F68E4-972B-472B-80B8-F65E71D361E5']:
    path = Path.home() / 'Library/Developer/CoreSimulator/Devices' / udid / 'device.plist'
    row = {'path': str(path), 'exists': path.exists(), 'scope': 'Persisted metadata, not execution proof'}
    if path.is_file():
        try:
            data = path.read_bytes(); value = plistlib.loads(data)
            row.update(sha256=hashlib.sha256(data).hexdigest(), values={key: value.get(key) for key in
                       ['UDID', 'name', 'state', 'runtime', 'deviceType', 'isAvailable']})
        except (OSError, ValueError) as error:
            row['error'] = type(error).__name__
    result['device_plists'].append(row)

runtime = Path('/Library/Developer/CoreSimulator/Profiles/Runtimes/iOS 27.0.simruntime/Contents/Resources/RuntimeRoot')
for relative in ['usr/lib/dyld', 'sbin/launchd', 'System/Library/CoreServices/SpringBoard.app/SpringBoard',
                 'System/Library/CoreServices/SystemVersion.plist']:
    result['runtime_files'].append(file_info(runtime / relative))
result['finished_utc'] = datetime.now(timezone.utc).isoformat()
(OUT / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
