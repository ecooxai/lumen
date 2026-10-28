#!/usr/bin/env python3
"""One bounded native demo probe using an existing, hash-verified Mac binary.

No compilation, package creation, dependency installation, credentials, or
capture/location permission requests. Stops only processes created by this run.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import struct
import subprocess
import sys
import threading
import time

MIB = 1024 * 1024
SERVER_PROGRAM = '''
import {pathToFileURL} from 'node:url';
const modulePath=process.argv[1];process.argv[1]='lume-owned-probe';
const {createLabServer}=await import(pathToFileURL(modulePath));
const server=createLabServer();
server.listen(0,'127.0.0.1',()=>console.log('LUME_OWNED_PORT '+server.address().port));
const close=()=>{server.close();server.closeAllConnections();};
process.on('SIGTERM',close);process.on('SIGINT',close);
'''


def utc():
    return datetime.now(timezone.utc).isoformat()


def atomic_json(path, value):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def free_mib(root):
    return shutil.disk_usage(root).free / MIB


def tree_bytes(root):
    return sum(file.stat().st_size for file in root.rglob('*')
               if file.is_file() and not file.is_symlink())


def stop_owned(process):
    if process is None:
        return None
    forced = False
    if process.poll() is None:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=4)
        except subprocess.TimeoutExpired:
            forced = True
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait(timeout=3)
    return {'pid': process.pid, 'returncode': process.returncode,
            'forcedKill': forced, 'checkedAt': utc()}


class BoundedLog:
    def __init__(self, stream, destination):
        self.stream, self.destination = stream, destination
        self.error = None
        self.port = None
        self.reports = []
        self.chunks = {}
        self.cleanup_seen = False
        self.thread = threading.Thread(target=self.consume, daemon=True)
        self.thread.start()

    def consume(self):
        count = 0
        pending = b''
        try:
            with self.destination.open('xb') as output:
                while True:
                    chunk = os.read(self.stream.fileno(), 4096)
                    if not chunk:
                        break
                    count += len(chunk)
                    if count > MIB:
                        self.error = 'app/server log exceeded 1 MiB cap'
                        break
                    output.write(chunk)
                    output.flush()
                    pending += chunk
                    while b'\n' in pending:
                        line, pending = pending.split(b'\n', 1)
                        self.inspect(line.decode(errors='replace'))
                if pending:
                    self.inspect(pending.decode(errors='replace'))
                if any(not group.get('complete') for group in self.chunks.values()):
                    self.error = self.error or 'incomplete chunked native diagnostic report'
        except OSError as error:
            self.error = str(error)

    def inspect(self, line):
        if line.startswith('LUME_OWNED_PORT '):
            try:
                self.port = int(line.split()[-1])
            except ValueError:
                self.error = 'invalid owned server port'
        chunk_marker = 'LUME_LAB_REPORT_CHUNK '
        offset = line.find(chunk_marker)
        if offset >= 0:
            try:
                chunk = json.loads(line[offset + len(chunk_marker):])
                if (not isinstance(chunk, dict) or chunk.get('version') != 1
                        or type(chunk.get('index')) is not int or type(chunk.get('total')) is not int
                        or not 0 <= chunk['index'] < chunk['total'] <= 512
                        or not isinstance(chunk.get('id'), str) or len(chunk['id']) > 64
                        or not isinstance(chunk.get('data'), str) or len(chunk['data']) > 256):
                    raise ValueError('invalid chunk metadata')
                if chunk['id'] not in self.chunks:
                    if len(self.chunks) >= 4:
                        raise ValueError('too many diagnostic report groups')
                    self.chunks[chunk['id']] = {'total': chunk['total'], 'parts': {}}
                group = self.chunks[chunk['id']]
                if group['total'] != chunk['total']:
                    raise ValueError('inconsistent diagnostic chunk count')
                if group.get('complete'):
                    return
                if chunk['index'] in group['parts'] and group['parts'][chunk['index']] != chunk['data']:
                    raise ValueError('conflicting diagnostic chunk')
                group['parts'][chunk['index']] = chunk['data']
                if len(group['parts']) == group['total']:
                    value = json.loads(''.join(group['parts'][index] for index in range(group['total'])))
                    self.reports.append({'marker': 'LUME_LAB_REPORT', 'transport': 'chunked-console', 'value': value})
                    group['complete'] = True
            except (ValueError, TypeError, KeyError) as error:
                self.error = 'invalid chunked native diagnostic report: ' + str(error)
            return
        if 'LUME_LAB_REPORT_ERROR ' in line:
            self.error = 'page rejected oversized diagnostic output'
            return
        for marker in ('LUME_LAB_REPORT ', 'LUME_LAB_CLEANUP '):
            offset = line.find(marker)
            if offset < 0 or len(self.reports) >= 8:
                continue
            try:
                value = json.loads(line[offset + len(marker):])
                self.reports.append({'marker': marker.strip(), 'value': value})
                if (marker == 'LUME_LAB_CLEANUP ' and isinstance(value, dict)
                        and value.get('activeResourceCount') == 0):
                    self.cleanup_seen = True
            except ValueError:
                pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build-apple-gpt6as/lumen')
    parser.add_argument('--expected-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=45, choices=range(15, 61))
    parser.add_argument('--backend', choices=['webgl', 'webgpu'], required=True)
    args = parser.parse_args()
    root = Path.cwd().resolve()
    if sys.platform != 'darwin':
        parser.error('This existing-binary probe requires macOS; no platform substitute is permitted')
    binary = (root / args.binary).resolve(strict=True)
    destination = args.output.resolve()
    if not binary.is_relative_to(root) or not destination.is_relative_to(root / 'output'):
        parser.error('Binary and output must remain in the current project')
    if destination.exists():
        parser.error('Use a new task output directory')
    lab = root / 'tools/apple-web-lab-gpt6as'
    server_file = lab / 'server.mjs'
    telemetry = root / 'scripts/qa-apple-gpt6/telemetry.py'
    if not server_file.is_file() or not (lab / 'public/index.html').is_file() or not telemetry.is_file():
        parser.error('The companion lab and existing telemetry source must be present; nothing will be installed')
    node = shutil.which('node')
    if not node:
        parser.error('Node is unavailable; nothing will be installed')
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    if digest != args.expected_sha256:
        parser.error('Existing Release binary hash differs from the reviewed receipt')
    initial_free = free_mib(root)
    if initial_free < 128:
        parser.error(f'Only {initial_free:.1f} MiB free; need at least128 MiB to start this bounded probe')
    destination.mkdir(parents=True, exist_ok=False)
    profile = destination / 'profile'
    profile.mkdir()
    shot = destination / 'gpt-6-astra-pro_devinmac3d_lumen-native.png'
    targets = destination / 'targets.json'
    samples = destination / 'process-samples.jsonl'
    receipt = {'startedAt': utc(), 'binary': str(binary), 'sha256': digest,
               'buildPerformed': False, 'packageCreated': False, 'backend': args.backend,
               'requestedSeconds': args.seconds, 'initialFreeMiB': round(initial_free, 3),
               'diskFloorMiB': 64, 'outputLimitMiB': 32, 'logLimitMiB': 1,
               'profile': 'new isolated task-owned directory',
               'scope': 'Single existing-Release local API probe; not a completed development/release iteration',
               'permissionPromptsRequested': False, 'functionalPass': False,
               'reports': [], 'issues': []}
    server = sampler = app = None
    server_log = app_log = None
    sampler_log = None
    def phase(name, pid=None):
        atomic_json(targets, {'phase': name, 'groups': [{'name': 'lumen', 'pids': [pid]}] if pid else []})
    def interrupted(_signum, _frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    try:
        phase('baseline')
        server = subprocess.Popen([node, '--input-type=module', '--eval', SERVER_PROGRAM, str(server_file)],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                  start_new_session=True)
        server_log = BoundedLog(server.stdout, destination / 'server.log')
        deadline = time.monotonic() + 6
        while server_log.port is None and time.monotonic() < deadline:
            if server.poll() is not None or server_log.error:
                raise RuntimeError('Owned companion server failed to start')
            time.sleep(.1)
        if not server_log.port:
            raise RuntimeError('Owned companion server did not report a port')
        url = f'http://127.0.0.1:{server_log.port}/?autorun=1&backend={args.backend}&cleanup=1'
        receipt['url'] = url
        sampler_log = (destination / 'sampler.log').open('x')
        sampler = subprocess.Popen([sys.executable, str(telemetry), 'collect', '--targets', str(targets),
                                    '--output', str(samples), '--seconds', str(args.seconds + 30)],
                                   stdout=sampler_log, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL, start_new_session=True)
        time.sleep(1.1)
        if sampler.poll() is not None or free_mib(root) < 128:
            raise RuntimeError('Telemetry failed or free space dropped below the128 MiB launch gate')
        environment = dict(os.environ)
        environment.update({'LUMEN_PROFILE_DIR': str(profile), 'LUMEN_SHOT': str(shot),
                            'LUMEN_SHOT_MS': str(args.seconds * 1000), 'LUMEN_WINDOW': '1200x900',
                            'LUMEN_NO_SIDEBAR': '1', 'LUMEN_CONSOLE': '1', 'LUMEN_CPU_DEBUG': '1'})
        app = subprocess.Popen([str(binary), url], env=environment, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, start_new_session=True)
        receipt['appPID'] = app.pid
        receipt['appStartedAt'] = utc()
        app_log = BoundedLog(app.stdout, destination / 'lumen.log')
        phase('functional', app.pid)
        deadline = time.monotonic() + args.seconds + 12
        next_disk_check = 0
        idle_marked = False
        while app.poll() is None:
            if sampler.poll() is not None:
                raise RuntimeError('Telemetry exited during the owned native probe')
            if server.poll() is not None:
                raise RuntimeError('Companion server exited during the owned native probe')
            if app_log.error or server_log.error:
                raise RuntimeError(app_log.error or server_log.error)
            now = time.monotonic()
            if now >= next_disk_check:
                if free_mib(root) < 64:
                    raise RuntimeError('Free space fell below64 MiB; owned probe aborted')
                if tree_bytes(destination) > 32 * MIB:
                    raise RuntimeError('Task output exceeded32 MiB; owned probe aborted')
                next_disk_check = now + 1
            if app_log.cleanup_seen and not idle_marked:
                phase('idle', app.pid)
                idle_marked = True
            if now >= deadline:
                raise RuntimeError('Native screenshot/exit deadline exceeded')
            time.sleep(.25)
        receipt['appReturncode'] = app.returncode
    except (Exception, KeyboardInterrupt) as error:
        receipt['issues'].append(type(error).__name__ + ': ' + str(error))
    finally:
        # Cleanup comes before any final write, which might fail under disk pressure.
        try:
            phase('shutdown', app.pid if app else None)
        except OSError:
            pass
        receipt['appTermination'] = stop_owned(app)
        receipt['serverTermination'] = stop_owned(server)
        receipt['samplerTermination'] = stop_owned(sampler)
        if sampler_log:
            sampler_log.close()
        for reader in (app_log, server_log):
            if reader:
                reader.thread.join(timeout=2)
                if reader.error and reader.error not in receipt['issues']:
                    receipt['issues'].append(reader.error)
        if app_log:
            receipt['reports'] = app_log.reports
        if shot.is_file():
            with shot.open('rb') as image:
                header = image.read(24)
            if len(header) == 24 and header.startswith(b'\x89PNG\r\n\x1a\n'):
                receipt['screenshot'] = shot.name
                receipt['screenshotDimensions'] = list(struct.unpack('>II', header[16:24]))
        receipt['finishedAt'] = utc()
        receipt['remainingFreeMiB'] = round(free_mib(root), 3)
        receipt['outputBytes'] = tree_bytes(destination)
        atomic_json(destination / 'probe-receipt.json', receipt)
    print(json.dumps({'receipt': str(destination / 'probe-receipt.json'),
                      'reports': len(receipt['reports']), 'issues': receipt['issues'],
                      'screenshot': receipt.get('screenshot'), 'appReturncode': receipt.get('appReturncode')}))
    return 0 if not receipt['issues'] and receipt.get('appReturncode') == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
