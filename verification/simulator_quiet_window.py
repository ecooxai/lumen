#!/usr/bin/env python3
"""Temporarily stop only the two designated simulators; independently restore.

No erase, uninstall, preference change, shutdown-all, or daemon signaling is
used. A durable journal precedes mutation. A separately sessioned watchdog
restores the original target states if the controller exits or its lease ends.
"""
import argparse
import datetime as dt
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import stat
import subprocess
import sys
import tempfile
import time

ROOT = Path('/Users/devin/repos/lumen').resolve()
TARGETS = ('EDD9B083-63F1-44E6-9A7B-0A6964CC2561', '590ACCA9-0BA2-4803-970F-EF3DAC515255')
EXPECTED = 'b7e581006bd0afc77aabddfcf0348478c3baa59c8d996101cd5c9609efd20b73'
UNRELATED = 'E0224A22-F874-4B8C-83FB-EB084492C493'


def require(value, message):
    if not value:
        raise RuntimeError(message)


def atomic(path, value):
    descriptor, temporary = tempfile.mkstemp(prefix=path.name + '.', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w') as stream:
            json.dump(value, stream, sort_keys=True)
            stream.write('\n'); stream.flush(); os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def read_json(path):
    return json.loads(path.read_text())


def best_effort_record(path, value):
    try:
        atomic(path, value)
        return True
    except OSError:
        return False


def not_cancelled(run_dir):
    require(not (run_dir / 'cancel.json').exists(), 'Watchdog cancelled this quiet window')


def query(args, timeout=10):
    try:
        result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, timeout=timeout)
        return result.returncode, result.stdout
    except subprocess.TimeoutExpired:
        return 'timeout', ''


def states():
    code, raw = query(['xcrun', 'simctl', 'list', 'devices', '--json'])
    require(code == 0, 'Cannot read Simulator boot states')
    return {device['udid']: device['state'] for group in json.loads(raw)['devices'].values() for device in group}


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            value.update(block)
    return value.hexdigest()


def cpu_seconds(value):
    days = 0
    if '-' in value:
        days, value = value.split('-', 1); days = int(days)
    seconds = 0.0
    for field in value.split(':'):
        seconds = seconds * 60 + float(field)
    return days * 86400 + seconds


def processes():
    code, raw = query(['/bin/ps', '-axww', '-o', 'pid=,ppid=,uid=,lstart=,stat=,time=,rss=,comm='], 4)
    require(code == 0, 'Cannot read process health')
    rows = {}
    for line in raw.splitlines():
        fields = line.split(None, 11)
        if len(fields) == 12:
            try:
                rows[int(fields[0])] = {'pid': int(fields[0]), 'ppid': int(fields[1]),
                    'uid': int(fields[2]), 'started': ' '.join(fields[3:8]), 'state': fields[8],
                    'cpu': cpu_seconds(fields[9]), 'rssKiB': int(fields[10]), 'name': Path(fields[11]).name}
            except ValueError:
                pass
    return rows


def identity(row):
    return {key: row[key] for key in ('pid', 'uid', 'started', 'name')}


def controller_alive(expected):
    row = processes().get(expected['pid'])
    return bool(row and not row['state'].startswith('Z') and identity(row) == expected)


def container(udid, kind):
    code, raw = query(['xcrun', 'simctl', 'get_app_container', udid, 'com.ecooxai.lumen', kind])
    require(code == 0 and raw.strip().startswith('/'), 'Cannot read the designated app container')
    path = Path(raw.strip()).resolve()
    require(path.is_dir(), 'Designated container is absent')
    return path


def sentinels(udid):
    app, data = container(udid, 'app'), container(udid, 'data')
    binary_sha = digest(app / 'Lumen')
    require(binary_sha == EXPECTED, 'Designated app is not the accepted binary')
    files = []
    documents = data / 'Documents'
    # Only a bounded sample of ordinary app Documents is hashed; names/content
    # stay in the private journal and are never copied to the printed report.
    if documents.is_dir() and not documents.is_symlink():
        count = 0
        for directory, subdirs, names in os.walk(documents, followlinks=False):
            subdirs[:] = sorted(name for name in subdirs if not (Path(directory) / name).is_symlink())
            for name in sorted(names):
                count += 1
                path = Path(directory) / name
                info = path.lstat()
                if stat.S_ISREG(info.st_mode) and info.st_size <= 1024 * 1024:
                    files.append({'relative': str(path.relative_to(data)), 'bytes': info.st_size, 'sha256': digest(path)})
                if len(files) >= 16 or count >= 128:
                    break
            if len(files) >= 16 or count >= 128:
                break
    info = data.stat()
    return {'app': str(app), 'data': str(data), 'dataDevice': info.st_dev, 'dataInode': info.st_ino,
            'binarySHA256': binary_sha, 'documentsFiles': files}


