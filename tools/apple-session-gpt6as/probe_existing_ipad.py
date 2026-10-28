#!/usr/bin/env python3
"""Bounded baseline of one already-installed, inactive iPad Simulator app.

No install/build/boot, no termination of an existing app, and no shared profile.
This older binary cannot establish that the new Mac source fixes reached iPad.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from probe_existing_release import BoundedLog, SERVER_PROGRAM, MIB, atomic_json, free_mib, tree_bytes, stop_owned

DEVICE = 'A77F68E4-972B-472B-80B8-F65E71D361E5'
BUNDLE = 'com.ecooxai.lumen'


def read_command(*arguments):
    return subprocess.run(list(arguments), capture_output=True, text=True, check=True, timeout=20).stdout.strip()


def identity(pid, executable):
    try:
        result = subprocess.run(['ps', '-ww', '-p', str(pid), '-o', 'lstart=', '-o', 'comm='],
                                capture_output=True, text=True, timeout=4)
    except (OSError, subprocess.SubprocessError):
        return False  # Unknown identity must not authorize a signal or deletion.
    if result.returncode not in (0, 1) or result.stderr.strip():
        return False
    if not result.stdout.strip():
        return None
    value = result.stdout.strip()
    if not value.endswith(str(executable)):
        return False
    return value


class SimConsole(BoundedLog):
    def __init__(self, *arguments):
        self.app_pid = None
        super().__init__(*arguments)

    def inspect(self, line):
        if line.strip().startswith(BUNDLE + ':'):
            try:
                candidate = int(line.split(':', 1)[1].strip())
                if candidate > 1:
                    self.app_pid = candidate
            except ValueError:
                pass
        super().inspect(line)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', required=True, choices=[DEVICE])
    parser.add_argument('--expected-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=45, choices=range(15, 61))
    parser.add_argument('--backend', required=True, choices=['webgl', 'webgpu'])
    args = parser.parse_args()
    root = Path.cwd().resolve()
    if sys.platform != 'darwin':
        parser.error('The selected iPad Simulator baseline requires macOS')
    output = args.output.resolve()
    if not output.is_relative_to(root / 'output') or output.exists():
        parser.error('Use a new output directory inside this project')
    devices = json.loads(read_command('xcrun', 'simctl', 'list', 'devices', 'booted', '--json'))
    selected = [device for rows in devices['devices'].values() for device in rows if device.get('udid') == DEVICE]
    if len(selected) != 1 or selected[0].get('state') != 'Booted' or 'iPad' not in selected[0].get('name', ''):
        parser.error('The explicit selected iPad is not booted; no device will be booted')
    app = Path(read_command('xcrun', 'simctl', 'get_app_container', DEVICE, BUNDLE, 'app')).resolve(strict=True)
    data = Path(read_command('xcrun', 'simctl', 'get_app_container', DEVICE, BUNDLE, 'data')).resolve(strict=True)
    if DEVICE not in app.parts or DEVICE not in data.parts or 'Application' not in data.parts:
        parser.error('Returned app containers do not belong to the explicit simulator')
    info = plistlib.loads((app / 'Info.plist').read_bytes())
    if info.get('CFBundleIdentifier') != BUNDLE:
        parser.error('Installed bundle identity differs from the reviewed app')
    executable = (app / info['CFBundleExecutable']).resolve(strict=True)
    if not executable.is_relative_to(app):
        parser.error('Installed executable escaped its app bundle')
    digest = hashlib.sha256()
    with executable.open('rb') as source:
        while chunk := source.read(MIB):
            digest.update(chunk)
    if digest.hexdigest() != args.expected_sha256:
        parser.error('Installed iPad binary differs from the reviewed baseline hash')
    jobs = read_command('xcrun', 'simctl', 'spawn', DEVICE, 'launchctl', 'list')
    if any(BUNDLE in line and line.split()[0].isdigit() for line in jobs.splitlines() if line.split()):
        parser.error('Selected iPad app is already active; no existing session will be changed')
    if str(executable) in read_command('ps', '-axo', 'pid=,comm='):
        parser.error('Installed executable is already running; no existing session will be changed')
    available = lambda: min(free_mib(root), free_mib(data))
    initial_free = available()
    if initial_free < 128:
        parser.error(f'Only {initial_free:.1f} MiB free; at least128 MiB is required')
    node = shutil.which('node')
    server_file = root / 'tools/apple-web-lab-gpt6as/server.mjs'
    telemetry = root / 'scripts/qa-apple-gpt6/telemetry.py'
    if not node or not server_file.is_file() or not telemetry.is_file():
        parser.error('Existing Node, lab and telemetry are required; nothing will be installed')
    output.mkdir(parents=True, exist_ok=False)
    profile = Path(tempfile.mkdtemp(prefix='lumen-gpt6-existing-', dir=data / 'tmp'))
    shot = profile / 'native-ipad.png'
    targets = output / 'targets.json'
    receipt = {'startedAt': datetime.now(timezone.utc).isoformat(), 'device': selected[0],
               'bundle': BUNDLE, 'binary': str(executable), 'sha256': digest.hexdigest(),
               'appVersion': info.get('CFBundleShortVersionString'), 'appBuild': info.get('CFBundleVersion'),
               'scope': 'Already-installed older iPad app baseline; not the new Mac build/patch or same-IPA proof',
               'buildPerformed': False, 'installPerformed': False, 'profile': 'new owned temporary directory inside selected app data container',
               'backend': args.backend, 'requestedSeconds': args.seconds, 'initialFreeMiB': round(initial_free, 3),
               'diskFloorMiB': 64, 'outputLimitMiB': 32, 'logLimitMiB': 1,
               'permissionPromptsRequested': False, 'functionalPass': False, 'issues': []}
    server = sampler = console = None
    server_log = app_log = None
    app_pid = app_identity = None
    sampler_log = None
    def phase(name):
        atomic_json(targets, {'phase': name, 'groups': [{'name': 'lumen-ipad', 'pids': [app_pid]}] if app_pid else []})
    def interrupted(_signum, _frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    try:
        phase('baseline')
        server = subprocess.Popen([node, '--input-type=module', '--eval', SERVER_PROGRAM, str(server_file)],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                  start_new_session=True)
        server_log = BoundedLog(server.stdout, output / 'server.log')
        deadline = time.monotonic() + 6
        while server_log.port is None and time.monotonic() < deadline:
            if server.poll() is not None or server_log.error:
                raise RuntimeError('Owned companion server failed')
            time.sleep(.1)
        if not server_log.port:
            raise RuntimeError('Owned companion server startup timed out')
        url = f'http://127.0.0.1:{server_log.port}/?autorun=1&backend={args.backend}&cleanup=1'
        receipt['url'] = url
        sampler_log = (output / 'sampler.log').open('x')
        sampler = subprocess.Popen([sys.executable, str(telemetry), 'collect', '--targets', str(targets),
                                    '--output', str(output / 'process-samples.jsonl'), '--seconds', str(args.seconds + 40)],
                                   stdout=sampler_log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, start_new_session=True)
        time.sleep(1.1)
        if sampler.poll() is not None or available() < 128:
            raise RuntimeError('Telemetry failed or free space fell below128 MiB before app launch')
        jobs = read_command('xcrun', 'simctl', 'spawn', DEVICE, 'launchctl', 'list')
        if any(BUNDLE in line and line.split()[0].isdigit() for line in jobs.splitlines() if line.split()):
            raise RuntimeError('Selected app became active before launch; no existing session will be changed')
        if str(executable) in read_command('ps', '-ww', '-axo', 'pid=,comm='):
            raise RuntimeError('Installed executable became active before launch; no existing session will be changed')
        environment = dict(os.environ)
        for name in ('LUMEN_PRE_FILE', 'LUMEN_FRAME_PRE', 'JSRUN_PRE'):
            environment.pop('SIMCTL_CHILD_' + name, None)
        for key, value in {'LUMEN_PROFILE_DIR': str(profile), 'LUMEN_DOWNLOAD_DIR': str(profile / 'downloads'),
                           'LUMEN_SHOT': str(shot), 'LUMEN_SHOT_MS': str(args.seconds * 1000),
                           'LUMEN_CONSOLE': '1', 'LUMEN_CPU_DEBUG': '1'}.items():
            environment['SIMCTL_CHILD_' + key] = value
        console = subprocess.Popen(['xcrun', 'simctl', 'launch', '--console', DEVICE, BUNDLE, url],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                   env=environment, start_new_session=True)
        app_log = SimConsole(console.stdout, output / 'lumen.log')
        deadline = time.monotonic() + args.seconds + 15
        next_disk_check = 0
        idle_marked = False
        while console.poll() is None:
            if sampler.poll() is not None or server.poll() is not None:
                raise RuntimeError('An owned collector/server exited during the iPad probe')
            if app_log.error or server_log.error:
                raise RuntimeError(app_log.error or server_log.error)
            if app_pid is None and app_log.app_pid:
                app_pid = app_log.app_pid
                app_identity = identity(app_pid, executable)
                if not app_identity:
                    raise RuntimeError('Returned app PID could not be matched to the installed executable')
                receipt['appPID'] = app_pid
                receipt['processIdentity'] = app_identity
                phase('functional')
            now = time.monotonic()
            if now >= next_disk_check:
                if available() < 64 or tree_bytes(output) + tree_bytes(profile) > 32 * MIB:
                    raise RuntimeError('iPad probe exceeded its disk floor/output budget')
                next_disk_check = now + 1
            if app_pid and app_log.cleanup_seen and not idle_marked:
                phase('idle'); idle_marked = True
            if now >= deadline:
                raise RuntimeError('iPad screenshot/exit deadline exceeded')
            time.sleep(.25)
        receipt['consoleReturncode'] = console.returncode
        if app_pid is None:
            raise RuntimeError('No verified app PID was registered with the one-second collector')
    except (Exception, KeyboardInterrupt) as error:
        receipt['issues'].append(type(error).__name__ + ': ' + str(error))
    finally:
        try:
            phase('shutdown')
        except OSError:
            pass
        # The simulator app is launchd-owned, NOT in simctl's process group.
        # Never signal any preexisting app; match the returned PID and start identity.
        termination = {'pid': app_pid, 'forcedKill': False, 'signalSent': False, 'appExitCode': 'not inferred from simctl exit code'}
        if app_pid and app_identity and identity(app_pid, executable) == app_identity:
            try:
                os.kill(app_pid, signal.SIGTERM); termination['signalSent'] = True
            except ProcessLookupError:
                pass
            except OSError as error:
                receipt['issues'].append('Owned app SIGTERM failed: ' + str(error))
            deadline = time.monotonic() + 4
            while time.monotonic() < deadline and identity(app_pid, executable) == app_identity:
                time.sleep(.2)
            if identity(app_pid, executable) == app_identity:
                try:
                    os.kill(app_pid, signal.SIGKILL); termination['forcedKill'] = True
                except ProcessLookupError:
                    pass
                except OSError as error:
                    receipt['issues'].append('Owned app SIGKILL failed: ' + str(error))
        receipt['appTermination'] = termination
        receipt['consoleTermination'] = stop_owned(console)
        receipt['serverTermination'] = stop_owned(server)
        receipt['samplerTermination'] = stop_owned(sampler)
        if sampler_log:
            sampler_log.close()
        for reader in (app_log, server_log):
            if reader:
                reader.thread.join(timeout=2)
                if reader.error:
                    receipt['issues'].append(reader.error)
        receipt['reports'] = app_log.reports if app_log else []
        if shot.is_file() and shot.stat().st_size <= 24 * MIB:
            target = output / 'gpt-6-astra-pro_devinmac3d_existing-ipad.png'
            shutil.copy2(shot, target); receipt['screenshot'] = target.name
        receipt['ownedProfileBytes'] = tree_bytes(profile)
        # Only the mkdtemp directory created by this invocation may be removed.
        # Preserve it for recovery if app process cleanup could not be verified.
        if app_pid and identity(app_pid, executable) is None:
            shutil.rmtree(profile)
            receipt['ownedProfileRemoved'] = True
        else:
            receipt['ownedProfileRemoved'] = False
            receipt['retainedOwnedProfile'] = str(profile)
            receipt['issues'].append('Owned profile retained because app exit was not verified')
        receipt['finishedAt'] = datetime.now(timezone.utc).isoformat()
        receipt['remainingFreeMiB'] = round(available(), 3)
        atomic_json(output / 'probe-receipt.json', receipt)
    print(json.dumps({'receipt': str(output / 'probe-receipt.json'), 'reports': len(receipt['reports']),
                      'issues': receipt['issues'], 'consoleReturncode': receipt.get('consoleReturncode')}))
    return 0 if not receipt['issues'] and receipt.get('consoleReturncode') == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
