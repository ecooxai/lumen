#!/usr/bin/env python3
"""Two bounded real simulator cases for the demonstrated CSS defects only."""
from pathlib import Path
import hashlib
import json
import os
import signal
import subprocess
import sys
import time

ROOT = Path('/Users/devin/repos/lumen')
EXPECTED_BINARY = 'b7e581006bd0afc77aabddfcf0348478c3baa59c8d996101cd5c9609efd20b73'
CSS = {'style.css': '905df7a5f16ce7ea93d700996e791e6486ac85551364b7e305d416907cfae4de',
       'frame.css': '66cddc6f3bae1a7820063324149371da16b8e0f6aeaf671a82e9bfff0fb69005'}
if sys.platform != 'darwin' or Path.cwd().resolve() != ROOT:
    raise SystemExit('Unexpected simulator CSS verification workspace')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, reason):
    if not condition:
        raise RuntimeError(reason)


out = ROOT / 'out/verification'
probe = ROOT / 'verification/responsive_layout_probe_v1.js'
driver = ROOT / 'verification/simulator_browser_case.py'
monitor = out / 'resource-monitor.jsonl'
report_path = out / 'responsive-css-v1-acceptance.json'
require(not report_path.exists(), 'Preserve the existing responsive acceptance attempt')
for name, expected in CSS.items():
    require(sha(ROOT / 'tools/web-capability-lab' / name) == expected, 'Reviewed candidate CSS differs: ' + name)
sources = [probe, driver] + [ROOT / 'tools/web-capability-lab' / name for name in
          ('style.css', 'frame.css', 'app.mjs', 'core.mjs', 'index.html', 'iframe-probe.html', 'iframe-probe.mjs')]
source_hashes = {str(path.relative_to(ROOT)): sha(path) for path in sources}
port = (out / 'lab-server-port.txt').read_text().strip()
require(port.isdigit() and 0 < int(port) < 65536, 'Invalid owned lab port')
result = {'schemaVersion': 1, 'scope': 'Actual iPhone/iPad CSS geometry and separate GPU/iframe captures',
          'startedUTCSeconds': time.time(), 'sourceHashes': source_hashes, 'cases': [],
          'stageMapping': {'overview': 'webgl', 'gpu': 'webgpu', 'network': 'iframe'},
          'visualContentReviewed': False, 'physicalDeviceClaimed': False, 'performanceClaimed': False}
active = None


def stop_owned_driver():
    if active is not None and active.poll() is None:
        os.killpg(active.pid, signal.SIGINT)
        try:
            active.wait(timeout=15)
        except subprocess.TimeoutExpired:
            os.killpg(active.pid, signal.SIGTERM)
            active.wait(timeout=5)


def interrupted(signum, frame):
    raise KeyboardInterrupt('Responsive verification interrupted')


for signal_number in (signal.SIGTERM, signal.SIGINT):
    signal.signal(signal_number, interrupted)