def verify_sentinels(udid, saved):
    app, data = container(udid, 'app'), container(udid, 'data')
    info = data.stat()
    checks = {'sameAppContainer': str(app) == saved['app'], 'sameDataContainer': str(data) == saved['data'],
              'sameDataDirectoryIdentity': info.st_dev == saved['dataDevice'] and info.st_ino == saved['dataInode'],
              'binaryUnchanged': digest(app / 'Lumen') == saved['binarySHA256'],
              'documentSentinelCount': len(saved['documentsFiles']), 'documentSentinelsUnchanged': True}
    for record in saved['documentsFiles']:
        path = data / record['relative']
        if not path.is_file() or path.is_symlink() or path.stat().st_size != record['bytes'] or digest(path) != record['sha256']:
            checks['documentSentinelsUnchanged'] = False
    checks['pass'] = all(checks[key] for key in ('sameAppContainer', 'sameDataContainer',
        'sameDataDirectoryIdentity', 'binaryUnchanged', 'documentSentinelsUnchanged'))
    return checks


def memory_disk():
    pressure_code, raw = query(['/usr/bin/memory_pressure', '-Q'], 4)
    match = re.search(r'System-wide memory free percentage:\s*(\d+)%', raw)
    swap_code, swap_raw = query(['/usr/sbin/sysctl', 'vm.swapusage'], 4)
    vm_code, vm_raw = query(['/usr/bin/vm_stat'], 4)
    counters = {}
    for name, value in re.findall(r'^(Compressions|Decompressions|Swapins|Swapouts|Pageins|Pageouts):\s*(\d+)\.', vm_raw, re.M):
        counters[name] = int(value)
    disk = os.statvfs(ROOT)
    return {'memoryPressureQueryStatus': pressure_code, 'systemFreePercentage': int(match[1]) if match else None,
            'swapQueryStatus': swap_code, 'swap': {name: {'value': float(value), 'unit': unit}
                for name, value, unit in re.findall(r'(total|used|free)\s*=\s*(\d+(?:\.\d+)?)([KMG])', swap_raw)},
            'vmStatStatus': vm_code, 'vmCounters': counters,
            'mainDiskAvailableBytes': disk.f_bavail * disk.f_frsize}


def health_window(run_dir, seconds, label):
    samples = []
    previous, last = processes(), time.monotonic()
    due, start = last + 1, last
    for index in range(seconds):
        not_cancelled(run_dir)
        time.sleep(max(0, due - time.monotonic()))
        current, now = processes(), time.monotonic()
        elapsed = now - last
        values = []
        for pid, row in current.items():
            old = previous.get(pid)
            if old and all(old[key] == row[key] for key in ('uid', 'started', 'name')) and row['cpu'] >= old['cpu']:
                values.append((row, (row['cpu'] - old['cpu']) / elapsed * 100))
        samples.append({'elapsedSeconds': round(now - start, 3), 'intervalSeconds': round(elapsed, 3),
            'cpuPercentOfOneCore': round(sum(value for row, value in values), 3),
            'processCount': len(current), 'runnable': sum(row['state'].startswith('R') for row in current.values()),
            'uninterruptible': sum(row['state'].startswith(('U', 'D')) for row in current.values()),
            'topCPU': [{'pid': row['pid'], 'ppid': row['ppid'], 'name': row['name'], 'uid': row['uid'],
                        'cpuIntervalPercent': round(value, 3), 'rssMiB': round(row['rssKiB'] / 1024, 3)}
                       for row, value in sorted(values, key=lambda pair: pair[1], reverse=True)[:8]],
            'lumenProcesses': sum(row['name'].lower() in ('lumen', 'lumen-jit', 'lumen-jitless') for row in current.values())})
        previous, last, due = current, now, due + 1
        atomic(run_dir / (label + '-health.json'), {'phase': label, 'samples': samples,
               'finished': index == seconds - 1})
    not_cancelled(run_dir)
    measured = samples[10:] if seconds > 15 else samples
    return {'sampleCount': len(samples), 'settleSamplesExcluded': len(samples) - len(measured),
            'meanCPUPercentOfOneCore': sum(item['cpuPercentOfOneCore'] for item in measured) / len(measured),
            'peakCPUPercentOfOneCore': max(item['cpuPercentOfOneCore'] for item in measured),
            'runnableFinal': samples[-1]['runnable'], 'loadAverage': list(os.getloadavg())}


