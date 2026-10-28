import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('cft_reference_policy', Path(__file__).with_name('run_cft156_url77.py'))
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)


def known_incident():
    return {'finished_schedule': True, 'stopped_for_cleanup_failure': True,
            'chromium_cleanup': {
                'errors': ['Unexpected Chromium exit status 2'],
                'process': {'error': None, 'teardown_error': 'Unexpected Chromium exit status 2',
                            'root_exit_code': 2, 'root_exit_before_signal_cleanup': 2,
                            'root_exit_observation_verified': True, 'sent_sigterm': False, 'sent_sigkill': False,
                            'process_group_gone': True, 'remaining_group_members': {}},
                'graceful_close': {'method': 'Browser.close', 'attempted': True,
                                   'acknowledged': True, 'process_group_gone': True}}}


class FunctionalReferencePolicyTests(unittest.TestCase):
    def test_known_post_report_incident_is_preserved_and_recognized(self):
        value = known_incident()
        saved = copy.deepcopy(value)
        self.assertTrue(policy.known_reference_teardown(value))
        self.assertEqual(value, saved)
        self.assertTrue(value['stopped_for_cleanup_failure'])

    def test_incomplete_reports_and_other_run_stops_are_rejected(self):
        for change in ({'finished_schedule': False}, {'stopped_for_telemetry_failure': ['bad collector']},
                       {'stopped_for_infrastructure_failure': True}, {'stopped_for_binary_change': True}):
            with self.subTest(change=change):
                value = known_incident()
                value.update(change)
                self.assertFalse(policy.known_reference_teardown(value))

    def test_only_the_exact_unsignaled_exit_two_is_eligible(self):
        for field, changed in (('root_exit_code', 1), ('root_exit_code', -9),
                               ('root_exit_before_signal_cleanup', None), ('root_exit_observation_verified', False),
                               ('sent_sigterm', True), ('sent_sigkill', True)):
            with self.subTest(field=field, changed=changed):
                value = known_incident()
                value['chromium_cleanup']['process'][field] = changed
                self.assertFalse(policy.known_reference_teardown(value))

    def test_acknowledged_browser_close_and_empty_group_proof_are_required(self):
        for key in ('attempted', 'acknowledged', 'process_group_gone'):
            with self.subTest(key=key):
                value = known_incident()
                value['chromium_cleanup']['graceful_close'][key] = False
                self.assertFalse(policy.known_reference_teardown(value))
        value = known_incident()
        value['chromium_cleanup']['process']['remaining_group_members'] = {'123': 'start'}
        self.assertFalse(policy.known_reference_teardown(value))

    def test_additional_cleanup_and_nested_verification_errors_are_rejected(self):
        for location, key, error in (('process', 'error', 'PermissionError'),
                                     ('graceful_close', 'verification_error', 'ps failed'),
                                     ('process', 'post_signal_verification', {'error': 'identity unavailable'})):
            with self.subTest(key=key):
                value = known_incident()
                value['chromium_cleanup'][location][key] = error
                self.assertFalse(policy.known_reference_teardown(value))
        value = known_incident()
        value['chromium_cleanup']['errors'].append('Profile cleanup failed')
        self.assertFalse(policy.known_reference_teardown(value))

    def test_missing_observations_never_become_success(self):
        value = known_incident()
        del value['chromium_cleanup']['process']['remaining_group_members']
        self.assertFalse(policy.known_reference_teardown(value))


if __name__ == '__main__':
    unittest.main()
