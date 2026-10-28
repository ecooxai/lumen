#!/usr/bin/env python3
"""Bounded, existing-binary Mac run of the pinned six-case WPT-source subset.

This is not canonical wpt run, a full WPT rate, a release build, or iPad proof.
Only an owned server, isolated profiles and the exact selected binary are used.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import time
from urllib.parse import urlparse

from probe_existing_release import BoundedLog, atomic_json, free_mib, tree_bytes, stop_owned, MIB
QA_PATH = Path(__file__).resolve().parents[2] / 'scripts/qa-apple-gpt6'
sys.path.insert(0, str(QA_PATH))
from report_codec import decode_lines

HARNESS = ['OK', 'ERROR', 'TIMEOUT', 'PRECONDITION_FAILED']
SUBTEST = ['PASS', 'FAIL', 'TIMEOUT', 'NOTRUN', 'PRECONDITION_FAILED']


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(MIB), b''):
            value.update(block)
    return value.hexdigest()


def utc():
    return datetime.now(timezone.utc).isoformat()


def convert_result(raw, test_id):
    if not isinstance(raw, dict) or raw.get('wpt_test_id') != test_id:
        raise ValueError('Native report does not match the selected test URL')
    status = raw.get('status')
    if type(status) is not int or not 0 <= status < len(HARNESS):
        raise ValueError('Unknown WPT harness status')
    if not isinstance(raw.get('subtests'), list):
        raise ValueError('Native report lacks subtests')
    names, converted = set(), []
    for row in raw['subtests']:
        if not isinstance(row, dict) or not isinstance(row.get('name'), str):
            raise ValueError('Invalid WPT subtest identity')
        if row['name'] in names:
            raise ValueError('Duplicate WPT subtest identity')
        names.add(row['name'])
        status = row.get('status')
        if type(status) is not int or not 0 <= status < len(SUBTEST):
            raise ValueError('Unknown WPT subtest status')
        converted.append({'name': row['name'], 'status': SUBTEST[status],
                          'message': row.get('message'), 'stack': row.get('stack')})
    return {'test': test_id, 'status': HARNESS[raw['status']], 'message': raw.get('message'),
            'subtests': converted, 'console_detail_policy': raw.get('console_detail_policy')}


def retain_exit_failures(row, natural_exit, termination):
    result = dict(row)
    code = termination.get('returncode')
    if natural_exit not in (None, 0):
        result['status'] = 'CRASH' if natural_exit < 0 else 'ERROR'
        result['message'] = 'Native process exited unsuccessfully after/before report: ' + str(natural_exit)
    elif termination.get('forcedKill'):
        result['status'] = 'ERROR'
        result['message'] = 'Native process required forced termination after its report'
    elif code not in (0, -signal.SIGTERM):
        result['status'] = 'CRASH' if isinstance(code, int) and code < 0 else 'ERROR'
        result['message'] = 'Unexpected native process termination: ' + str(code)
    return result


class ServerLog(BoundedLog):
    def inspect(self, line):
        super().inspect(line)
        if not line.startswith('{'):
            return
        try:
            value = json.loads(line)
            parsed = urlparse(value.get('url', ''))
            if parsed.scheme == 'http' and parsed.hostname == '127.0.0.1' and parsed.port:
                self.port = parsed.port
        except (ValueError, TypeError):
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build-apple-gpt6as/lumen')
    parser.add_argument('--expected-sha256', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--seconds-per-test', type=int, default=25, choices=range(10, 46))
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('Native WPT probe requires the original Mac; no platform substitute')
    root = Path.cwd().resolve()
    binary = (root / args.binary).resolve(strict=True)
    output = args.output.resolve()
    if not binary.is_relative_to(root) or not output.is_relative_to(root / 'output') or output.exists():
        parser.error('Use a project binary and a new project output directory')
    if digest(binary) != args.expected_sha256:
        parser.error('Release binary hash differs from the reviewed source')
    subset = root / 'tools/wpt-reference-gpt6as'
    selection = json.loads((subset / 'selection.json').read_text())
    lock = json.loads((subset / 'source-lock.json').read_text())
    ids = selection['test_ids']
    if len(ids) != 6 or len(set(ids)) != 6 or selection['wpt_revision'] != lock['wpt_revision']:
        parser.error('Expected the exact six-case pinned selection')
    for file in lock['files']:
        source = (subset / 'upstream' / file['path']).resolve(strict=True)
        if not source.is_relative_to(subset / 'upstream') or digest(source) != file['sha256']:
            parser.error('Pinned upstream source changed')
    telemetry = root / 'scripts/qa-apple-gpt6/telemetry.py'
    server_script = subset / 'run-reference.cjs'
    node = shutil.which('node')
    if not node or not telemetry.is_file() or not server_script.is_file():
        parser.error('Existing Node/server/telemetry required; nothing will be installed')
    if free_mib(root) < 128:
        parser.error('At least128 MiB free is required before this bounded existing-binary probe')
    output.mkdir(parents=True, exist_ok=False)
    targets = output / 'targets.json'
    samples = output / 'process-samples.jsonl'
    selection_bytes = json.dumps({'test_ids': ids, 'wpt_revision': selection['wpt_revision']}, separators=(',', ':')).encode()
    started = int(time.time() * 1000)
    report = {'run_info': {'product': 'lumen', 'browser_version': 'sha256:' + args.expected_sha256,
        'os': platform.system().lower(), 'os_version': platform.release(), 'revision': selection['wpt_revision'],
        'selection_sha256': hashlib.sha256(selection_bytes).hexdigest(),
        'adapter_name': 'lume-fixed-wpt-window-subset', 'adapter_version': '1', 'official_wpt_run': False},
        'source_lock_sha256': digest(subset / 'source-lock.json'), 'adapter_sha256': digest(server_script),
        'selection_file_sha256': digest(subset / 'selection.json'), 'time_start': started, 'results': [],
        'scope': selection['scope'], 'limitations': selection['limitations'], 'binary_sha256': args.expected_sha256}
    receipt = {'schemaVersion': 1, 'startedUTC': utc(), 'buildPerformed': False, 'packageCreated': False,
        'scope': 'Native existing Release / six prespecified WPT-source window cases; not full WPT or Safari parity',
        'binarySHA256': args.expected_sha256, 'initialFreeMiB': round(free_mib(root), 3),
        'launchFloorMiB': 128, 'abortFloorMiB': 64, 'outputLimitMiB': 32, 'logLimitMiB': 1,
        'serverSHA256': digest(server_script), 'runnerSHA256': digest(__file__),
        'supportSHA256': digest(Path(__file__).with_name('probe_existing_release.py')),
        'reportCodecSHA256': digest(QA_PATH / 'report_codec.py'),
        'telemetrySHA256': digest(telemetry), 'cases': [], 'issues': []}
    app = sampler = server = None
    server_log = None
    sampler_handle = None

    def phase(name, pid=None):
        atomic_json(targets, {'phase': name, 'groups': [{'name': 'lumen', 'pids': [pid]}] if pid else []})

    def check_budget():
        if free_mib(root) < 64 or tree_bytes(output) > 32 * MIB:
            raise RuntimeError('Native probe exceeded its free-space/output safety bounds')
        if sampler is not None and sampler.poll() is not None:
            raise RuntimeError('One-second telemetry ended before native tests')
        if server is not None and (server.poll() is not None or server_log.error):
            raise RuntimeError('Owned WPT-source server failed')

    def interrupt(_signum, _frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, interrupt)
    try:
        phase('baseline')
        server = subprocess.Popen([node, str(server_script), '--serve-only', '--port', '0'],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
        server_log = ServerLog(server.stdout, output / 'server.log')
        deadline = time.monotonic() + 8
        while server_log.port is None and time.monotonic() < deadline:
            check_budget(); time.sleep(.1)
        if not server_log.port:
            raise RuntimeError('Owned pinned WPT server did not report its loopback port')
        sampler_handle = (output / 'sampler.log').open('x')
        sampler = subprocess.Popen([sys.executable, str(telemetry), 'collect', '--targets', str(targets),
            '--output', str(samples), '--seconds', str(6 * (args.seconds_per_test + 10) + 30)],
            stdin=subprocess.DEVNULL, stdout=sampler_handle, stderr=subprocess.STDOUT, start_new_session=True)
        time.sleep(1.1)
        for index, test_id in enumerate(ids):
            check_budget()
            if free_mib(root) < 128:
                raise RuntimeError('Free space dropped below the128 MiB next-case launch gate')
            case_dir = output / ('case-' + str(index + 1))
            case_dir.mkdir()
            profile = case_dir / 'profile'; profile.mkdir()
            environment = dict(os.environ)
            # Do not inherit a prior screenshot, custom host mapping, URL, or
            # browser profile. Keep task execution on this owned loopback server.
            for key in list(environment):
                if key.startswith('LUMEN_'):
                    environment.pop(key)
            environment.update({'LUMEN_PROFILE_DIR': str(profile), 'LUMEN_CONSOLE': '1',
                'LUMEN_WINDOW': '1200x900', 'LUMEN_NO_SIDEBAR': '1', 'LUMEN_CPU_DEBUG': '1'})
            url = f'http://127.0.0.1:{server_log.port}{test_id}'
            begin = time.monotonic()
            app = subprocess.Popen([str(binary), url], env=environment, stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
            app_log = BoundedLog(app.stdout, case_dir / 'lumen.log')
            case = {'test': test_id, 'pid': app.pid, 'startedUTC': utc(), 'issues': []}
            receipt['cases'].append(case)
            phase('wpt-active-' + str(index + 1), app.pid)
            raw = None
            while time.monotonic() - begin < args.seconds_per_test:
                check_budget()
                if app_log.error:
                    raise RuntimeError(app_log.error)
                matching = [entry['value'] for entry in app_log.reports
                            if isinstance(entry.get('value'), dict) and entry['value'].get('wpt_test_id') == test_id]
                if len(matching) > 1:
                    raise RuntimeError('Conflicting or duplicate native WPT completion reports')
                if matching:
                    raw = matching[0]
                    if time.monotonic() - begin >= 1.2:
                        break
                if app.poll() is not None:
                    break
                time.sleep(.1)
            case['naturalExitBeforeCleanup'] = app.poll()
            case['termination'] = stop_owned(app)
            app_log.thread.join(timeout=2)
            if app_log.error:
                case['issues'].append(app_log.error)
            if app_log.thread.is_alive():
                case['issues'].append('Native log reader did not reach EOF')
            # The live collector is a completion signal only. Decode the saved
            # raw log again after EOF so a late conflicting chunk cannot pass.
            with (case_dir / 'lumen.log').open(errors='replace') as stream:
                decoded = decode_lines(stream)
            if decoded['errors']:
                case['issues'].append('Native report transport errors: ' + json.dumps(decoded['errors']))
            matching = [entry['value'] for entry in decoded['reports']
                        if isinstance(entry.get('value'), dict) and entry['value'].get('wpt_test_id') == test_id]
            raw = matching[0] if len(matching) == 1 else None
            if len(matching) > 1:
                case['issues'].append('Multiple complete WPT reports in the final raw log')
            if raw is not None and not case['issues']:
                row = convert_result(raw, test_id)
                row = retain_exit_failures(row, case['naturalExitBeforeCleanup'], case['termination'])
            else:
                row = {'test': test_id, 'status': 'ERROR' if case['issues'] or case['naturalExitBeforeCleanup'] is not None else 'TIMEOUT',
                       'message': '; '.join(case['issues']) or 'Native app supplied no complete WPT result before exit/deadline', 'subtests': []}
            row['duration'] = round((time.monotonic() - begin) * 1000)
            report['results'].append(row)
            atomic_json(case_dir / 'case.json', case)
            atomic_json(output / 'wpt-report.json', report)
            app = None
            phase('between-cases')
    except (Exception, KeyboardInterrupt) as error:
        receipt['issues'].append(type(error).__name__ + ': ' + str(error))
    finally:
        receipt['appTermination'] = stop_owned(app)
        receipt['serverTermination'] = stop_owned(server)
        receipt['samplerTermination'] = stop_owned(sampler)
        if sampler_handle:
            sampler_handle.close()
        if server_log:
            server_log.thread.join(timeout=2)
            if server_log.error:
                receipt['issues'].append(server_log.error)
        seen = {row['test'] for row in report['results']}
        for test_id in ids:
            if test_id not in seen:
                report['results'].append({'test': test_id, 'status': 'NOTRUN',
                    'message': 'Not executed because the bounded native run was interrupted: ' + '; '.join(receipt['issues']), 'subtests': []})
        report['time_end'] = int(time.time() * 1000)
        receipt['finishedUTC'] = utc()
        receipt['remainingFreeMiB'] = round(free_mib(root), 3)
        receipt['outputBytes'] = tree_bytes(output)
        atomic_json(output / 'wpt-report.json', report)
        atomic_json(output / 'receipt.json', receipt)
    summary = [{'test': row['test'], 'status': row['status'], 'pass': sum(x['status'] == 'PASS' for x in row['subtests']),
                'subtests': len(row['subtests'])} for row in report['results']]
    print(json.dumps({'report': str(output / 'wpt-report.json'), 'summary': summary, 'issues': receipt['issues']}))
    return 0 if not receipt['issues'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