class ActionLock:
    def __init__(self, run_dir, limit=90):
        self.path, self.limit = run_dir / 'lifecycle.lock', limit
    def __enter__(self):
        self.stream = self.path.open('a')
        deadline = time.monotonic() + self.limit
        while True:
            try:
                fcntl.flock(self.stream, fcntl.LOCK_EX | fcntl.LOCK_NB); return self
            except BlockingIOError:
                require(time.monotonic() < deadline, 'Lifecycle lock remained held beyond its bound')
                time.sleep(0.5)
    def __exit__(self, *unused):
        fcntl.flock(self.stream, fcntl.LOCK_UN); self.stream.close()


def transition(udid, desired, deadline):
    require(udid in TARGETS and desired in ('Booted', 'Shutdown'), 'Refusing a non-designated lifecycle target')
    while time.monotonic() < deadline:
        state = states().get(udid)
        if desired == 'Shutdown' and state == 'Shutdown':
            return {'state': state, 'bootReady': False}
        if desired == 'Booted' and state == 'Booted':
            code, unused = query(['xcrun', 'simctl', 'bootstatus', udid, '-b'], min(50, max(1, deadline - time.monotonic())))
            if code == 0:
                return {'state': 'Booted', 'bootReady': True}
        elif state == 'Booted' and desired == 'Shutdown':
            query(['xcrun', 'simctl', 'shutdown', udid], min(30, max(1, deadline - time.monotonic())))
        elif state == 'Shutdown' and desired == 'Booted':
            query(['xcrun', 'simctl', 'boot', udid], min(30, max(1, deadline - time.monotonic())))
        elif state not in ('Booting', 'Shutting Down', 'ShuttingDown'):
            raise RuntimeError('Unexpected designated Simulator state: ' + str(state))
        time.sleep(0.5)
    raise RuntimeError('Bounded normal Simulator transition did not complete: ' + udid + ' to ' + desired)


def restore(run_dir, actor):
    journal = read_json(run_dir / 'journal.json')
    require(tuple(journal['targets']) == TARGETS, 'Restoration target journal was altered')
    result = {'actor': actor, 'started': time.time(), 'devices': {}, 'errors': []}
    try:
        with ActionLock(run_dir, 330):
            for udid in TARGETS:
                try:
                    result['devices'][udid] = transition(udid, journal['initialStates'][udid], time.monotonic() + 120)
                except Exception as error:
                    result['errors'].append(type(error).__name__ + ': ' + str(error))
            after = states()
            result['targetStatesRestored'] = all(after.get(udid) == journal['initialStates'][udid] for udid in TARGETS)
            result['unrelatedStatesPreserved'] = all(after.get(udid) == value
                for udid, value in journal['initialStates'].items() if udid not in TARGETS)
            result['unrelatedDeviceStillBooted'] = after.get(UNRELATED) == journal['initialStates'].get(UNRELATED) == 'Booted'
            result['sentinels'] = {}
            for udid in TARGETS:
                try:
                    result['sentinels'][udid] = verify_sentinels(udid, journal['sentinels'][udid])
                except Exception as error:
                    result['errors'].append(type(error).__name__ + ': ' + str(error))
            result['sentinelsPreserved'] = len(result['sentinels']) == 2 and all(value['pass'] for value in result['sentinels'].values())
            result['pass'] = (result['targetStatesRestored'] and result['unrelatedStatesPreserved']
                and result['sentinelsPreserved'] and not result['errors'])
            scenario = ROOT / 'out/verification/monitor-scenario.txt'
            if scenario.exists() and scenario.read_text().startswith(journal['scenarioPrefix']):
                scenario.write_text(journal['scenarioBefore'])
    except Exception as error:
        result['errors'].append(type(error).__name__ + ': ' + str(error)); result['pass'] = False
    result['finished'] = time.time()
    if not best_effort_record(run_dir / ('restoration-' + actor + '.json'), result):
        result['reportWriteFailed'] = True
    return result


