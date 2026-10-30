import copy
import unittest
from rtc_interop_case import assess


class AcceptanceGateTests(unittest.TestCase):
    def setUp(self):
        self.peers = {name: dict(schemaVersion=4, state='pass', role=role, room='fresh-test-room',
            payloadMatched=True, independentPeerEcho=True, peerCompletionAcknowledged=True,
            gracefulChannelCloseObserved=True, dataChannelReadyState='closed', dataChannelErrorCount=0,
            peerChannelCloseAcknowledged=True, peerCompletionSignalSequence=10, peerChannelCloseSignalSequence=12, completionSignalsSent=2,
            resourcesReleased=True, connectionState='connected', dataChannelBytesSent=49,
            dataChannelBytesReceived=49, dtlsState='connected') for name, role in [('chrome', 'offer'), ('lumen', 'answer')]}
        self.peers['lumen']['engineCapabilities'] = {'webrtcNetworkTransport': True, 'webrtcBackend': 'libdatachannel-0.24.6'}
        self.processes = {name: {'engine': name, 'process_pid': pid, 'sample_count': 32,
            'binary_fingerprint': {'sha256': name * 10}} for name, pid in [('chrome', 120), ('lumen', 121)]}

    def test_accepts_complete_separate_engine_evidence(self):
        self.assertTrue(assess(self.peers, self.processes, 'fresh-test-room')['passed'])

    def test_page_claim_never_replaces_owned_process_identity(self):
        self.peers['lumen']['crossBrowserInteroperability'] = True
        self.processes['lumen']['process_pid'] = 120
        self.assertFalse(assess(self.peers, self.processes, 'fresh-test-room')['passed'])

    def test_missing_echo_ack_or_cleanup_cannot_pass(self):
        for engine in self.peers:
            for key in ('payloadMatched', 'independentPeerEcho', 'peerCompletionAcknowledged', 'gracefulChannelCloseObserved', 'peerChannelCloseAcknowledged', 'resourcesReleased'):
                with self.subTest(engine=engine, key=key):
                    peers = copy.deepcopy(self.peers); peers[engine][key] = False
                    self.assertFalse(assess(peers, self.processes, 'fresh-test-room')['passed'])

    def test_stale_room_and_synthetic_zero_bytes_cannot_pass(self):
        self.assertFalse(assess(self.peers, self.processes, 'another-room')['passed'])
        self.peers['lumen']['dataChannelBytesReceived'] = 0
        self.assertFalse(assess(self.peers, self.processes, 'fresh-test-room')['passed'])

    def test_unsupported_backend_cannot_pass(self):
        self.peers['lumen']['engineCapabilities']['webrtcNetworkTransport'] = False
        self.assertFalse(assess(self.peers, self.processes, 'fresh-test-room')['passed'])

    def test_error_or_unobserved_close_cannot_pass_after_successful_echo(self):
        for engine in self.peers:
            for changes in ({'dataChannelErrorCount': 1}, {'dataChannelErrorCount': False},
                            {'dataChannelReadyState': 'closing'}, {'schemaVersion': 3},
                            {'peerChannelCloseSignalSequence': 10}, {'peerChannelCloseSignalSequence': 9},
                            {'completionSignalsSent': 1}):
                with self.subTest(engine=engine, changes=changes):
                    peers = copy.deepcopy(self.peers); peers[engine].update(changes)
                    self.assertFalse(assess(peers, self.processes, 'fresh-test-room')['passed'])


if __name__ == '__main__':
    unittest.main()
