#!/usr/bin/env python3
"""Reject false positive independent gate summaries; not a network test."""
import copy
import unittest
from run_live_gate import GateFailure, validate_audit, validate_protocol


def fixture():
    positive = {'state': 'pass', 'endpointClosed': True, 'openConnectionsAfterClose': 0, 'alpn': 'h3', 'transport': 'QUIC over UDP', 'datagramEchoMatched': True, 'streamEchoMatched': True, 'streamFinished': True, 'freshNonces': True, 'udpBytesSent': 4096, 'udpBytesReceived': 4096, 'datagramPayloadBytes': 49, 'streamPayloadBytes': 47}
    negative = {'state': 'pass', 'endpointClosed': True, 'openConnectionsAfterClose': 0, 'connectionRejected': True, 'certificateSpecificFailureObserved': True}
    return {'state': 'pass', 'implementation': 'wtransport-0.7.2', 'lumenBrowserSupport': False, 'browserConstructorExposed': False, 'cases': [dict(positive, name='pinned-http3-before-controls'), dict(negative, name='wrong-pin-rejected', certificateMode='constrained-sha256-hash'), dict(negative, name='native-roots-reject-self-signed', certificateMode='normal-native-roots'), dict(positive, name='pinned-http3-after-controls')]}


class GateChecks(unittest.TestCase):
    def test_synthetic_valid_summary_and_exact_counts(self):
        expected = validate_protocol(fixture())
        events = [{'transport': 'h3-quic', 'event': kind, 'echoBytes': length} for kind, length in [('datagram', 49), ('stream', 47), ('datagram', 49), ('stream', 47)]]
        result = validate_audit(events, expected)
        self.assertEqual(result['eventCounts'], {'datagram': 2, 'stream': 2})

    def test_timeout_or_generic_error_cannot_pass_certificate_control(self):
        report = fixture()
        report['cases'][1]['certificateSpecificFailureObserved'] = False
        with self.assertRaises(GateFailure): validate_protocol(report)

    def test_missing_teardown_or_wire_evidence_cannot_pass(self):
        for field, value in [('endpointClosed', False), ('udpBytesSent', 0), ('udpBytesSent', True), ('streamFinished', False), ('alpn', 'h2')]:
            report = fixture()
            report['cases'][0][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(GateFailure): validate_protocol(report)

    def test_missing_or_unrelated_server_traffic_cannot_pass(self):
        expected = validate_protocol(fixture())
        with self.assertRaises(GateFailure): validate_audit([], expected)
        events = [{'transport': 'h3-quic', 'event': kind, 'echoBytes': length} for kind, length in [('datagram', 49), ('stream', 47), ('datagram', 49), ('stream', 47), ('datagram', 12)]]
        with self.assertRaises(GateFailure): validate_audit(events, expected)


if __name__ == '__main__':
    unittest.main()
