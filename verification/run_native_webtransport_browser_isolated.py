#!/usr/bin/env python3
"""Real WT-enabled bundle gate in a fresh, owned loopback HTTP/3 interval.

Never borrows the legacy lab's UDP4433, certificates or audit. The immutable
candidate bundle and explicitly selected native gate must agree with current
sources. Functional evidence is separate from performance and pixel review.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
import urllib.request

from bundle_case import matching_pids
from native_case import descendants
from resource_monitor import sample_processes, add_cpu_deltas
from webtransport_gate import assess

FIXTURE_SHA = '5aee5006eda64b1d516b86e21e2ddb47d2f06928c458a443dd06f1f87f52d2ce'
LAB_FILES = ('index.html', 'style.css', 'app.mjs', 'core.mjs', 'iframe-probe.html',
             'iframe-probe.mjs', 'frame.css', 'rtc-peer.html', 'rtc-peer.mjs',
             'rtc-peer.css', 'rtc-broker.mjs', 'server.mjs', 'webtransport_server.py')
NATIVE_REQUIRED_SOURCES = ('src/js/webtransport_native.cc', 'src/js/js.cc',
                           'src/js/js_int.h', 'src/js/prelude/08-window.js',
                           'src/js/prelude/07t-webtransport-native.js', 'Makefile',
                           'tests/test_webtransport_native.cc', 'tools/prepare-webtransport-link.py')


def need(value, message):
    if not value:
        raise RuntimeError(message)


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def load(path, limit=1024 * 1024):
    need(path.is_file() and path.stat().st_size <= limit, 'Missing or oversized JSON: ' + path.name)
    value = json.loads(path.read_text())
    need(isinstance(value, dict), 'Expected an evidence object: ' + path.name)
    return value


def owned_path(root, relative):
    need(isinstance(relative, str) and not Path(relative).is_absolute(), 'Unexpected absolute provenance path')
    path = root / relative
    need(path.resolve().is_relative_to(root), 'Provenance path escapes project')
    return path


def native_provenance(root, gate_path):
    gate = load(gate_path)
    need(gate.get('passed') is True and gate.get('running') is False,
         'The selected native gate has not completed successfully')
    need(gate.get('sourcesUnchangedDuringGate') is True and
         gate.get('providerUnchangedDuringGate') is True and
         gate.get('browserBinaryUnchanged') is True,
         'The selected native gate lacks stable source/provider evidence')
    need(type(gate.get('nativeAssertionCount')) is int and gate['nativeAssertionCount'] >= 39,
         'The full native binding assertions are required')
    commands = gate.get('commands', [])
    need({entry.get('label') for entry in commands} >= {'build', 'http3', 'capture-policy', 'window-lifecycle'}
         and all(entry.get('passed') is True and entry.get('exit') == 0 and 'stoppedForBound' not in entry
                 for entry in commands), 'The native command gates did not all pass')
    independent = gate.get('independentServerEvidence', {})
    need(all(independent.get(key) is True for key in ('exactServerByteCorrelation',
         'allEventTimesWithinOwnedInterval', 'ownedServerStopped', 'ownedNativeStopped')),
         'The selected native gate lacks its own completed server correlation')
    source_hashes = gate.get('sourceHashes', {})
    need(all(name in source_hashes for name in NATIVE_REQUIRED_SOURCES), 'Incomplete native source fingerprints')
    tracked = {str(gate_path.relative_to(root)): digest(gate_path)}
    for name, expected in source_hashes.items():
        path = owned_path(root, name)
        need(path.is_file() and digest(path) == expected, 'Source differs from the selected native gate: ' + name)
        tracked[name] = expected
    manifest_path = root / 'build-deps/webtransport/macos/manifest.json'
    manifest = load(manifest_path)
    need(digest(manifest_path) == gate.get('backendManifestSha256'),
         'Component manifest changed after the selected native gate; select a fresh passing gate')
    need(manifest.get('backend') == 'wtransport-0.7.2' and manifest.get('version') == '0.7.2'
         and manifest.get('target') == 'aarch64-apple-darwin', 'Wrong transport backend or target')
    tracked[str(manifest_path.relative_to(root))] = digest(manifest_path)
    source_files = manifest.get('sourceFiles', {})
    need(isinstance(source_files, dict) and all(name in source_files for name in
         ('Cargo.toml', 'Cargo.lock', 'src/lib.rs', 'include/lumen_wt.h', 'examples/ffi_gate.rs')),
         'The full native source and lockfile manifest is required')
    for name, expected in source_files.items():
        relative = 'src/net/webtransport-native/' + name
        path = owned_path(root, relative)
        need(path.is_file() and digest(path) == expected, 'Component source differs from its built manifest: ' + name)
        tracked[relative] = expected
    for relative, expected in (
        ('build-deps/webtransport/macos/lib/liblumen_webtransport_native.a', manifest.get('archiveSHA256')),
        ('build-deps/webtransport/macos/include/lumen_wt.h', manifest.get('headerSHA256')),
        ('build/test_webtransport_native', gate.get('testBinarySha256')),
    ):
        path = root / relative
        need(path.is_file() and digest(path) == expected, 'Native artifact differs from the selected gate: ' + relative)
        tracked[relative] = expected
    return gate, tracked


def bundle_provenance(root, metadata):
    manifest = load(metadata)
    app = Path(manifest['app']).resolve()
    executable = Path(manifest['executable']).resolve()
    need(app.is_relative_to(root / 'out/native-verification') and executable == app / 'Contents/MacOS/lumen',
         'The bundle must be the isolated local development candidate')
    need(manifest.get('developmentOnly') is True, 'An explicit development bundle is required')
    need(executable.is_file() and not matching_pids(executable), 'Candidate executable is missing or already running')
    need(digest(root / 'build/lumen') == manifest.get('binaryBeforeBundleSigningSha256'),
         'Candidate bundle is stale relative to build/lumen')
    need(digest(executable) == manifest.get('binarySha256'), 'Candidate signed executable differs from its manifest')
    stable = root / 'out/native-verification/mac-dev-bundle-stable.json'
    if stable.is_file():
        need(app != Path(load(stable)['app']).resolve(), 'Use the fresh WT candidate; preserve the stable accepted bundle')
    return manifest, executable


def candidate_chain(root, manifest, native_path, gate):
    expected_gate = {'path': str(native_path.relative_to(root)), 'sha256': digest(native_path),
                     'evidenceTag': gate.get('evidenceTag')}
    need(manifest.get('nativeGate') == expected_gate,
         'The candidate metadata is not bound to this exact successful native gate')
    reference = manifest.get('relinkReport', {})
    report_path = owned_path(root, reference.get('path'))
    need(report_path.resolve().is_relative_to(root / 'out/native-verification') and
         report_path.is_file() and digest(report_path) == reference.get('sha256'),
         'The candidate relink report is absent or changed')
    report = load(report_path)
    need(report.get('passed') is True and report.get('running') is False,
         'The candidate relink did not complete successfully')
    need(report.get('nativeGate') == expected_gate and
         report.get('sourceBinarySha256') == manifest['binaryBeforeBundleSigningSha256'] and
         report.get('signedExecutableSha256') == manifest['binarySha256'],
         'The completed relink does not bind this candidate executable to the selected native gate')
    return {reference['path']: reference['sha256']}, expected_gate, reference


def child_stop(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=4)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)


def process_identity(pid, executable):
    if pid not in matching_pids(executable):
        return None
    run = subprocess.run(['/bin/ps', '-p', str(pid), '-o', 'lstart=', '-o', 'comm='],
                         capture_output=True, text=True, timeout=3, check=False)
    value = run.stdout.strip()
    return value if run.returncode == 0 and value.endswith(str(executable)) else None


def choose_port(kind):
    with socket.socket(socket.AF_INET, kind) as reservation:
        reservation.bind(('127.0.0.1', 0))
        return reservation.getsockname()[1]


def verify_listener(process, protocol, port):
    need(process.poll() is None, 'Owned fixture exited before listener verification')
    command = ['/usr/sbin/lsof', '-nP', '-a', '-p', str(process.pid), '-i' + protocol + ':' + str(port), '-Fpn']
    if protocol == 'TCP':
        command += ['-sTCP:LISTEN']
    check = subprocess.run(command, capture_output=True, text=True, timeout=3, check=False)
    lines = check.stdout.splitlines()
    need(check.returncode == 0 and 'p' + str(process.pid) in lines and
         'n127.0.0.1:' + str(port) in lines, 'Fresh loopback port is not held by the spawned fixture PID')
    return {'pid': process.pid, 'protocol': protocol, 'port': port, 'loopbackListenerVerified': True}


def metadata(path, checksum=False):
    if not path.exists():
        return {'exists': False}
    stat = path.stat()
    result = {'exists': True, 'device': stat.st_dev, 'inode': stat.st_ino,
              'bytes': stat.st_size, 'mtimeNs': stat.st_mtime_ns}
    if checksum:
        result['sha256'] = digest(path)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name', required=True)
    parser.add_argument('--native-gate', required=True)
    parser.add_argument('--metadata', required=True)
    args = parser.parse_args()
    root = Path.cwd().resolve()
    need(sys.platform == 'darwin' and root == Path('/Users/devin/repos/lumen'), 'Unexpected Mac project workspace')
    need(re.fullmatch(r'native-webtransport-browser-[a-z0-9-]{1,64}', args.name), 'Use an explicit safe browser attempt name')
    out = root / 'out/verification'
    case = out / (args.name + '-fixture')
    need(not list(out.glob(args.name + '*')), 'Preserve the previous browser attempt; choose a fresh explicit name')
    native_path = owned_path(root, args.native_gate)
    bundle_path = owned_path(root, args.metadata)
    need(native_path.resolve().is_relative_to(root / 'out/native-verification') and
         bundle_path.resolve().is_relative_to(root / 'out/native-verification'), 'Evidence must be project-owned')
    gate, tracked = native_provenance(root, native_path)
    bundle, executable = bundle_provenance(root, bundle_path)
    chain_hashes, selected_gate, relink_reference = candidate_chain(root, bundle, native_path, gate)
    tracked.update(chain_hashes)
    tracked.update({args.metadata: digest(bundle_path), 'build/lumen': digest(root / 'build/lumen'),
                    str(executable.relative_to(root)): digest(executable)})
    lab = root / 'tools/web-capability-lab'
    need(digest(lab / 'webtransport_server.py') == FIXTURE_SHA, 'Review the changed independent HTTP/3 fixture first')
    interpreter = lab / '.venv/bin/python'
    need(interpreter.is_file() and shutil.which('node'), 'Project aioquic interpreter and Node are required')
    monitor = out / 'resource-monitor.jsonl'
    need(monitor.is_file() and 0 <= time.time() - monitor.stat().st_mtime < 6,
         'Renew the task-owned one-second monitor before this browser interval')
    need(shutil.disk_usage(root).free >= 1024 ** 3, 'Keep at least 1GiB free disk for this functional interval')
    for name in LAB_FILES:
        tracked['tools/web-capability-lab/' + name] = digest(lab / name)
    for name in ('run_native_webtransport_browser_isolated.py', 'webtransport_gate.py',
                 'webtransport-only-probe.js', 'bundle_case.py', 'resource_monitor.py', 'native_case.py'):
        tracked['verification/' + name] = digest(root / 'verification' / name)
    tracked['out/verification/capture-window'] = digest(out / 'capture-window')
    os.umask(0o077)
    case.mkdir(mode=0o700)
    copied_lab = case / 'lab'
    copied_lab.mkdir(mode=0o700)
    for name in LAB_FILES:
        shutil.copyfile(lab / name, copied_lab / name)
        need(digest(copied_lab / name) == tracked['tools/web-capability-lab/' + name], 'Lab copy differs: ' + name)
    udp_port, tcp_port = choose_port(socket.SOCK_DGRAM), choose_port(socket.SOCK_STREAM)
    need(udp_port != 4433, 'The legacy UDP4433 fixture must remain untouched')
    origin = 'http://127.0.0.1:' + str(tcp_port)
    endpoint = 'https://127.0.0.1:' + str(udp_port) + '/echo'
    copied_server = copied_lab / 'server.mjs'
    original = copied_server.read_text()
    edits = [('https://127.0.0.1:4433/echo', endpoint),
             ('https://localhost:4433 https://127.0.0.1:4433;',
              'https://localhost:' + str(udp_port) + ' https://127.0.0.1:' + str(udp_port) + ';')]
    for before, after in edits:
        need(original.count(before) == 1, 'The copied lab endpoint/CSP shape changed; review it first')
        original = original.replace(before, after)
    copied_server.write_text(original)
    copy_hashes = {name: digest(copied_lab / name) for name in LAB_FILES}
    legacy_paths = {'certificate': lab / 'output/wt-certificate.json', 'audit': lab / 'output/webtransport-events.jsonl'}
    legacy_before = {name: metadata(path, name == 'certificate') for name, path in legacy_paths.items()}
    result_path = out / (args.name + '-acceptance.json')
    result = {'schemaVersion': 2, 'name': args.name, 'running': True, 'passed': False,
              'scope': 'Actual normal-bundle native WebTransport with isolated real HTTP/3 fixture',
              'nativeGate': selected_gate, 'nativeAssertionCount': gate['nativeAssertionCount'],
              'relinkReport': relink_reference, 'candidateToRelinkToNativeGateVerified': True,
              'bundleMetadata': args.metadata, 'sourceHashes': tracked, 'copiedLabSHA256': copy_hashes,
              'copiedServerEndpointOnlyAdaptation': True, 'origin': origin, 'endpoint': endpoint,
              'legacyPortReservedOrSignalled': False, 'privateKeyReadByObserver': False,
              'payloadsOrCookiesRecorded': False, 'signalsRestrictedToOwnedProcesses': True,
              'compositorPixelsIndependentlyReviewed': False, 'performanceComparisonClaimed': False,
              'bounds': {'runSeconds': 55, 'ownedRSSMiB': 3072, 'freeDiskMiB': 1024, 'monitorMaxAgeSeconds': 6},
              'startUnixSeconds': time.time(), 'samples': []}
    http = quic = browser = None
    owned_browser = {}
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def record():
        temporary = result_path.with_suffix('.json.tmp')
        temporary.write_text(json.dumps(result, indent=2) + '\n')
        temporary.replace(result_path)

    def interrupted(signum, frame):
        raise KeyboardInterrupt('Owned browser interval interrupted')
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, interrupted)
    record()
    try:
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith(('LUMEN_', 'LUME_LAB_'))}
        environment.update(PYTHONDONTWRITEBYTECODE='1', LUME_LAB_HOST='127.0.0.1',
                           LUME_LAB_PORT=str(tcp_port), LUME_LAB_ORIGINS=origin)
        with (case / 'http.log').open('xb') as handle:
            http = subprocess.Popen([shutil.which('node'), str(copied_server)], cwd=copied_lab,
                                    env=environment, stdin=subprocess.DEVNULL, stdout=handle, stderr=subprocess.STDOUT)
        with (case / 'http3.log').open('xb') as handle:
            quic = subprocess.Popen([str(interpreter), '-u', str(copied_lab / 'webtransport_server.py'),
                                     '--host', '127.0.0.1', '--port', str(udp_port)], cwd=copied_lab,
                                    env=environment, stdin=subprocess.DEVNULL, stdout=handle, stderr=subprocess.STDOUT)
        result.update(ownedHTTPServerPID=http.pid, ownedHTTP3ServerPID=quic.pid)
        record()
        certificate = copied_lab / 'output/wt-certificate.json'
        audit = copied_lab / 'output/webtransport-events.jsonl'
        due = time.monotonic() + 8
        config = None
        while time.monotonic() < due:
            need(http.poll() is None and quic.poll() is None, 'A newly owned fixture exited before readiness')
            if certificate.is_file():
                try:
                    with opener.open(origin + '/api/config', timeout=.75) as response:
                        config = json.loads(response.read(65537))
                    if config.get('webtransport', {}).get('available') is True:
                        break
                except (OSError, ValueError):
                    pass
            time.sleep(.1)
        need(config is not None and config.get('webtransport', {}).get('available') is True,
             'The fresh copied lab did not become ready')
        public = load(certificate, 65536)
        pin = public.get('certificateHash')
        need(public.get('transport') == 'HTTP/3 only' and public.get('expiresAt', 0) > time.time() * 1000
             and isinstance(pin, list) and len(pin) == 32 and all(type(n) is int and 0 <= n <= 255 for n in pin),
             'Fresh public certificate manifest is invalid')
        need(config['webtransport'].get('url') == endpoint and config['webtransport'].get('certificateHash') == pin,
             'The page is not configured for this owned endpoint and pin')
        need(not audit.exists(), 'Unexpected traffic before the exclusive browser interval')
        certificate_sha = digest(certificate)
        result.update(ownedListenerIdentities=[verify_listener(http, 'TCP', tcp_port), verify_listener(quic, 'UDP', udp_port)],
                      certificateManifestSHA256=certificate_sha, auditStartBytes=0,
                      browserStartUnixSeconds=time.time())
        # The driver uses its own fresh empty profile. It never copies Chrome credentials.
        with (case / 'browser-driver.log').open('xb') as handle:
            browser = subprocess.Popen([sys.executable, 'verification/bundle_case.py',
                '--metadata', args.metadata, '--name', args.name, '--kind', 'showcase',
                '--probe', 'verification/webtransport-only-probe.js', '--url', origin + '/',
                '--duration', '40', '--settle', '25'], cwd=root,
                env=environment, stdin=subprocess.DEVNULL, stdout=handle, stderr=subprocess.STDOUT)
        result['ownedBrowserDriverPID'] = browser.pid
        started = time.monotonic()
        next_sample, prior, previous = started, None, {}
        while browser.poll() is None:
            processes = sample_processes()
            now = time.monotonic()
            add_cpu_deltas(processes, previous, now - prior if prior else 0)
            previous, prior = {p['pid']: p for p in processes}, now
            candidates = matching_pids(executable)
            need(len(candidates) <= 1, 'More than one candidate executable appeared')
            for pid in candidates:
                current = process_identity(pid, executable)
                need(current is not None, 'Candidate process identity changed while observed')
                if pid in owned_browser:
                    need(owned_browser[pid] == current, 'Candidate PID was reused')
                owned_browser[pid] = current
            selected = {p['pid']: p for p in descendants(processes, os.getpid())}
            for pid in owned_browser:
                selected.update({p['pid']: p for p in descendants(processes, pid)})
            owned = list(selected.values())
            cpus = [p['cpu_interval_percent'] for p in owned if p['cpu_interval_percent'] is not None]
            sample = {'elapsedSeconds': round(now - started, 3),
                      'cpuOneCorePercent': sum(cpus) if cpus else None,
                      'rssMiBSum': sum(p['rss_kib'] for p in owned) / 1024,
                      'freeDiskMiB': shutil.disk_usage(root).free / (1024 * 1024),
                      'monitorAgeSeconds': time.time() - monitor.stat().st_mtime if monitor.exists() else None}
            result['samples'].append(sample)
            need(http.poll() is None and quic.poll() is None, 'An owned fixture exited during the browser interval')
            need(sample['monitorAgeSeconds'] is not None and 0 <= sample['monitorAgeSeconds'] < 6,
                 'The global monitor became stale')
            need(now - started < 55 and sample['rssMiBSum'] < 3072 and sample['freeDiskMiB'] >= 1024,
                 'The owned browser interval exceeded its time, memory or disk bound')
            record()
            next_sample += 1
            time.sleep(max(0, next_sample - time.monotonic()))
        result.update(runnerExitCode=browser.returncode, browserEndUnixSeconds=time.time())
        summary = load(out / (args.name + '.json'), 4 * 1024 * 1024)
        need(browser.returncode == 0, 'The actual bundle driver did not complete its requested workload')
        need(summary.get('actual_browser_pid') in owned_browser and
             summary.get('binary_sha256') == bundle['binarySha256'] and
             summary.get('source_binary_sha256') == bundle['binaryBeforeBundleSigningSha256'],
             'Browser summary is not from the observed exact candidate')
        need(audit.is_file() and audit.stat().st_size <= 262144, 'Independent bounded HTTP/3 audit is absent')
        events = [json.loads(line) for line in audit.read_text().splitlines() if line]
        need(events and all(isinstance(row, dict) and row.get('event') in ('datagram', 'stream') and
             type(row.get('echoBytes')) is int and row['echoBytes'] >= 0 and
             type(row.get('time')) in (int, float) for row in events), 'Invalid independent server events')
        contracts, environments = [], []
        for line in summary.get('evidence', []):
            if 'LUME_LAB_EVIDENCE ' in line:
                value = json.loads(line.split('LUME_LAB_EVIDENCE ', 1)[1])
                if value.get('kind') == 'transport-contract':
                    contracts.append(value.get('payload', {}))
                if value.get('kind') == 'environment':
                    environments.append(value.get('payload', {}))
        need(len(contracts) == 1, 'Exactly one real immutable backend declaration is required')
        need(len(environments) == 1 and environments[0].get('origin') == origin and
             environments[0].get('secureContext') is True,
             'The actual browser document must be the new secure loopback origin')
        unchanged = all(digest(root / name) == expected for name, expected in tracked.items())
        copied_unchanged = all(digest(copied_lab / name) == expected for name, expected in copy_hashes.items())
        need(unchanged and copied_unchanged and digest(certificate) == certificate_sha,
             'Source, candidate, native gate or fixture changed during the browser interval')
        acceptance = assess(summary, contracts[0], events, result['browserStartUnixSeconds'],
                            result['browserEndUnixSeconds'], unchanged and copied_unchanged)
        result.update(acceptance)
        capture = load(out / (args.name + '.overview.capture.log'), 65536)
        png = out / (args.name + '.overview.png')
        captured = capture.get('testPID') == summary['actual_browser_pid'] and capture.get('path') == str(png) and \
            capture.get('onScreen') is True and capture.get('targetActive') is True and \
            capture.get('frontmostMatchesTarget') is True and png.is_file() and png.stat().st_size > 100
        if captured:
            with png.open('rb') as handle:
                captured = handle.read(8) == b'\x89PNG\r\n\x1a\n'
        final_visibility = summary.get('window_visibility', {}).get('final', {})
        visible = all(final_visibility.get(key) is True for key in
                      ('ownedWindowFound', 'ownedWindowOnScreen', 'active', 'frontmostMatchesTarget'))
        result.update(processPID=summary['actual_browser_pid'], binarySHA256=summary['binary_sha256'],
                      sourceBinarySHA256=summary['source_binary_sha256'],
                      candidateProcessIdentityVerified=True, windowVisibility=summary.get('window_visibility'),
                      actualOwnedCompositorCapture=capture, actualOwnedCompositorCapturePassed=captured,
                      observedBrowserEnvironment=environments[0],
                      compositorCapture=str(png.relative_to(root)), candidateVisibleAtEnd=visible,
                      auditEndBytes=audit.stat().st_size, serverEventDelta=events,
                      copiedLabUnchangedDuringRun=copied_unchanged)
        result['passed'] = bool(acceptance['passed'] and captured and visible)
    except (Exception, KeyboardInterrupt) as error:
        result.update(passed=False, error=type(error).__name__ + ': ' + str(error)[:400])
    finally:
        cleanup_errors = []
        try:
            child_stop(browser)
        except Exception as error:
            cleanup_errors.append('browser-driver: ' + type(error).__name__)
        # LaunchServices children are not descendants of the Python driver.
        # Signal only the fresh candidate PID with the same start time and path.
        for pid, identity in owned_browser.items():
            try:
                if process_identity(pid, executable) == identity:
                    os.kill(pid, signal.SIGTERM)
                    deadline = time.monotonic() + 3
                    while process_identity(pid, executable) == identity and time.monotonic() < deadline:
                        time.sleep(.1)
                    if process_identity(pid, executable) == identity:
                        os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            except Exception as error:
                cleanup_errors.append('owned-candidate: ' + type(error).__name__)
        for label, process in (('http-fixture', http), ('http3-fixture', quic)):
            try:
                child_stop(process)
            except Exception as error:
                cleanup_errors.append(label + ': ' + type(error).__name__)
        candidates_stopped = False
        legacy_unchanged = False
        try:
            candidates_stopped = all(process_identity(pid, executable) != identity
                                     for pid, identity in owned_browser.items())
            legacy_unchanged = legacy_before == {
                name: metadata(path, name == 'certificate') for name, path in legacy_paths.items()}
        except Exception as error:
            cleanup_errors.append('cleanup-observation: ' + type(error).__name__)
        result.update(ownedHTTPServerStopped=http is None or http.poll() is not None,
                      ownedHTTP3ServerStopped=quic is None or quic.poll() is not None,
                      ownedHTTPServerExit=http.returncode if http is not None else None,
                      ownedHTTP3ServerExit=quic.returncode if quic is not None else None,
                      ownedBrowserDriverStopped=browser is None or browser.poll() is not None,
                      ownedCandidateStopped=candidates_stopped,
                      legacyFixtureFilesUnchanged=legacy_unchanged,
                      cleanupErrors=cleanup_errors,
                      running=False, endUnixSeconds=time.time())
        result['passed'] = bool(result['passed'] and not cleanup_errors and
            result['ownedHTTPServerExit'] == 0 and result['ownedHTTP3ServerExit'] == 0 and all(result[key] for key in
            ('ownedHTTPServerStopped', 'ownedHTTP3ServerStopped', 'ownedBrowserDriverStopped',
             'ownedCandidateStopped', 'legacyFixtureFilesUnchanged')))
        record()
    print(json.dumps({key: value for key, value in result.items() if key not in ('samples', 'sourceHashes')}, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