def controller(run_dir):
    report = {'schema': 1, 'running': True, 'phase': 'preflight', 'started': time.time(),
              'devicesAffected': list(TARGETS), 'unrelatedDeviceModified': False}
    report_path = run_dir / 'report.json'
    atomic(report_path, report)
    journal_ready = False
    watchdog = None
    try:
        require(Path.cwd().resolve() == ROOT, 'Unexpected repository')
        ui = read_json(ROOT / 'out/native-verification/apple-rtc-ui.json')
        require(ui.get('pass') is True and not ui.get('running'), 'Apple UI must finish successfully before isolation')
        monitor = ROOT / 'out/verification/resource-monitor.jsonl'
        require(monitor.is_file() and time.time() - monitor.stat().st_mtime < 6, 'Resource monitor must be fresh')
        rows = processes()
        forbidden = {'lumen', 'xcodebuild', 'xctest', 'clang', 'clang++', 'rustc', 'cargo', 'ninja', 'swift-frontend'}
        require(not any(row['name'].lower() in forbidden for row in rows.values()), 'An application/compiler/test is still active')
        initial = states()
        require(all(initial.get(udid) == 'Booted' for udid in TARGETS), 'Both designated targets must initially be Booted')
        require(initial.get(UNRELATED) == 'Booted', 'The unrelated device state changed before isolation')
        controller_identity = identity(rows[os.getpid()])
        scenario = ROOT / 'out/verification/monitor-scenario.txt'
        journal = {'schema': 1, 'targets': list(TARGETS), 'initialStates': initial,
            'sentinels': {udid: sentinels(udid) for udid in TARGETS},
            'controllerIdentity': controller_identity, 'deadlineUTC': time.time() + 600,
            'scenarioPrefix': 'sim-quiet-' + run_dir.name,
            'scenarioBefore': scenario.read_text() if scenario.exists() else '', 'created': time.time()}
        atomic(run_dir / 'journal.json', journal); journal_ready = True
        with (run_dir / 'watchdog.log').open('wb') as log:
            watchdog = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), '--watchdog', str(run_dir)],
                cwd=ROOT, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        armed = run_dir / 'watchdog-status.json'
        until = time.monotonic() + 8
        while not armed.exists() and watchdog.poll() is None and time.monotonic() < until:
            time.sleep(0.1)
        require(armed.is_file() and read_json(armed).get('armed') is True and watchdog.poll() is None,
                'Independent restoration watchdog did not arm; no shutdown attempted')
        report['watchdogPID'] = watchdog.pid
        report['phase'] = 'baseline'; atomic(report_path, report)
        scenario.write_text(journal['scenarioPrefix'] + '-baseline\n')
        report['memoryBefore'] = memory_disk()
        report['baseline'] = health_window(run_dir, 5, 'baseline')
        report['phase'] = 'shutdown-designated-only'; atomic(report_path, report)
        with ActionLock(run_dir):
            for udid in TARGETS:
                not_cancelled(run_dir)
                transition(udid, 'Shutdown', time.monotonic() + 60)
        quiet = states()
        require(all(quiet.get(udid) == 'Shutdown' for udid in TARGETS), 'Designated targets did not both shut down')
        require(all(quiet.get(udid) == value for udid, value in initial.items() if udid not in TARGETS),
                'Unrelated Simulator state changed; quiet observation stopped')
        report['phase'] = 'quiet-health'; report['quietTargetStates'] = {udid: quiet[udid] for udid in TARGETS}
        atomic(report_path, report); scenario.write_text(journal['scenarioPrefix'] + '-quiet\n')
        report['quiet'] = health_window(run_dir, 35, 'quiet')
        not_cancelled(run_dir)
        report['memoryQuiet'] = memory_disk()
        report['phase'] = 'quiet-ready'
        report['releaseFile'] = str(run_dir / 'release.json')
        report['holdEndsUTC'] = journal['deadlineUTC'] - 150
        report['watchdogDeadlineUTC'] = journal['deadlineUTC']
        atomic(report_path, report)
        # The parent can use this finite interval for reserved Mac gates and
        # release it early. Browser performance claims require their own scope.
        while time.time() < report['holdEndsUTC'] and not (run_dir / 'release.json').exists():
            not_cancelled(run_dir)
            require(time.time() - monitor.stat().st_mtime < 10, 'Resource monitor became stale')
            require(watchdog.poll() is None, 'Restoration watchdog stopped unexpectedly')
            time.sleep(1)
        not_cancelled(run_dir)
    except BaseException as error:
        report['error'] = type(error).__name__ + ': ' + str(error)
    finally:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        if journal_ready:
            report['phase'] = 'restoring-original-targets'
            best_effort_record(report_path, report)
            report['restoration'] = restore(run_dir, 'controller')
        report['running'] = False; report['finished'] = time.time()
        report['phase'] = 'complete' if report.get('restoration', {}).get('pass') else 'restoration-needs-attention'
        report['pass'] = bool(report.get('quiet') and report.get('restoration', {}).get('pass')
            and not report.get('error') and not (run_dir / 'cancel.json').exists())
        report['scope'] = 'Host resource isolation only; no browser feature or performance acceptance.'
        best_effort_record(report_path, report)


