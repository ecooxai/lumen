#!/usr/bin/env python3
"""Run an unchanged trusted fixture and drain actual sealed-guard cleanup.

Versioned replacement for the earlier local fixture adapter. This records only
new real guard events; it never creates or repairs historical exit audits.
"""
from __future__ import annotations
import argparse, functools, hashlib, http.server, json, os, pathlib, subprocess
import sys, tempfile, threading, time

ROOT = pathlib.Path(__file__).resolve().parents[3] if pathlib.Path(__file__).parent.name == 'runtime' else pathlib.Path(__file__).resolve().parents[2]
VALIDATION = ROOT / 'agent/validation'
sys.path.insert(0, str(VALIDATION))
from native_capture_guard import guarded_popen
from paired_wpt import monitor, endmonitor
from run_guarded_cohort import cleanup_workers


def hashes(paths):
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=pathlib.Path)
    parser.add_argument('--fixture', required=True, type=pathlib.Path)
    parser.add_argument('--marker', required=True)
    parser.add_argument('--output', required=True, type=pathlib.Path)
    parser.add_argument('--timeout', type=float, default=25)
    parser.add_argument('--cleanup-timeout', type=float, default=5)
    parser.add_argument('--testdriver', action='store_true')
    args = parser.parse_args()
    if not 0 < args.timeout <= 300 or not 0 < args.cleanup_timeout <= 60:
        parser.error('timeouts must be positive; fixture at most300s, cleanup at most60s')
    binary = args.binary.resolve(strict=True); fixture = args.fixture.resolve(strict=True)
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    audit_name = os.environ.get('LUMEN_CAPTURE_GUARD_AUDIT_DIR')
    if not audit_name:
        parser.error('an explicit unique LUMEN_CAPTURE_GUARD_AUDIT_DIR is required')
    audit = pathlib.Path(audit_name).resolve() / 'launches.jsonl'
    if audit.exists() and audit.stat().st_size:
        parser.error('use a new empty guard audit directory for every fixture invocation')
    paths = [pathlib.Path(__file__).resolve(), VALIDATION / 'native_capture_guard.py',
             VALIDATION / 'run_guarded_cohort.py', VALIDATION / 'paired_wpt.py', fixture, binary]
    before = hashes(paths)
    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, fmt, *values):
            with (output / 'requests.log').open('a') as stream:
                stream.write((fmt % values) + '\n')
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Handler, directory=str(fixture.parent)))
    server.daemon_threads = True
    server_thread = threading.Thread(target=server.serve_forever, daemon=True); server_thread.start()
    marker = 'RESULTS ' + args.marker + ':'
    result = None; process = None; owned_monitor = None; failure = None
    started = time.monotonic(); workers = []; remaining = []; cleanup_started = None
    with tempfile.TemporaryDirectory(prefix='lumen-browser-test-') as profile:
        env = dict(os.environ)
        env.pop('LUMEN_PRE_FILE', None); env.pop('LUMEN_WPT_CTRL', None)
        env.update(XDG_DATA_HOME=profile + '/data', XDG_CONFIG_HOME=profile + '/config', XDG_CACHE_HOME=profile + '/cache',
                   LUMEN_NO_SESSION='1', LUMEN_CONSOLE='0', LUMEN_NO_GPU='1', LUMEN_WINDOW='800x640',
                   SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy')
        if args.testdriver: env['LUMEN_TESTDRIVER'] = '1'
        log_path = output / 'browser.log'
        try:
            with log_path.open('wb', buffering=0) as log:
                process = guarded_popen([str(binary), 'http://127.0.0.1:%d/%s' % (server.server_port, fixture.name)],
                                        env=env, stdin=subprocess.DEVNULL, stdout=log, stderr=log)
                owned_monitor = monitor(process.pid, output / 'tree-resources', 'trusted fixture with drained guard cleanup')
                deadline = time.monotonic() + args.timeout
                while time.monotonic() < deadline:
                    text = log_path.read_text(errors='replace')
                    if result is None:
                        for line in text.splitlines():
                            if marker in line:
                                result = line[line.index(marker):]; break
                        if 'JS prelude failed' in text:
                            result = marker + 'FAIL JavaScript bootstrap failed'
                    if result is not None and time.monotonic() - started >= 1.05: break
                    if process.poll() is not None: break
                    time.sleep(.05)
        except BaseException as error:
            failure = error
        finally:
            if process is not None and process.poll() is None:
                process.terminate()
                try: process.wait(4)
                except subprocess.TimeoutExpired: process.kill(); process.wait(4)
            cleanup_started = time.monotonic()
            workers = cleanup_workers(); deadline = cleanup_started + args.cleanup_timeout
            for worker in workers: worker.join(max(0, deadline - time.monotonic()))
            remaining = cleanup_workers()
            if owned_monitor is not None: endmonitor(owned_monitor)
            server.shutdown(); server.server_close(); server_thread.join(2)
    after = hashes(paths)
    records = [json.loads(line) for line in audit.read_text().splitlines() if line.strip()] if audit.exists() else []
    started_events = [r for r in records if r.get('event') == 'started_at_private_gate']
    exit_events = [r for r in records if r.get('event') == 'exited']
    expected = ['started_at_private_gate', 'constructor_verified_and_acknowledged', 'author_url_released', 'exited']
    linked = len(started_events) == 1 and len(exit_events) == 1
    if linked:
        key = (started_events[0].get('pid'), started_events[0].get('nonce_sha256'))
        linked = all((r.get('pid'), r.get('nonce_sha256')) == key for r in records)
    complete = linked and [r.get('event') for r in records] == expected and exit_events[0].get('profile_removed') is True
    live = [r['pid'] for r in started_events if pathlib.Path('/proc/%d' % r['pid']).exists()]
    report = {'version': 2, 'binary': str(binary), 'fixture': str(fixture), 'marker': args.marker,
              'source_hashes_before': before, 'source_hashes_after': after, 'sources_unchanged': before == after,
              'joined_cleanup_threads': len(workers), 'remaining_cleanup_threads': [t.name for t in remaining],
              'cleanup_elapsed_s': time.monotonic() - cleanup_started, 'elapsed_s': time.monotonic() - started,
              'guard_audit': str(audit), 'guard_events': [r.get('event') for r in records],
              'guard_events_complete_and_linked': bool(complete), 'remaining_recorded_pids': live,
              'runner_exception': repr(failure) if failure is not None else None,
              'method': 'Join actual trusted cleanup workers; require their real exit record and removed profile; never synthesize audit events.'}
    (output / 'guard-cleanup.json').write_text(json.dumps(report, indent=2) + '\n')
    print(result if result is not None else marker + 'FAIL browser exited or fixture timed out')
    print(json.dumps({'guard_cleanup': {'joined': len(workers), 'remaining': len(remaining), 'complete': bool(complete),
                                     'live_pids': live, 'sources_unchanged': before == after}}))
    if remaining or live or not complete or before != after: return 2
    if failure is not None: raise failure
    return 0 if result is not None and result.startswith(marker + 'PASS') else 1


if __name__ == '__main__': raise SystemExit(main())
