"""Bounded Apple toolchain/artifact recovery inventory; never alters Xcode."""
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path('/Users/devin/repos/lumen').resolve()
OUT = ROOT / 'output/session-10290244/apple-toolchain-recovery-readonly'
XCODE = Path('/Applications/Xcode-27.0-RC.app')
DEVELOPER = XCODE / 'Contents/Developer'


def utc():
    return datetime.now(timezone.utc).isoformat()


def metadata(path):
    entry = {'path': str(path), 'exists': path.exists(), 'symlink': path.is_symlink()}
    if path.exists():
        info = path.stat()
        entry.update({'bytes': info.st_size, 'directory': path.is_dir()})
    if path.is_symlink():
        entry['link_target'] = os.readlink(path)
    return entry


def main():
    os.chdir(ROOT)
    OUT.mkdir(parents=True, exist_ok=False)
    result = {'started_utc': utc(), 'scope': 'Read-only Apple Xcode/SDK and task-specific archive metadata; no resets, signing imports, account login, system metadata repair or permission changes.', 'commands': {}}
    result['xcode_bundles'] = [metadata(path) for path in Path('/Applications').iterdir() if path.name.startswith('Xcode')]
    downloads = Path('/Users/devin/Downloads')
    result['matching_installer_names'] = [metadata(path) for path in downloads.iterdir() if ('xcode' in path.name.lower() or 'simulator' in path.name.lower()) and path.suffix.lower() in ('.xip', '.dmg', '.pkg', '.zip')] if downloads.is_dir() else []
    sdk = DEVELOPER / 'Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS27.0.sdk'
    result['sdk_files'] = [metadata(sdk / suffix) for suffix in [
        'SDKSettings.json', 'SDKSettings.plist', 'usr/include/stdio.h',
        'System/Library/Frameworks/UIKit.framework/Headers/UIKit.h',
        'System/Library/Frameworks/Foundation.framework/Headers/Foundation.h',
        'System/Library/Frameworks/UIKit.framework/UIKit.tbd',
    ]]
    environment = dict(os.environ)
    environment['DEVELOPER_DIR'] = str(DEVELOPER)
    commands = [
        ('xcode-version', [str(DEVELOPER / 'usr/bin/xcodebuild'), '-version'], 15, environment),
        ('iphoneos-sdk-path', ['/usr/bin/xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], 10, environment),
        ('iphoneos-clang', ['/usr/bin/xcrun', '--sdk', 'iphoneos', '-f', 'clang'], 10, environment),
    ]
    gh = shutil.which('gh')
    if gh:
        commands.append(('github-releases', [gh, 'release', 'list', '--repo', 'ecooxai/lumen', '--limit', '20', '--json', 'tagName,name,publishedAt,isDraft,isPrerelease'], 15, dict(os.environ)))
    for name, command, timeout, environment in commands:
        started = time.monotonic()
        entry = {'command': command, 'started_utc': utc(), 'timeout_seconds': timeout}
        try:
            completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       timeout=timeout, env=environment, stdin=subprocess.DEVNULL)
            entry['exit_code'] = completed.returncode
            stdout, stderr = completed.stdout, completed.stderr
        except subprocess.TimeoutExpired as error:
            entry['timed_out'] = True
            stdout, stderr = error.stdout or b'', error.stderr or b''
            if isinstance(stdout, bytes):
                stdout = stdout.decode('utf-8', 'replace')
            if isinstance(stderr, bytes):
                stderr = stderr.decode('utf-8', 'replace')
        except OSError as error:
            entry['error'] = str(error)
            stdout = stderr = ''
        entry.update({'finished_utc': utc(), 'duration_seconds': round(time.monotonic() - started, 3),
                      'stdout_file': name + '.stdout.txt', 'stderr_file': name + '.stderr.txt',
                      'stdout_bytes': len(stdout.encode()), 'stderr_bytes': len(stderr.encode())})
        (OUT / entry['stdout_file']).write_text(stdout)
        (OUT / entry['stderr_file']).write_text(stderr)
        result['commands'][name] = entry
    result['finished_utc'] = utc()
    (OUT / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
