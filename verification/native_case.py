#!/usr/bin/env python3
"""Run one owned native browser process, sample one-second CPU deltas and RSS.

Only our launched process group is terminated at a deadline. This helper never
terminates an existing user browser. Logs are private local evidence; publish
only reviewed results and screenshots from neutral public/demo pages.
"""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import signal
import statistics
import subprocess
import tempfile
import time

from resource_monitor import sample_processes, add_cpu_deltas


def descendants(rows, root):
    selected = {root}
    while True:
        new = {r['pid'] for r in rows if r['ppid'] in selected}
        if new <= selected:
            return [r for r in rows if r['pid'] in selected]
        selected |= new


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--name', required=True)
    parser.add_argument('--url', required=True)
    parser.add_argument('--probe')
    parser.add_argument('--duration', type=float, default=35)
    parser.add_argument('--settle', type=float, default=15)
    parser.add_argument('--binary', default='./build/lumen')
    parser.add_argument('--engine', choices=['lumen','chrome'], default='lumen')
    parser.add_argument('--kind', choices=['html5','lab','rtc','common','showcase','gpu-challenge','capture-control','identity-simulation'], default='html5')
    parser.add_argument('--shot-delay-ms', type=int)
    parser.add_argument('--capture-markers', action='store_true')
    parser.add_argument('--headful', action='store_true')
    parser.add_argument('--activate-owned', action='store_true')
    args = parser.parse_args()
    out = Path('out/verification')
    out.mkdir(parents=True, exist_ok=True)
    prefix = out / args.name
    binary = Path(args.binary)
    binary_fingerprint = {'sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
                          'mtime_utc':dt.datetime.fromtimestamp(binary.stat().st_mtime,dt.timezone.utc).isoformat()}
    source_revision = subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
    source_dirty = bool(subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],text=True).strip())
    started_utc = dt.datetime.now(dt.timezone.utc).isoformat()
    env = os.environ.copy()
    profile = tempfile.mkdtemp(prefix=args.engine + '-public-profile-', dir=out.resolve())
    if args.engine == 'lumen':
        env['LUMEN_PROFILE_DIR'] = profile
    env.update(LUMEN_CONSOLE='1', LUMEN_NO_SIDEBAR='1', LUMEN_WINDOW='1024x768',
               LUMEN_SHOT=str(prefix.with_suffix('.png')), LUMEN_SHOT_MS=str(int((args.duration - 3) * 1000)))
    if args.probe:
        env['LUMEN_PRE_FILE'] = args.probe
    if args.shot_delay_ms is not None:
        env['LUMEN_SHOT_MS'] = str(args.shot_delay_ms)
    mode = out / 'monitor-scenario.txt'
    mode.write_text(args.name + '\n')
    rows = []
    previous = {}
    prior_time = None
    start = time.monotonic()
    due = start
    ended_by_harness = False
    driver = None
    captures = {}
    activator = None
    activation_log = None
    with prefix.with_suffix('.log').open('w') as log:
        if args.engine == 'chrome':
            command = [args.binary] + ([] if args.headful else ['--headless=new']) + ['--no-first-run', '--no-default-browser-check',
                       '--disable-background-networking', '--disable-sync', '--remote-debugging-port=0',
                       '--user-data-dir=' + profile, '--window-size=1024,768', 'about:blank']
        else:
            command = [args.binary, args.url]
        proc = subprocess.Popen(command, env=env, stdout=log,
                                stderr=subprocess.STDOUT, start_new_session=True)
        if args.activate_owned:
            activation_log = open(str(prefix) + '.activation.log','w')
            activator = subprocess.Popen(['out/verification/capture-window','--activate-owned',str(proc.pid)],stdout=activation_log,stderr=subprocess.STDOUT)
        if args.engine == 'chrome':
            driver = subprocess.Popen(['node','verification/chrome_probe.mjs',profile,str(prefix),args.url,args.kind,'headful' if args.headful else 'headless'],
                                      stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        try:
            while proc.poll() is None and time.monotonic() - start < args.duration:
                processes = sample_processes()
                now = time.monotonic()
                add_cpu_deltas(processes, previous, now - prior_time if prior_time else 0)
                previous = {r['pid']: r for r in processes}
                prior_time = now
                selected = descendants(processes, proc.pid)
                values = [r['cpu_interval_percent'] for r in selected if r['cpu_interval_percent'] is not None]
                compilers = [r for r in processes if r['name'] in ('clang','clang++','ld','swift-frontend','rustc','cargo','ninja','cc1')
                             and (r.get('cpu_interval_percent') or r['cpu_percent']) > 10]
                rows.append({'elapsed_s': round(now - start, 3), 'cpu_interval_percent': sum(values) if values else None,
                             'rss_kib': sum(r['rss_kib'] for r in selected), 'members': selected,
                             'active_compilers':compilers,'host_load_average':os.getloadavg()})
                if args.capture_markers:
                    current_log = prefix.with_suffix('.log').read_text(errors='replace')
                    for stage in ('overview','gpu','network'):
                        if stage not in captures and 'LUME_CAPTURE_REQUEST ' + stage in current_log:
                            destination = str(prefix) + '.' + stage + '.png'
                            capture_log = open(str(prefix) + '.' + stage + '.capture.log','w')
                            captures[stage] = (subprocess.Popen(['out/verification/capture-window',str(proc.pid),destination],stdout=capture_log,stderr=subprocess.STDOUT),capture_log)
                due += 1
                time.sleep(max(0, due - time.monotonic()))
        finally:
            if args.activate_owned and proc.poll() is None:
                with open(str(prefix)+'.visibility-final.json','w') as visibility_log:
                    try:
                        subprocess.run(['out/verification/capture-window','--inspect-owned',str(proc.pid)],stdout=visibility_log,stderr=subprocess.STDOUT,timeout=3,check=False)
                    except subprocess.TimeoutExpired:
                        visibility_log.write('{"error":"visibility-inspection-timeout"}')
            if proc.poll() is None:
                ended_by_harness = True
                os.killpg(proc.pid, signal.SIGTERM)
                try:
                    proc.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid, signal.SIGKILL)
                    proc.wait()
            if driver is not None and driver.poll() is None:
                os.killpg(driver.pid, signal.SIGTERM)
                driver.wait(timeout=4)
            if activator is not None:
                try:activator.wait(timeout=5)
                except subprocess.TimeoutExpired:activator.terminate();activator.wait(timeout=3)
                activation_log.close()
            for capture, capture_log in captures.values():
                try:
                    capture.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    capture.terminate()
                    capture.wait(timeout=3)
                capture_log.close()
            mode.write_text('development-uncontrolled\n')
    steady = [r for r in rows if r['elapsed_s'] >= args.settle and r['cpu_interval_percent'] is not None]
    measured = [r for r in rows if r['cpu_interval_percent'] is not None]
    visibility_evidence = {}
    for label, suffix in [('activation','.activation.log'),('final','.visibility-final.json')]:
        try: visibility_evidence[label] = json.loads(Path(str(prefix)+suffix).read_text())
        except (OSError, ValueError): pass
    result = {'name': args.name, 'url': args.url, 'process_pid': proc.pid, 'binary': args.binary,
        'binary_fingerprint':binary_fingerprint,'source_revision':source_revision,'source_has_uncommitted_changes':source_dirty,
        'window_visibility':visibility_evidence, 'engine': args.engine, 'headless': args.engine == 'chrome' and not args.headful, 'started_utc': started_utc, 'profile_policy': 'fresh isolated empty profile for each case', 'duration_s': round(time.monotonic() - start, 3), 'settle_s': args.settle, 'exit_code': proc.returncode,
        'ended_by_harness': ended_by_harness, 'screenshot_written': prefix.with_suffix('.png').exists(),
        'owned_window_captures':{stage:{'exit_code':pair[0].returncode,'written':Path(str(prefix)+'.'+stage+'.png').exists()} for stage,pair in captures.items()},
        'sample_count': len(rows), 'steady_samples': len(steady),
        'samples_with_active_compilers':sum(bool(r['active_compilers']) for r in rows),
        'steady_cpu_mean_percent': round(statistics.mean(r['cpu_interval_percent'] for r in steady), 3) if steady else None,
        'cpu_peak_percent': round(max((r['cpu_interval_percent'] for r in measured), default=0), 3),
        'rss_peak_mib': round(max((r['rss_kib'] for r in rows), default=0) / 1024, 3),
        'rss_final_mib': round(rows[-1]['rss_kib'] / 1024, 3) if rows else None,
        'resource_scope': 'owned process and descendants; CPU delta one logical core; sum RSS may double-count shared memory',
        'host_scope': 'shared development host; not an isolated comparative benchmark', 'samples': rows}
    prefix.with_suffix('.json').write_text(json.dumps(result, indent=2))
    print(json.dumps({k:v for k,v in result.items() if k != 'samples'}, indent=2))
    text = prefix.with_suffix('.log').read_text(errors='replace')
    evidence = [line for line in text.splitlines() if any(marker in line for marker in ['LUME_EVIDENCE ', 'LUME_CHROME_EVIDENCE ', 'LUME_LAB_EVIDENCE ', 'LUME_RTC_INTEROP ', 'LUME_WINDOW_EVIDENCE ', 'LUME_IDENTITY_EVIDENCE '])]
    prefix.with_suffix('.evidence.json').write_text(json.dumps(evidence,indent=2))
    for line in evidence[-16:]:
        print(line)


if __name__ == '__main__':
    main()

