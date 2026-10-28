#!/usr/bin/env python3
"""Diagnose owned Chrome teardown with a bounded root-process stack sample.

This is diagnostic evidence, not another WPT cohort or a cleanup override.
Existing Chrome instances are never selected or signalled by this script.
"""
import argparse
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import threading
import time
from types import SimpleNamespace

import full_runner as core


def group_snapshot(group):
    # Do not read arguments/environment. Retain only this fresh launch's PGID.
    raw = subprocess.check_output(['ps', '-A', '-ww', '-o',
                                   'pid=,ppid=,pgid=,uid=,ruid=,stat=,lstart=,comm='],
                                  text=True, timeout=5)
    members = []
    for line in raw.splitlines():
        fields = line.split(None, 11)
        if len(fields) != 12 or not fields[2].isdigit() or int(fields[2]) != group:
            continue
        members.append({'pid': int(fields[0]), 'ppid': int(fields[1]), 'pgid': int(fields[2]),
                        'uid': int(fields[3]), 'ruid': int(fields[4]), 'stat': fields[5],
                        'start_identity': ' '.join(fields[6:11]), 'executable': fields[11]})
    return members


class Observer:
    def __init__(self, group, path):
        self.group, self.path = group, path
        self.stop = threading.Event()
        self.phase = 'tests'
        self.errors = []
        self.began = time.monotonic()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        try:
            with self.path.open('w') as output:
                while not self.stop.is_set():
                    row = {'elapsed_s': round(time.monotonic() - self.began, 3),
                           'phase': self.phase, 'members': group_snapshot(self.group)}
                    output.write(json.dumps(row, ensure_ascii=True) + '\n')
                    output.flush()
                    self.stop.wait(.25)
        except Exception as exc:
            self.errors.append(type(exc).__name__ + ': ' + str(exc))

    def finish(self):
        self.stop.set()
        self.thread.join(timeout=6)
        if self.thread.is_alive():
            self.errors.append('Observer did not finish within its bounded ps timeout')


