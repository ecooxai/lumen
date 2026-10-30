#!/usr/bin/env python3
"""Native-only public video interval; reuse the accepted v3 probe and assessor.

New evidence is isolated. Resource limits and exact process identity are checked
throughout the interval; only typed public observations and owned PNGs survive.
"""
from pathlib import Path
import argparse, datetime as dt, hashlib, json, os, signal, statistics, struct
import subprocess, tempfile, threading, time
from public_video_case import VideoPipe, assess_video, URL
from bundle_case import matching_pids
from native_case import descendants
from resource_monitor import sample_processes, add_cpu_deltas

LIMIT_SECONDS = 70
RSS_LIMIT_MIB = 4096
DISK_FLOOR_BYTES = 1024 ** 3


def need(ok, reason):
    if not ok:
        raise RuntimeError(reason)


def sha(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()


def within_bounds(row):
    return (type(row.get('rss_mib')) in (int, float) and 0 <= row['rss_mib'] <= RSS_LIMIT_MIB and
            type(row.get('free_bytes')) is int and row['free_bytes'] >= DISK_FLOOR_BYTES and
            type(row.get('monitor_age_s')) in (int, float) and 0 <= row['monitor_age_s'] < 6 and
            row.get('active_compilers') == 0)


def identity(pid, executable):
    if pid is None:
        return None
    value = subprocess.run(['/bin/ps', '-p', str(pid), '-o', 'lstart=', '-o', 'comm='],
                           capture_output=True, text=True, timeout=3)
    text = value.stdout.strip()
    return text if value.returncode == 0 and text.endswith(' ' + str(executable)) else None


def capture_proof(root, image, diagnostic, pid):
    need(image.is_file() and not image.is_symlink() and image.resolve().is_relative_to(root / 'out/verification'),
         'Owned public PNG is missing or escaped')
    need(diagnostic.is_file() and not diagnostic.is_symlink() and diagnostic.stat().st_size <= 65536,
         'Owned capture diagnostic is missing or oversized')
    with image.open('rb') as stream:
        header = stream.read(24)
    need(len(header) == 24 and header[:8] == b'\x89PNG\r\n\x1a\n' and header[12:16] == b'IHDR', 'Owned PNG header is invalid')
    width, height = struct.unpack('>II', header[16:24])
    need(width == 1024 and 724 <= height <= 1100, 'Owned PNG dimensions differ')
    proof = json.loads(diagnostic.read_text())
    raw = proof.get('path')
    need(isinstance(raw, str) and len(raw) < 1024, 'Capture path is unavailable')
    measured = Path(raw)
    need((measured if measured.is_absolute() else root / measured).resolve() == image.resolve(), 'Capture path differs')
    need(proof.get('testPID') == pid and type(proof.get('windowID')) is int and proof['windowID'] > 0 and
         proof.get('bytes') == image.stat().st_size and proof.get('alpha') == 1 and
         all(proof.get(key) is True for key in ('onScreen', 'targetActive', 'frontmostMatchesTarget')),
         'Capture was not the visible owned browser window')
    return {'path': str(image.relative_to(root)), 'sha256': sha(image), 'bytes': image.stat().st_size,
            'dimensions': [width, height], 'windowID': proof['windowID'], 'testPID': pid,
            'visibleOwnedWindow': True, 'diagnosticSHA256': sha(diagnostic)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', required=True)
    parser.add_argument('--tag', required=True)
    args = parser.parse_args()
    import re
    need(re.fullmatch(r'[a-z0-9-]{1,40}', args.tag), 'Unexpected native public case tag')
    root = Path.cwd().resolve()
    need(root == Path('/Users/devin/repos/lumen'), 'Unexpected native public workspace')
    out = root / 'out/verification'
    prefix = out / ('public-video-lumen-' + args.tag)
    need(not list(out.glob(prefix.name + '.*')), 'Preserve prior native public evidence')
    metadata = root / args.metadata
    need(not Path(args.metadata).is_absolute() and metadata.resolve().is_relative_to(root / 'out/native-verification'),
         'Candidate metadata escaped project')
    manifest = json.loads(metadata.read_text())
    app, exe = Path(manifest['app']).resolve(), Path(manifest['executable']).resolve()
    need(app.is_relative_to(root / 'out/native-verification') and exe == app / 'Contents/MacOS/lumen' and
         manifest.get('developmentOnly') is True, 'Unexpected native candidate')
    source_sha, signed_sha = sha(root / 'build/lumen'), sha(exe)
    need(source_sha == manifest.get('binaryBeforeBundleSigningSha256') and signed_sha == manifest.get('binarySha256'),
         'Candidate executable does not match metadata')
    need(not matching_pids(exe), 'The native candidate is already running')
    os.umask(0o077)
    profile = Path(tempfile.mkdtemp(prefix='public-video-empty-lumen-', dir=out))
    evidence = []
    pipe = VideoPipe(out, evidence)
    launcher = None
    launcher_thread = None
    jobs = []
    captures = {}
    visibility = {}
    samples = []
    pid = None
    owned_identity = None
    failure = None
    cleanup_errors = []
    window_proofs = {}
    observed = False
    browser_stopped = False
    monitor = out / 'resource-monitor.jsonl'
    scenario = out / 'monitor-scenario.txt'
    started_utc = dt.datetime.now(dt.timezone.utc).isoformat()
    start = time.monotonic()
    due = start
    previous = {}
    prior = None

    def interrupted(signum, frame):
        raise KeyboardInterrupt('Native public interval interrupted')

    for signum in (signal.SIGINT, signal.SIGTERM):
        signal.signal(signum, interrupted)

    def owned_now():
        return owned_identity is not None and identity(pid, exe) == owned_identity

    def job(label, command):
        need(owned_now(), 'Owned browser identity changed before capture')
        path = Path(str(prefix) + '.' + label + '.json')
        stream = path.open('x')
        try:
            child = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=stream,
                                     stderr=subprocess.STDOUT, start_new_session=True)
        except BaseException:
            stream.close()
            raise
        jobs.append((child, stream, label))
        return path

    try:
        scenario.write_text('public-video-native-candidate02\n')
        command = ['/usr/bin/open', '-n', str(app), '--stdout', str(pipe.path), '--stderr', str(pipe.path)]
        envs = {'LUMEN_PROFILE_DIR': str(profile), 'LUMEN_PRE_FILE': str(root / 'verification/public-video-probe.js'),
                'LUMEN_CONSOLE': '1', 'LUMEN_NO_SIDEBAR': '1', 'LUMEN_WINDOW': '1024x768', 'LUMEN_MAC_VISIBILITY': '0'}
        for key, value in envs.items():
            command += ['--env', key + '=' + value]
        command += ['--args', URL]
        launcher = subprocess.Popen(command, env={key: value for key, value in os.environ.items() if not key.startswith('LUMEN_')},
                                    stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
        launcher_thread = threading.Thread(target=pipe.drain, args=(launcher.stdout,), daemon=True)
        launcher_thread.start()
        while time.monotonic() - start < LIMIT_SECONDS:
            processes = sample_processes()
            now = time.monotonic()
            add_cpu_deltas(processes, previous, now - prior if prior else 0)
            previous = {row['pid']: row for row in processes}
            prior = now
            if pid is None:
                found = matching_pids(exe)
                need(len(found) <= 1, 'Native executable ownership is ambiguous')
                if found:
                    pid = found[0]
                    owned_identity = identity(pid, exe)
                    need(owned_identity is not None, 'Native process identity unavailable')
                elif now - start > 10:
                    raise RuntimeError('Normal launch produced no native executable')
            if pid is not None:
                need(owned_now(), 'Native process ended or changed before observation completed')
                if 'activation' not in visibility:
                    visibility['activation'] = job('visibility-activation', [str(out / 'capture-window'), '--activate-owned', str(pid)])
            owned = descendants(processes, pid) if pid else []
            cpu = [row['cpu_interval_percent'] for row in owned if row['cpu_interval_percent'] is not None]
            fs = os.statvfs(root)
            modified = monitor.stat().st_mtime
            row = {'elapsed_s': round(now - start, 3), 'cpu_interval_percent': sum(cpu) if cpu else None,
                   'rss_mib': sum(item['rss_kib'] for item in owned) / 1024, 'free_bytes': fs.f_bavail * fs.f_frsize,
                   'monitor_age_s': time.time() - modified, 'owned_process_observed': bool(owned),
                   'active_compilers': sum(1 for item in processes if item['name'] in ('clang', 'clang++', 'ld', 'swift-frontend', 'rustc', 'cargo', 'ninja', 'cc1') and
                                           (item.get('cpu_interval_percent') or item['cpu_percent']) > 10)}
            samples.append(row)
            need(within_bounds(row), 'Native public interval exceeded a resource or observer bound')
            if owned:
                observed = True
            elif observed:
                raise RuntimeError('Owned native process disappeared from resource sample')
            for phase, deadline in (('earlier', 18), ('later', 36)):
                if pid and phase not in captures and now - start >= deadline:
                    image = Path(str(prefix) + '.' + phase + '.png')
                    diagnostic = job(phase + '.capture', [str(out / 'capture-window'), str(pid), str(image)])
                    captures[phase] = (image, diagnostic)
            if pid and 'steady' not in visibility and now - start >= 35:
                visibility['steady'] = job('visibility-steady', [str(out / 'capture-window'), '--inspect-owned', str(pid)])
            if now - start >= 36 and any(item.get('resourcesReleased') is True for item in evidence):
                break
            due += 1
            time.sleep(max(0, due - time.monotonic()))
        need(any(item.get('resourcesReleased') is True for item in evidence), 'Native v3 probe did not complete before the deadline')
    except (Exception, KeyboardInterrupt) as error:
        failure = type(error).__name__ + ': ' + str(error)[:220]
    finally:
        # Ignore only further termination requests while bounded teardown runs.
        for signum in (signal.SIGINT, signal.SIGTERM):
            signal.signal(signum, signal.SIG_IGN)
        try:
            # LaunchServices may create the process before our first sample.
            # The exact executable was absent at launch; adopt only one such PID.
            if pid is None and launcher is not None:
                until = time.monotonic() + 3
                while time.monotonic() < until:
                    found = matching_pids(exe)
                    if len(found) == 1:
                        pid = found[0]
                        owned_identity = identity(pid, exe)
                        break
                    if len(found) > 1:
                        cleanup_errors.append('Ambiguous late launcher identities were not signalled')
                        break
                    time.sleep(0.1)
            if owned_now():
                os.kill(pid, signal.SIGTERM)
                until = time.monotonic() + 4
                while owned_now() and time.monotonic() < until:
                    time.sleep(0.1)
                if owned_now():
                    os.kill(pid, signal.SIGKILL)
                    until = time.monotonic() + 3
                    while owned_now() and time.monotonic() < until:
                        time.sleep(0.1)
            browser_stopped = not matching_pids(exe)
            if not browser_stopped:
                cleanup_errors.append('Owned executable remains; unexpected identities were not signalled')
        except Exception as error:
            cleanup_errors.append('Browser cleanup: ' + type(error).__name__)
        if launcher is not None:
            try:
                if launcher.poll() is None:
                    os.killpg(launcher.pid, signal.SIGTERM)
                    try:
                        launcher.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        os.killpg(launcher.pid, signal.SIGKILL)
                        launcher.wait(timeout=3)
                launcher_thread.join(timeout=3)
                need(not launcher_thread.is_alive(), 'Launcher redactor remains')
            except Exception as error:
                cleanup_errors.append('Launcher cleanup: ' + type(error).__name__)
        for child, stream, label in jobs:
            try:
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGTERM)
                    try:
                        child.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        os.killpg(child.pid, signal.SIGKILL)
                        child.wait(timeout=3)
                need(child.returncode == 0, 'Owned capture did not complete')
            except Exception as error:
                cleanup_errors.append(label + ': ' + type(error).__name__)
            finally:
                stream.close()
        try:
            pipe.close()
        except Exception as error:
            cleanup_errors.append('FIFO cleanup: ' + type(error).__name__)
        scenario.write_text('development-uncontrolled\n')
    actual_visibility = {}
    for label, path in visibility.items():
        try:
            actual_visibility[label] = json.loads(path.read_text())
        except (OSError, ValueError):
            actual_visibility[label] = {'unavailable': True}
    try:
        need(set(captures) == {'earlier', 'later'}, 'Both owned public captures are required')
        for label, (image, diagnostic) in captures.items():
            window_proofs[label] = capture_proof(root, image, diagnostic, pid)
    except Exception as error:
        failure = failure or type(error).__name__ + ': ' + str(error)[:220]
    assessment = assess_video(evidence)
    steady = [row['cpu_interval_percent'] for row in samples if 30 <= row['elapsed_s'] < 44 and row['cpu_interval_percent'] is not None]
    result = {'schemaVersion': 1, 'engine': 'lumen', 'tag': args.tag, 'startedUTC': started_utc,
              'scope': 'muted programmatic public video playback in a fresh native candidate profile',
              'actualBrowserPID': pid, 'processStartIdentity': owned_identity, 'binarySHA256': sha(exe),
              'sourceBinarySHA256': sha(root / 'build/lumen'), 'profilePolicy': 'fresh empty isolated; no cookies imported',
              'rawLogsSaved': False, 'observationBoundSeconds': LIMIT_SECONDS, 'observedProcess': observed,
              'sampleCount': len(samples), 'samplesWithCompilers': sum(row['active_compilers'] > 0 for row in samples),
              'peakRSSMiB': max((row['rss_mib'] for row in samples), default=None),
              'minimumFreeBytes': min((row['free_bytes'] for row in samples), default=None),
              'maximumMonitorAgeSeconds': max((row['monitor_age_s'] for row in samples), default=None),
              'steadyCPUPercent': statistics.mean(steady) if steady else None,
              'resourceBoundsPassed': bool(samples) and all(within_bounds(row) for row in samples),
              'windowVisibility': actual_visibility, 'captures': window_proofs, 'assessment': assessment,
              'evidence': evidence, 'samples': samples, 'failure': failure, 'cleanupErrors': cleanup_errors,
              'ownedBrowserStopped': browser_stopped, 'launcherExit': launcher.returncode if launcher else None,
              'binariesUnchanged': sha(exe) == signed_sha and sha(root / 'build/lumen') == source_sha,
              'physicalInputClaim': False, 'actualCompositorFramesVerified': False, 'performanceComparisonClaim': False}
    result['passed'] = bool(not failure and not cleanup_errors and browser_stopped and result['binariesUnchanged'] and
                            result['resourceBoundsPassed'] and result['launcherExit'] == 0 and observed and len(samples) >= 30 and
                            assessment.get('videoFrameProgressVerified') is True and set(window_proofs) == {'earlier', 'later'} and
                            evidence and all(row.get('probeRevision') == 3 for row in evidence))
    path = Path(str(prefix) + '.json')
    with path.open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(json.dumps({key: value for key, value in result.items() if key != 'samples'}, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
