"""Diagnose explicit Apple compiler/SDK use without modifying system policy or files.

A tiny compile/link probe establishes toolchain availability only. It is not
Lume, an IPA, or a device-launch acceptance result.
"""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time

ROOT = Path('/Users/devin/repos/lumen').resolve()
XCODE = Path('/Applications/Xcode-27.0-RC.app/Contents/Developer')
TOOLS = XCODE / 'Toolchains/XcodeDefault.xctoolchain/usr/bin'
DEVICE_SDK = XCODE / 'Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS27.0.sdk'
SIMULATOR_SDK = XCODE / 'Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator27.0.sdk'
OUT = ROOT / 'output/session-10290244/apple-direct-sdk-probe'
PREVIOUS = ROOT / 'output/session-10290244/apple-toolchain-recovery-readonly'


def utc():
    return datetime.now(timezone.utc).isoformat()


def metadata(path):
    result = {'path': str(path), 'exists': path.exists(), 'symlink': path.is_symlink()}
    if path.exists():
        result.update({'bytes': path.stat().st_size, 'directory': path.is_dir()})
    return result


def macho(path):
    with path.open('rb') as stream:
        header = stream.read(32)
        magic, cpu, subtype, kind, count, size, flags, reserved = struct.unpack('<8I', header)
        result = {'magic': hex(magic), 'cpu': hex(cpu), 'filetype': kind, 'build_platform': None}
        if magic != 0xfeedfacf or size > path.stat().st_size - 32:
            raise RuntimeError('Unexpected compiler output format')
        remaining = size
        for _ in range(count):
            command, length = struct.unpack('<2I', stream.read(8))
            if length < 8 or length > remaining:
                raise RuntimeError('Malformed compiler output load command')
            if command == 0x32:
                result['build_platform'] = struct.unpack('<I', stream.read(4))[0]
                stream.seek(length - 12, 1)
            else:
                stream.seek(length - 8, 1)
            remaining -= length
    result['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    result['bytes'] = path.stat().st_size
    return result


def main():
    os.chdir(ROOT)
    OUT.mkdir(parents=True, exist_ok=False)
    result = {'started_utc': utc(), 'scope': 'Task-owned compile/link diagnostics using unmodified explicit Xcode tools and SDKs; no global xcode-select change, altered Apple metadata, signing imports, simulator reset, or app installation.', 'commands': {}, 'metadata': [], 'prior_diagnostics': {}}
    for name in ['xcode-version.stdout.txt', 'iphoneos-sdk-path.stderr.txt', 'iphoneos-clang.stderr.txt', 'github-releases.stderr.txt']:
        path = PREVIOUS / name
        if path.is_file():
            text = path.read_text(errors='replace')
            result['prior_diagnostics'][name] = text[:20000]
    for path in [TOOLS / 'clang', TOOLS / 'clang++', TOOLS / 'ld', TOOLS / 'otool', XCODE / 'usr/bin/simctl',
                 DEVICE_SDK / 'SDKSettings.json', SIMULATOR_SDK / 'SDKSettings.json',
                 SIMULATOR_SDK / 'System/Library/Frameworks/UIKit.framework/UIKit.tbd']:
        result['metadata'].append(metadata(path))
    environment = dict(os.environ)
    environment['DEVELOPER_DIR'] = str(XCODE)
    environment['PATH'] = str(TOOLS) + ':' + environment.get('PATH', '')

    def command(name, argv, timeout):
        started = time.monotonic()
        entry = {'command': [str(value) for value in argv], 'started_utc': utc(), 'timeout_seconds': timeout}
        try:
            completed = subprocess.run(entry['command'], cwd=ROOT, env=environment, stdin=subprocess.DEVNULL,
                                       capture_output=True, text=True, timeout=timeout)
            entry['exit_code'] = completed.returncode
            stdout, stderr = completed.stdout, completed.stderr
        except subprocess.TimeoutExpired as error:
            entry['timed_out'] = True
            stdout, stderr = error.stdout or b'', error.stderr or b''
            if isinstance(stdout, bytes): stdout = stdout.decode('utf-8', 'replace')
            if isinstance(stderr, bytes): stderr = stderr.decode('utf-8', 'replace')
        except OSError as error:
            entry['error'] = str(error)
            stdout = stderr = ''
        entry.update({'finished_utc': utc(), 'duration_seconds': round(time.monotonic()-started, 3),
                      'stdout_file': name+'.stdout.txt', 'stderr_file': name+'.stderr.txt',
                      'stdout_bytes': len(stdout.encode()), 'stderr_bytes': len(stderr.encode())})
        (OUT/entry['stdout_file']).write_text(stdout)
        (OUT/entry['stderr_file']).write_text(stderr)
        result['commands'][name] = entry
        return entry.get('exit_code') == 0

    clang = TOOLS/'clang'
    if clang.is_file() and command('direct-clang-version', [clang, '--version'], 10):
        source = OUT/'toolchain-smoke.m'
        source.write_text('#import <UIKit/UIKit.h>\nint main(int argc,char **argv) { @autoreleasepool { return UIApplicationMain(argc,argv,nil,nil); } }\n')
        for name, sdk, triple, platform in [('device', DEVICE_SDK, 'arm64-apple-ios16.0', 2), ('simulator', SIMULATOR_SDK, 'arm64-apple-ios16.0-simulator', 7)]:
            if not sdk.is_dir():
                continue
            object_file = OUT/(name+'.o')
            flags = [clang, '-target', triple, '-isysroot', sdk, '-fobjc-arc', '-O0']
            if command(name+'-compile', flags+['-c', source, '-o', object_file], 20):
                result[name+'_object'] = macho(object_file)
                binary = OUT/(name+'-toolchain-smoke')
                if command(name+'-link', flags+[object_file, '-framework', 'UIKit', '-framework', 'Foundation', '-o', binary], 20):
                    result[name+'_binary'] = macho(binary)
                    result[name+'_platform_matches'] = result[name+'_binary']['build_platform'] == platform
    simctl = XCODE/'usr/bin/simctl'
    if simctl.is_file():
        command('simulator-runtimes', [simctl, 'list', 'runtimes', '--json'], 12)
        command('simulator-devices', [simctl, 'list', 'devices', 'available', '--json'], 12)
    result['finished_utc'] = utc()
    (OUT/'receipt.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
