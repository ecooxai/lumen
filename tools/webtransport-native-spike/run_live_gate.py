#!/usr/bin/env python3
"""Independent count-only server evidence for the standalone HTTP3 client."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time


class GateFailure(Exception):
    pass


def positive_integer(value):
    return type(value) is int and value > 0


def validate_protocol(protocol):
    expected = ['pinned-http3-before-controls', 'wrong-pin-rejected', 'native-roots-reject-self-signed', 'pinned-http3-after-controls']
    cases = protocol.get('cases', [])
    if protocol.get('state') != 'pass' or len(cases) != 4 or [case.get('name') for case in cases] != expected:
        raise GateFailure('protocol-cases-incomplete')
    if protocol.get('implementation') != 'wtransport-0.7.2' or protocol.get('lumenBrowserSupport') is not False or protocol.get('browserConstructorExposed') is not False:
        raise GateFailure('protocol-scope-invalid')
    for case in cases:
        if case.get('state') != 'pass' or case.get('endpointClosed') is not True or case.get('openConnectionsAfterClose') != 0:
            raise GateFailure('protocol-case-or-teardown-failed')
    for case in (cases[0], cases[3]):
        if case.get('alpn') != 'h3' or case.get('transport') != 'QUIC over UDP':
            raise GateFailure('actual-http3-not-established')
        if any(case.get(field) is not True for field in ['datagramEchoMatched', 'streamEchoMatched', 'streamFinished', 'freshNonces']):
            raise GateFailure('fresh-echo-or-stream-eof-missing')
        if any(not positive_integer(case.get(field)) for field in ['udpBytesSent', 'udpBytesReceived', 'datagramPayloadBytes', 'streamPayloadBytes']):
            raise GateFailure('measured-byte-counters-missing')
    for case in (cases[1], cases[2]):
        if case.get('connectionRejected') is not True or case.get('certificateSpecificFailureObserved') is not True:
            raise GateFailure('certificate-negative-control-failed')
    if cases[1].get('certificateMode') != 'constrained-sha256-hash' or cases[2].get('certificateMode') != 'normal-native-roots':
        raise GateFailure('certificate-negative-control-mode-invalid')
    return {
        'datagram': sum(case['datagramPayloadBytes'] for case in (cases[0], cases[3])),
        'stream': sum(case['streamPayloadBytes'] for case in (cases[0], cases[3])),
    }


def validate_audit(events, expected):
    counts = {'datagram': 0, 'stream': 0}
    byte_counts = {'datagram': 0, 'stream': 0}
    for event in events:
        kind = event.get('event')
        length = event.get('echoBytes')
        if event.get('transport') != 'h3-quic' or kind not in counts or type(length) is not int or length < 0:
            raise GateFailure('server-telemetry-invalid')
        counts[kind] += 1
        byte_counts[kind] += length
    if counts['datagram'] != 2 or counts['stream'] < 2 or byte_counts != expected:
        raise GateFailure('server-echo-telemetry-mismatch-or-concurrent-probe')
    return {'eventCounts': counts, 'echoPayloadBytes': byte_counts, 'transport': 'h3-quic', 'payloadRecorded': False}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    base = Path(__file__).resolve().parent
    output = base / 'output'
    lab_output = base.parent / 'web-capability-lab' / 'output'
    manifest = lab_output / 'wt-certificate.json'
    audit = lab_output / 'webtransport-events.jsonl'
    report = {'schemaVersion': 1, 'state': 'fail', 'scope': 'standalone-native-http3-experiment', 'lumenBrowserSupport': False, 'browserAPIExposed': False}
    path = output / 'GPT6-AstraPro-subagents-WebTransport-independent-gate.json'
    if path.exists():
        print(json.dumps({'state': 'blocked', 'reason': 'existing-gate-evidence-preserved'}))
        return 2
    try:
        build = json.loads((output / 'GPT6-AstraPro-build.measurement.json').read_text())
        if build.get('exitCode') != 0 or build.get('processExited') is not True or any(build.get(field) is not False for field in ['timedOut', 'memoryLimitExceeded', 'observerFailed']):
            raise GateFailure('strict-build-gate-not-passed')
        if not audit.exists() or not manifest.exists():
            raise GateFailure('http3-server-evidence-unavailable')
        start_stat = audit.stat()
        manifest_hash = digest(manifest)
        report['startedAtUnixSeconds'] = time.time()
        with (output / 'gate-observer.log').open('wb') as log:
            observed = subprocess.run([sys.executable, str(base / 'run_measured.py'), '--label', 'GPT6-AstraPro-http3-gate', '--timeout', '70', '--max-rss-mib', '512', '--', str(base / 'target/debug/lumen-webtransport-native-spike'), '--certificate-manifest', str(manifest)], stdout=log, stderr=subprocess.STDOUT, cwd=base)
        report['finishedAtUnixSeconds'] = time.time()
        measurement = json.loads((output / 'GPT6-AstraPro-http3-gate.measurement.json').read_text())
        report['measurement'] = {key: value for key, value in measurement.items() if key != 'samples'}
        report['fingerprints'] = {name: digest(base / name) for name in ['src/main.rs', 'Cargo.lock', 'target/debug/lumen-webtransport-native-spike']}
        protocol = json.loads((output / 'GPT6-AstraPro-subagents-WebTransport-gate.json').read_text())
        report['protocol'] = protocol
        if observed.returncode != 0 or measurement.get('exitCode') != 0 or measurement.get('processExited') is not True or any(measurement.get(field) is not False for field in ['timedOut', 'memoryLimitExceeded', 'observerFailed']):
            raise GateFailure('measured-client-failed-or-not-released')
        expected = validate_protocol(protocol)
        final_stat = audit.stat()
        if final_stat.st_ino != start_stat.st_ino or final_stat.st_size < start_stat.st_size or final_stat.st_size - start_stat.st_size > 524288 or digest(manifest) != manifest_hash:
            raise GateFailure('server-evidence-changed-or-unbounded')
        with audit.open('rb') as handle:
            handle.seek(start_stat.st_size)
            delta = handle.read(524289)
        events = [json.loads(line) for line in delta.splitlines() if line]
        report['serverEvidence'] = validate_audit(events, expected)
        report['serverEvidence']['auditStartOffset'] = start_stat.st_size
        report['serverEvidence']['auditEndOffset'] = final_stat.st_size
        report['state'] = 'pass'
    except GateFailure as failure:
        report['reason'] = str(failure)
    except (OSError, ValueError, TypeError, KeyError, subprocess.SubprocessError):
        report['reason'] = 'evidence-unreadable-or-invalid'
    output.mkdir(exist_ok=True)
    path.write_text(json.dumps(report, indent=2))
    print(json.dumps({key: value for key, value in report.items() if key not in ['protocol']}))
    return 0 if report['state'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
