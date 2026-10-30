#!/usr/bin/env python3
"""Independent Chrome/Lume data-channel proof, with process identity gates.

Peer pages attest nonce/cleanup facts only. This harness supplies process and
engine identity, and never promotes a page's self-reported cross-browser flag.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid


def assess(peers, processes, room):
    failures = []
    for engine, role in [('chrome', 'offer'), ('lumen', 'answer')]:
        peer, process = peers.get(engine, {}), processes.get(engine, {})
        for key in ('payloadMatched', 'independentPeerEcho', 'peerCompletionAcknowledged', 'gracefulChannelCloseObserved', 'peerChannelCloseAcknowledged', 'resourcesReleased'):
            if peer.get(key) is not True:
                failures.append(engine + ':' + key)
        if peer.get('schemaVersion') != 4 or peer.get('state') != 'pass':
            failures.append(engine + ':peer-final-schema-or-state')
        if peer.get('dataChannelReadyState') != 'closed':
            failures.append(engine + ':channel-close-state')
        if type(peer.get('dataChannelErrorCount')) is not int or peer['dataChannelErrorCount'] != 0:
            failures.append(engine + ':channel-transport-error')
        echo_sequence, close_sequence = peer.get('peerCompletionSignalSequence'), peer.get('peerChannelCloseSignalSequence')
        if type(echo_sequence) is not int or type(close_sequence) is not int or not 0 < echo_sequence < close_sequence:
            failures.append(engine + ':peer-close-acknowledgement-order')
        if type(peer.get('completionSignalsSent')) is not int or peer['completionSignalsSent'] != 2:
            failures.append(engine + ':completion-phase-count')
        if peer.get('room') != room or peer.get('role') != role:
            failures.append(engine + ':fresh-room-or-role')
        if peer.get('connectionState') != 'connected':
            failures.append(engine + ':connection-not-connected')
        for key in ('dataChannelBytesSent', 'dataChannelBytesReceived'):
            value = peer.get(key)
            if type(value) not in (int, float) or not value > 0:
                failures.append(engine + ':' + key)
        if type(process.get('process_pid')) is not int or process.get('process_pid', 0) <= 1:
            failures.append(engine + ':missing-owned-pid')
        if process.get('engine') != engine or not process.get('binary_fingerprint', {}).get('sha256'):
            failures.append(engine + ':missing-engine-or-binary-identity')
        if process.get('sample_count', 0) < 10:
            failures.append(engine + ':insufficient-owned-lifetime-evidence')
    chrome, lumen = processes.get('chrome', {}), processes.get('lumen', {})
    if chrome.get('process_pid') == lumen.get('process_pid'):
        failures.append('process-ids-not-distinct')
    if chrome.get('binary_fingerprint', {}).get('sha256') == lumen.get('binary_fingerprint', {}).get('sha256'):
        failures.append('engine-binaries-not-distinct')
    descriptor = peers.get('lumen', {}).get('engineCapabilities') or {}
    if descriptor.get('webrtcNetworkTransport') is not True or descriptor.get('webrtcBackend') != 'libdatachannel-0.24.6':
        failures.append('native-real-backend-not-declared')
    if peers.get('chrome', {}).get('dtlsState') != 'connected':
        failures.append('chrome-dtls-not-connected')
    return {
        'passed': not failures, 'failedGates': failures,
        'processSeparationVerified': bool(chrome.get('process_pid') and lumen.get('process_pid') and chrome['process_pid'] != lumen['process_pid']),
        'crossBrowserInteroperability': not failures,
        'coverage': 'Two separate known Chrome/Lume processes; fresh bidirectional nonce echo, both peer acknowledgements, actual graceful channel-close events without transport errors, both close-event acknowledgements before peer disposal, and resource release.',
        'mediaRtpVerified': False, 'externalStunTurnVerified': False,
        'visibleRenderingVerified': False, 'comparativePerformanceMeasured': False,
    }


def final_peer(path):
    matches = []
    if path.exists():
        for line in path.read_text(errors='replace').splitlines():
            if 'LUME_RTC_INTEROP ' not in line:
                continue
            try:
                value = json.loads(line.split('LUME_RTC_INTEROP ', 1)[1])
            except ValueError:
                continue
            if isinstance(value, dict):
                matches.append(value)
    return matches[-1] if matches else {}


def main():
    out = Path('out/verification')
    port = (out / 'lab-server-port.txt').read_text().strip()
    if not port.isdecimal() or not 1024 <= int(port) <= 65535:
        raise SystemExit('Invalid task-owned lab port')
    room = 'lume-real-' + uuid.uuid4().hex
    run_tag = room[-12:]
    base = 'http://127.0.0.1:' + port + '/rtc-peer.html?room=' + room + '&autostart=1&role='
    chrome_path = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
    commands = {
        'chrome': ['python3', 'verification/native_case.py', '--engine', 'chrome', '--binary', chrome_path,
                   '--name', 'real-rtc-chrome-' + run_tag, '--kind', 'rtc', '--url', base + 'offer', '--duration', '32', '--settle', '22'],
        'lumen': ['python3', 'verification/native_case.py', '--name', 'real-rtc-lumen-' + run_tag, '--kind', 'rtc',
                  '--url', base + 'answer', '--duration', '32', '--settle', '22', '--shot-delay-ms', '60000'],
    }
    env = os.environ.copy()
    env.pop('LUMEN_EXPERIMENTAL_RTC_LOOPBACK', None)
    jobs, handles = {}, []
    for engine, command in commands.items():
        handle = (out / ('real-rtc-' + engine + '-' + run_tag + '.driver.log')).open('w')
        handles.append(handle)
        jobs[engine] = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT, env=env)
    exits = {engine: job.wait() for engine, job in jobs.items()}
    for handle in handles:
        handle.close()
    peers, processes = {}, {}
    for engine in commands:
        prefix = out / ('real-rtc-' + engine + '-' + run_tag)
        peers[engine] = final_peer(Path(str(prefix) + '.log'))
        try:
            processes[engine] = json.loads(Path(str(prefix) + '.json').read_text())
        except (OSError, ValueError):
            processes[engine] = {}
    assessment = assess(peers, processes, room)
    result = {
        'schemaVersion': 1, 'room': room, 'assessment': assessment, 'driverExitCodes': exits,
        'peerPageSha256': hashlib.sha256(Path('tools/web-capability-lab/rtc-peer.mjs').read_bytes()).hexdigest(),
        'peerCoreSha256': hashlib.sha256(Path('tools/web-capability-lab/core.mjs').read_bytes()).hexdigest(),
        'peers': peers,
        'processes': {engine: {key: value for key, value in result.items() if key != 'samples'} for engine, result in processes.items()},
    }
    target = out / ('real-rtc-interop-' + run_tag + '.json')
    target.write_text(json.dumps(result, indent=2))
    (out / 'real-rtc-interop-latest.json').write_text(json.dumps({'result': str(target)}, indent=2))
    print(json.dumps(result, indent=2))
    return 0 if assessment['passed'] and all(code == 0 for code in exits.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
