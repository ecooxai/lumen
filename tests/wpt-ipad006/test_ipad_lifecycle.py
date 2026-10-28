"""Retained evidence and platform-contract gates; execute only on the Mac."""
import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('ipad_lifecycle', Path(__file__).with_name('ipad_lifecycle.py'))
lifecycle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lifecycle)


def recovery():
    return {'status': 'recovered_owned_app_gone_original_cleanup_remains_failed',
            'device': lifecycle.DEVICE, 'bundle_id': lifecycle.BUNDLE,
            'original_test_cleanup_passed': False, 'performance_release_gate_passed': False,
            'original_row_sha256': lifecycle.ORIGINAL_ROW_SHA,
            'original_cleanup': {'stopped': False, 'sent_sigterm': True,
                                 'error': 'Owned iPad app did not terminate within the bounded cleanup window'},
            'after': {'identity': None, 'matching_installed_app_pids': []},
            'steps': [{'command': ['xcrun', 'simctl', 'terminate', lifecycle.DEVICE, lifecycle.BUNDLE],
                       'monitor_exit_code': 0, 'measurement': {
                           'natural_exit_code': 0, 'ended_by_monitor': False, 'violations': [],
                           'owned_group_cleanup': {'sent_sigterm': False, 'sent_sigkill': False}}}]}


class PlatformLifecycleEvidenceTests(unittest.TestCase):
    def test_safe_platform_recovery_does_not_convert_original_failure_to_pass(self):
        value = recovery()
        unchanged = copy.deepcopy(value)
        lifecycle.validate_recovery(value)
        self.assertEqual(value, unchanged)
        self.assertFalse(value['original_test_cleanup_passed'])
        for key in ('original_test_cleanup_passed', 'performance_release_gate_passed'):
            changed = recovery()
            changed[key] = True
            with self.subTest(key=key), self.assertRaises(ValueError):
                lifecycle.validate_recovery(changed)

    def test_other_device_or_bundle_cannot_supply_recovery_proof(self):
        for field in ('device', 'bundle_id'):
            changed = recovery()
            changed[field] = 'other'
            with self.subTest(field=field), self.assertRaises(ValueError):
                lifecycle.validate_recovery(changed)

    def test_later_or_replacement_app_prevents_accepting_recovery(self):
        for after in ({'identity': {'pid': 1}, 'matching_installed_app_pids': []},
                      {'identity': None, 'matching_installed_app_pids': [1]}, {}):
            changed = recovery()
            changed['after'] = after
            with self.subTest(after=after), self.assertRaises(ValueError):
                lifecycle.validate_recovery(changed)

    def test_timeout_signal_or_collector_failure_never_qualifies_recovery(self):
        for update in ({'natural_exit_code': 2}, {'ended_by_monitor': True},
                       {'collector_error': 'failed'}, {'violations': ['overshoot']},
                       {'owned_group_cleanup': {'sent_sigterm': True, 'sent_sigkill': False}}):
            changed = recovery()
            changed['steps'][0]['measurement'].update(update)
            with self.subTest(update=update), self.assertRaises(ValueError):
                lifecycle.validate_recovery(changed)

    def test_broad_or_wrong_termination_command_cannot_qualify(self):
        for command in (['killall', 'Lumen'], ['xcrun', 'simctl', 'terminate', 'booted', lifecycle.BUNDLE]):
            changed = recovery()
            changed['steps'][0]['command'] = command
            with self.subTest(command=command), self.assertRaises(ValueError):
                lifecycle.validate_recovery(changed)

    def test_file_reader_rejects_symlink_and_excess_size(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            ordinary = root / 'receipt.json'
            ordinary.write_bytes(b'{}')
            linked = root / 'linked.json'
            linked.symlink_to(ordinary)
            with mock.patch.object(lifecycle, 'PRIOR', root):
                self.assertEqual(lifecycle.read_evidence(ordinary)[0], b'{}')
                with self.assertRaises(ValueError):
                    lifecycle.read_evidence(linked)
                with ordinary.open('wb') as target:
                    target.truncate(4 * 1024 * 1024 + 1)
                with self.assertRaises(ValueError):
                    lifecycle.read_evidence(ordinary)


if __name__ == '__main__':
    unittest.main()