def watchdog(run_dir):
    journal = read_json(run_dir / 'journal.json')
    require(tuple(journal['targets']) == TARGETS, 'Watchdog target scope differs')
    atomic(run_dir / 'watchdog-status.json', {'armed': True, 'pid': os.getpid(), 'deadlineUTC': journal['deadlineUTC']})
    reason = None
    while reason is None:
        try:
            report = read_json(run_dir / 'report.json')
            if report.get('running') is False:
                if report.get('restoration', {}).get('pass') is True:
                    current = states()
                    if all(current.get(udid) == journal['initialStates'][udid] for udid in TARGETS):
                        atomic(run_dir / 'watchdog-status.json', {'armed': False, 'finished': True,
                            'controllerRestorationIndependentlyConfirmed': True}); return
                reason = 'controller-finished-without-verified-restoration'
            elif not controller_alive(journal['controllerIdentity']):
                reason = 'controller-identity-no-longer-live'
            elif time.time() >= journal['deadlineUTC']:
                reason = 'controller-lease-ended'
        except Exception as error:
            reason = 'watchdog-observation-failed: ' + type(error).__name__
        if reason is None:
            time.sleep(2)
    # Cancellation is durable before recovery and checked while the lifecycle
    # lock is held before every shutdown. It also invalidates quiet samples.
    best_effort_record(run_dir / 'cancel.json', {'reason': reason, 'utc': time.time()})
    try:
        if controller_alive(journal['controllerIdentity']):
            os.kill(journal['controllerIdentity']['pid'], signal.SIGTERM)
    except (OSError, RuntimeError):
        pass
    result = restore(run_dir, 'watchdog')
    best_effort_record(run_dir / 'watchdog-status.json', {'armed': False, 'finished': True, 'recoveryReason': reason,
        'restorationPass': result['pass'], 'restorationReport': 'restoration-watchdog.json'})


def main():
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--controller', type=Path); group.add_argument('--watchdog', type=Path)
    args = parser.parse_args()
    supplied = args.controller or args.watchdog
    require(not supplied.is_symlink(), 'Diagnostic directory may not be a symlink')
    run_dir = supplied.resolve()
    require(run_dir.parent == ROOT / 'out/native-verification' and run_dir.name.startswith('sim-quiet-')
            and run_dir.is_dir() and not run_dir.is_symlink(), 'Invalid owned diagnostic directory')
    if args.controller:
        controller_lock = (ROOT / 'out/native-verification/simulator-quiet-controller.lock').open('a')
        fcntl.flock(controller_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        def interrupted(signum, unused):
            raise KeyboardInterrupt('Owned quiet-window controller interrupted')
        signal.signal(signal.SIGTERM, interrupted); signal.signal(signal.SIGINT, interrupted)
        controller(run_dir)
    else:
        watchdog(run_dir)


if __name__ == '__main__':
    main()
