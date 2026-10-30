#!/usr/bin/env python3
"""Launch and observe only the neutral independent SDL app built by this task."""
import datetime as dt
import hashlib
import json
import os
import signal
import statistics
import subprocess
import time
from pathlib import Path

from bundle_case import matching_pids
from native_case import descendants
from resource_monitor import add_cpu_deltas, sample_processes


def main():
    root = Path.cwd().resolve()
    out = root / 'out/verification'
    manifest = json.loads((out / 'sdl-metal-control-manifest.json').read_text())
    app, exe = Path(manifest['app']), Path(manifest['executable'])
    if not app.is_relative_to(out) or exe != app / 'Contents/MacOS/LumeSDLMetalControl':
        raise SystemExit('Independent control must be the task-owned bundle')
    if hashlib.sha256(exe.read_bytes()).hexdigest() != manifest['binarySha256']:
        raise SystemExit('Control executable changed after compilation')
    if matching_pids(exe):
        raise SystemExit('An exact control executable is already running')
    prefix = out / ('sdl-metal-control-' + str(int(time.time())))
    stdout_path, stderr_path = Path(str(prefix) + '.stdout.log'), Path(str(prefix) + '.stderr.log')
    command = ['/usr/bin/open', '-n', str(app), '--stdout', str(stdout_path), '--stderr', str(stderr_path)]
    (out / 'monitor-scenario.txt').write_text('independent-sdl-metal-control\n')
    start = time.monotonic()
    started_utc = dt.datetime.now(dt.timezone.utc).isoformat()
    due, previous, prior = start, {}, None
    pid, observed = None, False
    samples, captures = [], {}
    launcher = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    failure = None
    try:
        while time.monotonic() - start < 32:
            processes = sample_processes()
            now = time.monotonic()
            add_cpu_deltas(processes, previous, now - prior if prior else 0)
            previous, prior = {p['pid']: p for p in processes}, now
            if pid is None:
                found = matching_pids(exe)
                if len(found) > 1:
                    raise RuntimeError('More than one exact control PID')
                if found:
                    pid = found[0]
                elif now - start > 10:
                    raise RuntimeError('Normal launch produced no exact control PID')
            selected = descendants(processes, pid) if pid else []
            cpus = [p['cpu_interval_percent'] for p in selected if p['cpu_interval_percent'] is not None]
            samples.append({'elapsedSeconds': round(now - start, 3), 'cpuIntervalPercent': sum(cpus) if cpus else None,
                            'rssKiB': sum(p['rss_kib'] for p in selected), 'members': selected})
            content = stdout_path.read_text(errors='replace') if stdout_path.exists() else ''
            if pid:
                for stage in ('initial', 'sdl-raised', 'app-activated'):
                    if stage not in captures and 'LUME_SDL_CAPTURE ' + stage in content:
                        handle = open(str(prefix) + '.' + stage + '.capture.json', 'w')
                        job = subprocess.Popen([str(out / 'capture-window'), str(pid), str(prefix) + '.' + stage + '.png'],
                                               stdout=handle, stderr=subprocess.STDOUT)
                        captures[stage] = (job, handle)
                if selected:
                    observed = True
                elif observed:
                    break
            due += 1
            time.sleep(max(0, due - time.monotonic()))
    except Exception as exc:
        failure = str(exc)
    finally:
        if pid and pid in matching_pids(exe):
            os.kill(pid, signal.SIGTERM)
            until = time.monotonic() + 4
            while pid in matching_pids(exe) and time.monotonic() < until:
                time.sleep(.1)
            if pid in matching_pids(exe):
                os.kill(pid, signal.SIGKILL)
        try:
            launcher_output = launcher.communicate(timeout=3)[0].decode(errors='replace')
        except subprocess.TimeoutExpired:
            launcher.terminate()
            launcher_output = launcher.communicate(timeout=3)[0].decode(errors='replace')
        for job, handle in captures.values():
            try:
                job.wait(timeout=3)
            except subprocess.TimeoutExpired:
                job.terminate(); job.wait(timeout=3)
            handle.close()
        (out / 'monitor-scenario.txt').write_text('development-uncontrolled\n')
    content = stdout_path.read_text(errors='replace') if stdout_path.exists() else ''
    records = []
    for line in content.splitlines():
        if not line.startswith('LUME_SDL_') or line.startswith('LUME_SDL_CAPTURE '):
            continue
        label, value = line.split(' ', 1)
        try:
            records.append({'type': label.removeprefix('LUME_SDL_'), 'value': json.loads(value)})
        except ValueError:
            pass
    states = [r['value'] for r in records if r['type'] == 'STATE']
    summary = {
        'startedUtc': started_utc, 'scope': 'Independent SDL3 Metal control, not Lume performance or feature acceptance',
        'launcher': 'normal LaunchServices open -n', 'launcherExit': launcher.returncode,
        'actualOwnedPid': pid, 'binarySha256': manifest['binarySha256'], 'sdlPkgVersion': manifest['sdlPkgVersion'],
        'failure': failure, 'observedProcessSamples': sum(bool(s['members']) for s in samples),
        'ownWindowVisibleInAnyState': any(s['windowVisible'] for s in states),
        'ownWindowOcclusionVisibleInAnyState': any(s['windowOcclusionVisible'] for s in states),
        'applicationActiveInAnyState': any(s['applicationActive'] for s in states),
        'metalViewAttachedInEveryState': bool(states) and all(s['metalViewWindowMatchesNative'] for s in states),
        'capturePaths': {stage: str(prefix) + '.' + stage + '.png' for stage in captures},
        'cleanupReported': any(r['type'] == 'CLEANUP' and all(r['value'].values()) for r in records),
        'cpuMeanPercent': statistics.mean(s['cpuIntervalPercent'] for s in samples if s['cpuIntervalPercent'] is not None) if any(s['cpuIntervalPercent'] is not None for s in samples) else None,
        'rssPeakMiB': max((s['rssKiB'] for s in samples), default=0) / 1024,
        'compositorVisualVerified': False,
        'normalActivationActions': [r['value'] for r in records if r['type'] == 'ACTION'],
        'stateFirst': states[0] if states else None, 'stateFinal': states[-1] if states else None,
        'launcherOutput': launcher_output,
    }
    full = dict(summary, records=records, samples=samples)
    Path(str(prefix) + '.json').write_text(json.dumps(full, indent=2))
    (out / 'sdl-metal-control-latest.json').write_text(json.dumps({'result': str(prefix) + '.json'}, indent=2))
    print(json.dumps(summary, indent=2))
    for stage in captures:
        print(stage + ': ' + Path(str(prefix) + '.' + stage + '.capture.json').read_text().strip())
    if failure or not summary['cleanupReported']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
