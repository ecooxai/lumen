import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('fixture_server', Path(__file__).with_name('fixture_server.py'))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class FixtureServerTests(unittest.TestCase):
    def test_loopback_startup_never_performs_reverse_dns(self):
        # Isolate resolver behavior without requiring a listening network socket.
        with patch('socket.socket') as socket, patch('socket.getfqdn', side_effect=AssertionError('unexpected reverse DNS')):
            socket.return_value.getsockname.return_value = ('127.0.0.1', 43123)
            with fixture.LoopbackHTTPServer(('127.0.0.1', 0), fixture.BaseHTTPRequestHandler) as server:
                self.assertEqual(server.server_name, 'localhost')
                self.assertEqual(server.server_port, 43123)

    def test_records_browser_events_without_mixing_test_runs(self):
        store = fixture.EventStore()
        store.record({'run': 'phone-A', 'type': 'ready', 'width': 393, 'height': 715})
        store.record({'run': 'phone-A', 'type': 'click', 'target': 'tap'})
        store.record({'run': 'tablet-B', 'type': 'input', 'value': 'hello'})
        self.assertEqual(store.state('phone-A')['clicks'], ['tap'])
        self.assertEqual(store.state('phone-A')['width'], 393)
        self.assertEqual(store.state('tablet-B')['value'], 'hello')
        self.assertNotIn('value', store.state('phone-A'))

    def test_rejects_path_like_or_unbounded_run_names(self):
        store = fixture.EventStore()
        for run in ('', '../other', 'A' * 65, 'slash/name', None):
            with self.assertRaises(ValueError):
                store.record({'run': run, 'type': 'ready'})

    def test_caps_diagnostic_history(self):
        store = fixture.EventStore()
        for i in range(300):
            store.record({'run': 'A', 'type': 'scroll', 'scrollY': i})
        self.assertEqual(len(store.state('A')['events']), 128)
        self.assertEqual(store.state('A')['scrollY'], 299)

    def test_monitor_matches_only_the_selected_installed_binary(self):
        raw = '10 1.2 200 /some/other/Lumen.app/Lumen\n11 0.5 300 /chosen/Lumen.app/Lumen\n'
        self.assertEqual(fixture.match_pids(raw, '/chosen/Lumen.app/Lumen'), [11])
        self.assertEqual(fixture.match_pids(raw, '/missing/Lumen.app/Lumen'), [])


if __name__ == '__main__':
    unittest.main()