try:
    for device in ('iphone', 'ipad'):
        name = 'responsive-css-v1-' + device
        require(not any(out.glob(name + '.*')), 'Preserve existing device evidence: ' + name)
        require(monitor.is_file() and 0 <= time.time() - monitor.stat().st_mtime < 6,
                'Required one-second resource observer is stale')
        ps = subprocess.check_output(['ps', '-axo', 'comm='], text=True)
        forbidden = {'clang', 'clang++', 'ld', 'rustc', 'cargo', 'ninja', 'xcodebuild', 'swift-frontend'}
        require(not any(Path(line.strip()).name in forbidden for line in ps.splitlines()),
                'Wait for the reserved compiler-free GUI slot')
        with (out / (name + '.driver.log')).open('x') as handle:
            active = subprocess.Popen([sys.executable, str(driver), '--device', device, '--name', name,
                '--url', 'http://127.0.0.1:' + port + '/', '--probe', str(probe),
                '--capture-markers', '--duration', '27'], cwd=ROOT, stdin=subprocess.DEVNULL,
                stdout=handle, stderr=subprocess.STDOUT, start_new_session=True)
            begin = time.monotonic()
            while active.poll() is None:
                require(time.monotonic() - begin < 43, 'Bounded simulator case did not finish')
                require(monitor.is_file() and 0 <= time.time() - monitor.stat().st_mtime < 6,
                        'Required global observer became stale')
                time.sleep(1)
            require(active.returncode == 0, 'Simulator case did not exit successfully')
        summary = json.loads((out / (name + '.json')).read_text())
        require(summary.get('binary_fingerprint', {}).get('sha256') == EXPECTED_BINARY,
                'Installed Apple executable differs from the accepted baseline')
        records = []
        log = (out / (name + '.log')).read_text(errors='replace')
        require(len(log) < 1024 * 1024, 'Responsive diagnostic log exceeded its bound')
        for line in log.splitlines():
            if 'LUME_LAB_EVIDENCE ' in line:
                records.append(json.loads(line.split('LUME_LAB_EVIDENCE ', 1)[1]))
        geometry = {entry['payload']['id']: entry['payload'] for entry in records
                    if entry.get('kind') == 'responsive-geometry'}
        errors = [entry.get('payload') for entry in records if entry.get('kind') == 'responsive-error']
        completed = any(entry.get('kind') == 'responsive-completed' for entry in records)
        api_passed = all(summary.get('lab_results', {}).get(capability, {}).get('state') == 'pass'
                         for capability in ('webgl', 'webgpu', 'iframe'))
        canvas_passed = all(geometry.get(capability, {}).get('canvas', {}).get('fullCanvasInsideViewport') is True
            and geometry[capability]['canvas'].get('intrinsicAspectPreserved') is True
            and geometry[capability]['canvas'].get('objectFit') == 'contain'
            for capability in ('webgl', 'webgpu'))
        frame = geometry.get('iframe', {}).get('iframe', {})
        iframe_passed = frame.get('fullFrameInsideViewport') is True and frame.get('allButtonsInsideChildViewport') is True
        phone_passed = device != 'iphone' or all(entry.get('singlePhoneColumnObserved') is True for entry in geometry.values())
        captures = summary.get('owned_simulator_captures', {})
        pngs = {}
        for stage in ('overview', 'gpu', 'network'):
            record = captures.get(stage, {})
            path = out / (name + '.' + stage + '.png')
            valid = record.get('exit_code') == 0 and record.get('path') == str(path) and path.is_file() and path.stat().st_size > 100
            if valid:
                with path.open('rb') as stream:
                    valid = stream.read(8) == b'\x89PNG\r\n\x1a\n'
            pngs[stage] = {'path': str(path), 'validPNG': valid, 'sha256': sha(path) if valid else None}
        passed = (not summary.get('failure') and summary.get('sample_count', 0) >= 15
            and summary.get('samples_with_active_compilers') == 0 and api_passed and canvas_passed
            and iframe_passed and phone_passed and len(geometry) == 3 and not errors and completed
            and summary.get('lab_cleanup_verified') is True and all(item['validPNG'] for item in pngs.values()))
        case = {'device': device, 'passedGeometryAndCaptureGate': passed, 'geometry': geometry, 'errors': errors,
                'apiProbesPassed': api_passed, 'canvasGeometryPassed': canvas_passed,
                'iframeGeometryPassed': iframe_passed, 'singlePhoneColumnPassed': phone_passed,
                'sampleCount': summary.get('sample_count'), 'peakRSSMiB': summary.get('rss_peak_mib'),
                'sourceBinarySHA256': EXPECTED_BINARY, 'cleanupVerified': summary.get('lab_cleanup_verified'), 'captures': pngs}
        result['cases'].append(case)
        require(passed, 'Actual responsive geometry/capture gate failed for ' + device)
    result['sourcesUnchanged'] = all(sha(ROOT / name) == expected for name, expected in source_hashes.items())
    require(result['sourcesUnchanged'], 'Demo or observer source changed during the bounded cases')
    result['passedGeometryAndCaptureGate'] = True
except (Exception, KeyboardInterrupt) as error:
    result['failure'] = {'name': type(error).__name__, 'message': str(error)[:400]}
    result['passedGeometryAndCaptureGate'] = False
finally:
    stop_owned_driver()
    result['finishedUTCSeconds'] = time.time()
    report_path.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
if not result.get('passedGeometryAndCaptureGate'):
    raise SystemExit(1)
