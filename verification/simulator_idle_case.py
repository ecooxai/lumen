#!/usr/bin/env python3
"""Fresh-profile static-page simulator idle observation, with bounded profiling."""
import argparse
import datetime as dt
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import plistlib
import signal
import statistics
import subprocess
import tempfile
import threading
import time

from bundle_case import matching_pids
from native_case import descendants
from resource_monitor import add_cpu_deltas, sample_processes

DEVICES = {
    'iphone': ('EDD9B083-63F1-44E6-9A7B-0A6964CC2561', 'iPhone 17 / iOS 26.5'),
    'ipad': ('590ACCA9-0BA2-4803-970F-EF3DAC515255', 'iPad Air 11 M4 / iOS 26.5'),
}
PAGE = b'''<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>Lume static idle control</title><style>html{background:#dff1ff;color:#163047;font:20px system-ui}body{margin:0;padding:32px}main{max-width:640px;margin:auto}h1{font-size:32px}p{line-height:1.5}aside{border-top:1px solid #91afc6;padding-top:18px}</style></head><body><main><h1>Lume static idle control</h1><p>This neutral page has no animation, media, network poll, or repeating timer.</p><aside>Resource sampling runs outside the browser. Simulator evidence is separate from a physical Apple device.</aside></main><script>setTimeout(function(){console.log('LUME_IDLE_EVIDENCE '+JSON.stringify({readyState:document.readyState,viewportWidth:innerWidth,viewportHeight:innerHeight,devicePixelRatio:devicePixelRatio,neutralStaticPage:true,animationsStarted:false}));},1000);</script></body></html>'''


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != '/idle.html':
            self.send_error(404); return
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(PAGE)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers(); self.wfile.write(PAGE)

    def log_message(self, *_):
        pass


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--device', choices=DEVICES, required=True)
    args = parser.parse_args(); udid, label = DEVICES[args.device]
    root = Path.cwd().resolve(); out = root / 'out/verification'; out.mkdir(parents=True, exist_ok=True)
    bundle_id = 'com.ecooxai.lumen'
    def container(kind):
        return Path(subprocess.check_output(['xcrun', 'simctl', 'get_app_container', udid, bundle_id, kind], text=True).strip()).resolve()
    app, data = container('app'), container('data')
    executable_name = plistlib.loads((app / 'Info.plist').read_bytes())['CFBundleExecutable']
    exe = app / executable_name
    if matching_pids(exe):
        raise SystemExit('Target simulator Lume is already running; no existing app will be terminated')
    profile_root = data / 'tmp/lume-verification'; profile_root.mkdir(parents=True, exist_ok=True)
    profile = Path(tempfile.mkdtemp(prefix='static-idle-', dir=profile_root)); profile.chmod(0o700)
    prefix = out / ('simulator-' + args.device + '-static-idle-' + str(int(time.time())))
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server_thread = threading.Thread(target=server.serve_forever, daemon=True); server_thread.start()
    url = 'http://127.0.0.1:' + str(server.server_port) + '/idle.html'
    env = os.environ.copy()
    for name in list(env):
        if name.startswith('SIMCTL_CHILD_LUMEN_'):
            env.pop(name)
    env.update(SIMCTL_CHILD_LUMEN_PROFILE_DIR=str(profile), SIMCTL_CHILD_LUMEN_NO_REPLAYKIT='1', SIMCTL_CHILD_LUMEN_CONSOLE='1')
    log_path = Path(str(prefix) + '.public.log'); log = log_path.open('w')
    command = ['xcrun', 'simctl', 'launch', '--console', udid, bundle_id, url]
    (out / 'monitor-scenario.txt').write_text('simulator-' + args.device + '-static-idle\n')
    started = dt.datetime.now(dt.timezone.utc).isoformat(); start = time.monotonic(); due = start
    launcher = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    pid, observed, previous, prior = None, False, {}, None
    samples, capture, profile_job = [], None, None
    page_ready_at = None
    failure = None
    try:
        while time.monotonic() - start < 70:
            rows = sample_processes(); now = time.monotonic()
            add_cpu_deltas(rows, previous, now - prior if prior else 0)
            previous, prior = {row['pid']: row for row in rows}, now
            if pid is None:
                found = matching_pids(exe)
                if len(found) > 1:
                    raise RuntimeError('More than one exact target simulator executable appeared')
                if found:
                    pid = found[0]
                elif now - start > 10:
                    raise RuntimeError('simctl launch did not produce the exact app executable PID')
            selected = descendants(rows, pid) if pid else []
            if page_ready_at is None and 'LUME_IDLE_EVIDENCE ' in log_path.read_text(errors='replace'):
                page_ready_at = now
            ready_elapsed = now - page_ready_at if page_ready_at is not None else None
            phase = 'loading' if ready_elapsed is None else 'settling' if ready_elapsed < 20 else 'idle' if ready_elapsed < 50 else 'post-measurement'
            cpu = [r['cpu_interval_percent'] for r in selected if r['cpu_interval_percent'] is not None]
            compilers = sum(1 for r in rows if r['name'] in ('clang', 'clang++', 'ld', 'swift-frontend', 'rustc', 'cargo', 'ninja', 'cc1') and (r.get('cpu_interval_percent') or r['cpu_percent']) > 10)
            samples.append({'elapsedSeconds': round(now - start, 3), 'phase': phase, 'cpuIntervalPercent': sum(cpu) if cpu else None,
                            'rssMiB': sum(r['rss_kib'] for r in selected) / 1024, 'activeCompilers': compilers})
            if pid:
                if selected:
                    observed = True
                elif observed:
                    failure = 'Owned simulator app exited before idle interval completed'; break
                if capture is None and ready_elapsed is not None and ready_elapsed >= 15:
                    capture = subprocess.Popen(['xcrun', 'simctl', 'io', udid, 'screenshot', str(prefix) + '.png'],
                                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                if profile_job is None and ready_elapsed is not None and ready_elapsed >= 50:
                    steady = [r['cpuIntervalPercent'] for r in samples if r['phase'] == 'idle' and r['cpuIntervalPercent'] is not None]
                    if steady and statistics.mean(steady) > 3:
                        profile_job = subprocess.Popen(['/usr/bin/sample', str(pid), '3', '-file', str(prefix) + '.sample.txt'],
                                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if ready_elapsed is not None and ready_elapsed >= 55:
                break
            if page_ready_at is None and now - start >= 14:
                failure = 'Neutral static page did not report ready'; break
            due += 1; time.sleep(max(0, due - time.monotonic()))
    except Exception as exc:
        failure = str(exc)
    finally:
        if profile_job is not None:
            try: profile_job.wait(timeout=6)
            except subprocess.TimeoutExpired: profile_job.terminate(); profile_job.wait(timeout=3)
        if capture is not None:
            try: capture.wait(timeout=3)
            except subprocess.TimeoutExpired: capture.terminate(); capture.wait(timeout=3)
        if pid and pid in matching_pids(exe):
            subprocess.run(['xcrun', 'simctl', 'terminate', udid, bundle_id], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10, check=False)
        if launcher.poll() is None:
            os.killpg(launcher.pid, signal.SIGTERM)
            try: launcher.wait(timeout=3)
            except subprocess.TimeoutExpired: os.killpg(launcher.pid, signal.SIGKILL); launcher.wait()
        log.close(); server.shutdown(); server.server_close()
        (out / 'monitor-scenario.txt').write_text('development-uncontrolled\n')
    content = log_path.read_text(errors='replace')
    evidence = []
    for line in content.splitlines():
        if 'LUME_IDLE_EVIDENCE ' in line:
            try: evidence.append(json.loads(line.split('LUME_IDLE_EVIDENCE ', 1)[1]))
            except ValueError: pass
    steady = [r for r in samples if r['phase'] == 'idle' and r['cpuIntervalPercent'] is not None]
    ready = any(row.get('neutralStaticPage') is True for row in evidence)
    summary = {
        'startedUtc': started, 'device': label, 'actualOwnedPid': pid,
        'scope': 'Static-page simulator browser idle; not physical hardware, typing, video, or cross-engine performance',
        'binarySha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'neutralPageSha256': hashlib.sha256(PAGE).hexdigest(),
        'failure': failure, 'pageReadyObserved': ready, 'settleSeconds': 20, 'measuredIdleSecondsRequested': 30,
        'measuredIdleSamples': len(steady), 'sampleCount': len(samples),
        'idleMeanCpuPercent': statistics.mean(r['cpuIntervalPercent'] for r in steady) if steady else None,
        'idleMedianCpuPercent': statistics.median(r['cpuIntervalPercent'] for r in steady) if steady else None,
        'idlePeakCpuPercent': max((r['cpuIntervalPercent'] for r in steady), default=None),
        'idlePeakRssMiB': max((r['rssMiB'] for r in steady), default=None),
        'idleRssGrowthMiB': steady[-1]['rssMiB'] - steady[0]['rssMiB'] if steady else None,
        'idleSamplesWithActiveCompilers': sum(bool(r['activeCompilers']) for r in steady),
        'boundedPostMeasurementStackProfile': str(prefix) + '.sample.txt' if profile_job else None,
        'screenshotPath': str(prefix) + '.png' if Path(str(prefix) + '.png').exists() else None,
        'evidence': evidence,
    }
    summary['idleMeasurementValid'] = failure is None and ready and len(steady) >= 28 and summary['idleSamplesWithActiveCompilers'] == 0
    summary['idleCpuWithinThreePercent'] = summary['idleMeasurementValid'] and summary['idleMeanCpuPercent'] <= 3
    Path(str(prefix) + '.json').write_text(json.dumps(dict(summary, samples=samples), indent=2))
    (out / ('simulator-' + args.device + '-static-idle-latest.json')).write_text(json.dumps({'result': str(prefix) + '.json'}, indent=2))
    print(json.dumps(summary, indent=2))
    if not summary['idleMeasurementValid']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