class StackSampler:
    """Sample only the verified root of this fresh Chrome Popen instance."""
    def __init__(self, chrome, directory):
        self.record = {
            'diagnostic_only': True, 'status': 'not_started', 'errors': [],
            'requested_duration_s': 8, 'sampling_interval_ms': 10,
            'child_timeout_s': 14, 'sent_sigterm': False, 'sent_sigkill': False,
            'scope': 'Only this freshly launched Chromium root; no user browser or other process.',
            'measurement_limit': 'Stack sampling adds diagnostic overhead; these resource measurements are excluded from comparison aggregates.'}
        self.process = self.thread = self.log = None
        self.path = directory / 'chromium-teardown.sample.txt'
        try:
            if chrome.process.poll() is not None:
                raise RuntimeError('Owned Chromium root already exited before sampling')
            expected = chrome.sampler.root_identity
            if not expected:
                raise RuntimeError('No independently recorded Chrome root start identity')
            matches = [row for row in group_snapshot(chrome.process.pid)
                       if row['pid'] == chrome.process.pid]
            if len(matches) != 1:
                raise RuntimeError('Owned Chromium root is absent from its process group')
            root = matches[0]
            if root['start_identity'] != expected or root['pgid'] != chrome.process.pid or \
                    root['uid'] != os.getuid() or root['ruid'] != os.getuid() or \
                    root['stat'].startswith('Z') or \
                    Path(root['executable']).resolve(strict=True) != Path(chrome.process.args[0]).resolve(strict=True):
                raise RuntimeError('Chrome PID/start/group/user/executable verification failed')
            self.record['verified_target'] = root
            command = ['/usr/bin/sample', str(chrome.process.pid), '8', '10',
                       '-mayDie', '-file', str(self.path)]
            self.record['argv'] = command
            self.record['started_unix_ms'] = round(time.time() * 1000)
            self.log = (directory / 'chromium-teardown.sample-tool.log').open('wb')
            self.process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                            stdout=self.log, stderr=subprocess.STDOUT,
                                            start_new_session=True)
            self.record.update(status='running', sample_tool_pid=self.process.pid)
            self.thread = threading.Thread(target=self._wait, daemon=True)
            self.thread.start()
        except Exception as exc:
            self.record['status'] = 'failed'
            self.record['errors'].append(type(exc).__name__ + ': ' + str(exc))
            if self.process is not None:
                self._stop_child()
            if self.log:
                try:
                    self.log.close()
                except Exception as close_error:
                    self.record['errors'].append('Sampler log close: ' + str(close_error))

    def _stop_child(self):
        # This unreaped Popen child is the sampler, never the Chrome target.
        # Its PID cannot be reused while it remains our unreaped child.
        try:
            if self.process.poll() is None:
                self.process.terminate()
                self.record['sent_sigterm'] = True
                try:
                    self.process.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.record['sent_sigkill'] = True
                    self.process.wait(timeout=2)
        except Exception as exc:
            self.record['errors'].append('Sampler cleanup: ' + type(exc).__name__ + ': ' + str(exc))

    def _wait(self):
        try:
            self.process.wait(timeout=14)
        except subprocess.TimeoutExpired:
            self.record['errors'].append('sample exceeded its 14-second diagnostic budget')
            self.record['timed_out'] = True
            self._stop_child()
        except Exception as exc:
            self.record['errors'].append(type(exc).__name__ + ': ' + str(exc))
            self._stop_child()
        finally:
            code = None
            try:
                code = self.process.poll()
            except Exception as exc:
                self.record['errors'].append('Sampler exit verification: ' + type(exc).__name__ + ': ' + str(exc))
            self.record['sample_tool_exit_code'] = code
            self.record['finished_unix_ms'] = round(time.time() * 1000)
            if self.log:
                try:
                    self.log.close()
                except Exception as exc:
                    self.record['errors'].append('Sampler log close: ' + str(exc))
            if code != 0:
                self.record['errors'].append('sample tool exited with status ' + str(code))
            self.record['status'] = 'failed' if self.record['errors'] else 'complete'

    def finish(self):
        try:
            if self.thread:
                self.thread.join(timeout=18)
                if self.thread.is_alive():
                    self.record['errors'].append('Sampler management thread did not finish within its bound')
            if self.path.is_file():
                self.record.update(output=str(self.path), output_bytes=self.path.stat().st_size,
                                   output_sha256=core.file_digest(self.path))
                if self.record['output_bytes'] == 0:
                    self.record['errors'].append('The stack sample file is empty')
            else:
                self.record['errors'].append('No stack sample file was produced')
        except Exception as exc:
            self.record['errors'].append('Sampler finalization: ' + type(exc).__name__ + ': ' + str(exc))
        if self.record['errors']:
            self.record['status'] = 'failed'
        return self.record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-run', type=Path, required=True,
                        help='Existing six-case Chromium run supplying verified schedule and fixture arguments')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--chromium-arg', action='append', default=[],
                        help='Additional diagnostic-only profile flag, recorded separately from reference arguments')
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('Execute only on the authorized Mac through devinmac3d')
    reference = json.loads((args.reference_run / 'run.json').read_text())
    signature = reference['signature']
    if signature.get('engine') != 'chromium':
        raise ValueError('Reference run is not Chromium')
    schedule = core.verified_schedule(args.reference_run, signature)
    if len(schedule) != 6 or any(row['type'] != 'testharness' or not row['test'].startswith('/url/') for row in schedule):
        raise ValueError('Use the exact bounded six-URL Chromium reference run')
    binary = Path(reference['binary']).resolve(strict=True)
    if core.file_digest(binary) != signature['binary_sha256']:
        raise ValueError('Chromium changed since the reference run')
    core.verify_reporter_route(signature['origin'])
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    report = {'diagnostic_only': True, 'reference_run': str(args.reference_run.resolve()),
              'core_runner_sha256': core.file_digest(Path(core.__file__)),
              'diagnostic_sha256': core.file_digest(Path(__file__)), 'binary_sha256': signature['binary_sha256'],
              'cases': [], 'original_cleanup': None,
              'reference_chromium_args': signature.get('chromium_args', []),
              'additional_chromium_args': args.chromium_arg,
              'effective_chromium_args': [*signature.get('chromium_args', []), *args.chromium_arg]}
    chrome = observer = stack_sampler = None

    def interrupt(signum, _frame):
        raise KeyboardInterrupt('Diagnostic interrupted by signal ' + str(signum))

    handlers = {sig: signal.signal(sig, interrupt) for sig in (signal.SIGINT, signal.SIGTERM)}
    try:
        chrome = core.Chromium(binary, args.output / 'chromium-session', report['effective_chromium_args'])
        report.update(owned_pid=chrome.process.pid, browser_version=chrome.version)
        observer = Observer(chrome.process.pid, args.output / 'process-states.jsonl')
        options = SimpleNamespace(**{key: signature[key] for key in ('origin', 'secure_origin', 'h2_origin')})
        for index, test in enumerate(schedule):
            timeout = signature['long_timeout_s'] if test.get('timeout') == 'long' else signature['timeout_s']
            result = chrome.run(core.test_url(test, options), timeout, args.output / ('case-' + str(index)))
            report['cases'].append({'test': test['test'], 'status': result['status'],
                                    'subtest_count': result.get('subtest_count'), 'cleanup': result.get('cleanup')})
            if result.get('cleanup', {}).get('error') or core.measurement_failure(result):
                report['case_gate_stopped'] = True
                break
    except BaseException as exc:
        report['error'] = type(exc).__name__ + ': ' + str(exc)
    finally:
        # Defer subsequent stop signals while exact-owned bounded cleanup runs.
        for sig in handlers:
            signal.signal(sig, lambda *_: None)
        try:
            if chrome is not None:
                if observer:
                    observer.phase = 'core_cleanup'
                stack_sampler = StackSampler(chrome, args.output)
                report['original_cleanup'] = chrome.close()
                if observer:
                    observer.phase = 'post_cleanup_observation_no_signals'
                began, deadline = time.monotonic(), time.monotonic() + 10
                while True:
                    members = group_snapshot(chrome.process.pid)
                    live = [member for member in members if not member['stat'].startswith('Z')]
                    if not live or time.monotonic() >= deadline:
                        report['post_cleanup_observation'] = {
                            'waited_s': round(time.monotonic() - began, 3), 'live_group_gone': not live,
                            'members': members, 'sent_additional_signals': False}
                        break
                    time.sleep(.25)
        except Exception as exc:
            report['diagnostic_cleanup_error'] = type(exc).__name__ + ': ' + str(exc)
        finally:
            if stack_sampler:
                report['stack_sample'] = stack_sampler.finish()
            if observer:
                observer.finish()
                report['observer_errors'] = observer.errors
            for sig, handler in handlers.items():
                signal.signal(sig, handler)
            core.write_json(args.output / 'diagnostic.json', report)
    print(json.dumps(report, indent=2, ensure_ascii=True))
    cleanup = report.get('original_cleanup') or {}
    return 2 if (report.get('error') or report.get('diagnostic_cleanup_error') or
                 report.get('observer_errors') or cleanup.get('errors') or
                 report.get('stack_sample', {}).get('status') != 'complete') else 0


if __name__ == '__main__':
    raise SystemExit(main())
