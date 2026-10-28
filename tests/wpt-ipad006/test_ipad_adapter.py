import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('ipad_adapter', Path(__file__).with_name('ipad_adapter.py'))
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)


class IpadValidationTests(unittest.TestCase):
    def listing(self, **changes):
        device = {'udid': adapter.AUTHORIZED_DEVICE, 'name': 'iPad Pro',
                  'deviceTypeIdentifier': 'com.apple.CoreSimulator.SimDeviceType.iPad-Pro',
                  'state': 'Booted', 'isAvailable': True}
        device.update(changes)
        return {'devices': {'com.apple.CoreSimulator.SimRuntime.iOS-26-5': [device]}}

    def test_only_explicit_booted_ipad_is_accepted(self):
        result = adapter.select_device(self.listing(), adapter.AUTHORIZED_DEVICE)
        self.assertEqual(result['name'], 'iPad Pro')
        for listing, target in [(self.listing(), 'other-uuid'),
                                (self.listing(state='Shutdown'), adapter.AUTHORIZED_DEVICE),
                                (self.listing(isAvailable=False), adapter.AUTHORIZED_DEVICE),
                                (self.listing(deviceTypeIdentifier='com.apple.CoreSimulator.SimDeviceType.iPhone-17'), adapter.AUTHORIZED_DEVICE)]:
            with self.assertRaises(ValueError):
                adapter.select_device(listing, target)

    def test_launch_pid_is_bound_to_exact_bundle_and_not_ambiguous(self):
        self.assertEqual(adapter.launch_pid('com.ecooxai.lumen: 123\n', 'com.ecooxai.lumen'), 123)
        for invalid in ('other.bundle: 123\n', 'com.ecooxai.lumen: 0\n',
                        'com.ecooxai.lumen: 123\ncom.ecooxai.lumen: 124\n'):
            with self.assertRaises(ValueError):
                adapter.launch_pid(invalid, 'com.ecooxai.lumen')

    def test_constructor_rejects_other_app_before_any_simulator_call(self):
        with mock.patch.object(adapter.platform, 'system', return_value='Darwin'), mock.patch.object(adapter.IpadAdapter, '_checked') as checked:
            with self.assertRaises(ValueError):
                adapter.IpadAdapter(adapter.AUTHORIZED_DEVICE, 'com.apple.mobilesafari')
        checked.assert_not_called()

    def test_launch_never_implicitly_terminates_an_existing_app_or_selects_booted(self):
        args = adapter.launch_arguments(adapter.AUTHORIZED_DEVICE, 'com.ecooxai.lumen', 'out.log', 'err.log', 'https://web-platform.test:18443/a.html?x=1')
        self.assertNotIn('--terminate-running-process', args)
        self.assertNotIn('booted', args)
        self.assertEqual(args[-3], adapter.AUTHORIZED_DEVICE)
        self.assertEqual(args[-1], 'https://web-platform.test:18443/a.html?x=1')

    def test_only_explicit_test_environment_is_forwarded(self):
        base = {'PATH': 'host-path', 'SIMCTL_CHILD_LUMEN_NO_JS': '1',
                'SIMCTL_CHILD_LUMEN_SHOT': 'old.png', 'SIMCTL_CHILD_SECRET': 'private',
                'SIMCTL_CHILD_LUMEN_PRE_FILE': 'old-reporter.js', 'LUMEN_PRE_FILE': 'old-reporter.js',
                'LUMEN_HOST_MAP': 'web-platform.test=127.0.0.1',
                'LUMEN_HOST_MAP_MODE': 'exact', 'SSL_CERT_FILE': 'host-ca.pem'}
        result = adapter.launch_environment(base, Path('owned/profile'), Path('owned/ca.pem'))
        self.assertNotIn('SIMCTL_CHILD_SECRET', result)
        self.assertNotIn('SIMCTL_CHILD_LUMEN_NO_JS', result)
        self.assertNotIn('SIMCTL_CHILD_LUMEN_SHOT', result)
        self.assertNotIn('SIMCTL_CHILD_LUMEN_PRE_FILE', result)
        self.assertNotIn('LUMEN_PRE_FILE', result)
        self.assertEqual(result['SIMCTL_CHILD_SSL_CERT_FILE'], 'owned/ca.pem')
        self.assertEqual(result['SIMCTL_CHILD_LUMEN_HOST_MAP'], base['LUMEN_HOST_MAP'])
        self.assertEqual(result['SIMCTL_CHILD_LUMEN_HOST_MAP_MODE'], 'exact')
        self.assertEqual(result['SIMCTL_CHILD_LUMEN_PROFILE_DIR'], 'owned/profile')

    def test_process_identity_preserves_paths_containing_spaces(self):
        result = adapter.parse_process_identity('123 Thu Oct 22 09:00:01 2026 /sim/Lumen Test.app/Lumen\n', 123)
        self.assertEqual(result['start'], 'Thu Oct 22 09:00:01 2026')
        self.assertEqual(result['executable'], '/sim/Lumen Test.app/Lumen')
        self.assertIsNone(adapter.parse_process_identity('999 Thu Oct 22 09:00:01 2026 /sim/Lumen\n', 123))

    def instance(self):
        value = object.__new__(adapter.IpadAdapter)
        value.device = adapter.AUTHORIZED_DEVICE
        value.bundle_id = 'com.ecooxai.lumen'
        value.app = Path('/sim')
        value.binary = value.app / 'Lumen'
        value._running_instances = mock.Mock(return_value=[])
        value._checked = mock.Mock(return_value='')
        return value

    def test_cleanup_never_terminates_reused_pid(self):
        instance = self.instance()
        original = {'pid': 123, 'start': 'old', 'executable': '/sim/Lumen'}
        replacement = dict(original, start='new')
        with mock.patch.object(adapter, 'process_identity', return_value=replacement), mock.patch.object(adapter.os, 'kill') as kill:
            result = instance._stop_owned_app(original, Path('case'))
        self.assertTrue(result['stopped'])
        kill.assert_not_called()
        instance._checked.assert_not_called()

    def test_platform_cleanup_requires_same_registration_and_sole_owned_process(self):
        instance = self.instance()
        original = {'pid': 123, 'start': 'same', 'executable': '/sim/Lumen'}
        instance._running_instances.side_effect = [[123], [123], []]
        instance._checked.side_effect = [json.dumps(self.listing()), '/sim']
        instance._platform_terminate = mock.Mock(return_value={'request_passed': True})
        with mock.patch.object(adapter, 'process_identity', side_effect=[original, original, None]), mock.patch.object(adapter.os, 'kill') as kill:
            result = instance._stop_owned_app(original, Path('case'))
        self.assertTrue(result['stopped'])
        self.assertTrue(result['platform_termination_requested'])
        self.assertFalse(result['app_graceful_shutdown_verified'])
        self.assertIsNone(result['natural_app_exit_code'])
        self.assertEqual(instance._checked.call_args_list, [
            mock.call(['list', 'devices', '--json'], timeout=5),
            mock.call(['get_app_container', adapter.AUTHORIZED_DEVICE, adapter.AUTHORIZED_BUNDLE, 'app'], timeout=5)])
        instance._platform_terminate.assert_called_once_with(Path('case'))
        kill.assert_not_called()

    def test_failed_platform_request_is_not_excused_when_app_happens_to_exit(self):
        instance = self.instance()
        original = {'pid': 123, 'start': 'same', 'executable': '/sim/Lumen'}
        instance._running_instances.side_effect = [[123], [123], []]
        instance._checked.side_effect = [json.dumps(self.listing()), '/sim']
        instance._platform_terminate = mock.Mock(return_value={'request_passed': False, 'errors': ['denied']})
        with mock.patch.object(adapter, 'process_identity', side_effect=[original, original, None]), mock.patch.object(adapter.os, 'kill') as kill:
            result = instance._stop_owned_app(original, Path('case'))
        self.assertFalse(result['stopped'])
        self.assertTrue(result['app_gone'])
        self.assertEqual(result['request']['errors'], ['denied'])
        kill.assert_not_called()

    def test_binary_mismatch_is_detected_before_launch(self):
        instance = self.instance()
        digest = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()
        instance.runtime = SimpleNamespace(file_digest=digest)
        with tempfile.TemporaryDirectory() as directory:
            instance.binary = Path(directory) / 'installed'
            supplied = Path(directory) / 'requested'
            instance.binary.write_bytes(b'old-build')
            supplied.write_bytes(b'new-build')
            instance.metadata = {'installed_binary_sha256': digest(instance.binary)}
            with self.assertRaises(ValueError):
                instance._verify_binary(supplied)
        instance._checked.assert_not_called()

    def test_profile_cleanup_cannot_delete_unrelated_directory(self):
        instance = self.instance()
        with tempfile.TemporaryDirectory() as directory:
            instance.temp_root = Path(directory)
            unrelated = instance.temp_root / 'user-profile'
            unrelated.mkdir()
            with self.assertRaises(ValueError):
                instance._remove_owned_profile(unrelated)
            self.assertTrue(unrelated.exists())

    def test_unverified_launch_retains_profile_and_never_signals_app(self):
        instance = self.instance()
        instance.capture_next = False
        instance.runtime = SimpleNamespace(Collector=lambda: SimpleNamespace(
            result=lambda fallback: dict(fallback, subtests=[])))
        instance._verify_binary = mock.Mock()
        instance._running_instances = mock.Mock(side_effect=[[], [123]])
        instance._checked = mock.Mock(return_value='unparseable launch result')
        instance._stop_owned_app = mock.Mock()
        instance._remove_owned_profile = mock.Mock()
        with tempfile.TemporaryDirectory() as directory:
            instance.temp_root = Path(directory) / 'app-tmp'
            instance.temp_root.mkdir()
            with mock.patch.dict(adapter.os.environ, {}, clear=True):
                result = instance.run(Path(directory) / 'Lumen', 'http://web-platform.test:8000/a.html', 15, Path(directory) / 'output' / 'case')
            self.assertEqual(result['status'], 'INFRA_ERROR')
            self.assertFalse(result['cleanup']['stopped'])
            self.assertEqual(result['cleanup']['unverified_app_pids'], [123])
            self.assertTrue(Path(result['cleanup']['retained_profile']).is_dir())
        instance._stop_owned_app.assert_not_called()
        instance._remove_owned_profile.assert_not_called()

    def test_prefile_reporter_cannot_be_reintroduced_into_synchronous_run(self):
        instance = self.instance()
        with self.assertRaises(ValueError):
            instance.run(Path('Lumen'), 'http://web-platform.test:8000/a.html', 15, Path('out/case'), Path('old-reporter.js'))
        instance._checked.assert_not_called()

    def test_existing_app_is_not_reused_or_terminated(self):
        instance = self.instance()
        instance._verify_binary = mock.Mock()
        instance._running_instances = mock.Mock(return_value=[123])
        instance._stop_owned_app = mock.Mock()
        with self.assertRaises(RuntimeError):
            instance.run(Path('Lumen'), 'http://web-platform.test:8000/a.html', 15, Path('out/case'))
        instance._checked.assert_not_called()
        instance._stop_owned_app.assert_not_called()


class PlatformTerminationTests(unittest.TestCase):
    def instance(self):
        value = object.__new__(adapter.IpadAdapter)
        value.device, value.bundle_id = adapter.AUTHORIZED_DEVICE, adapter.AUTHORIZED_BUNDLE
        value.app, value.binary = Path('/sim'), Path('/sim/Lumen')
        value._running_instances = mock.Mock(return_value=[123])
        value._checked = mock.Mock(side_effect=[json.dumps(IpadValidationTests().listing()), '/sim'])
        value._platform_terminate = mock.Mock(return_value={'request_passed': True})
        return value

    def original(self):
        return {'pid': 123, 'start': 'same', 'executable': '/sim/Lumen'}

    def test_replacement_app_blocks_termination_even_when_original_has_exited(self):
        value = self.instance()
        value._running_instances.return_value = [456]
        with mock.patch.object(adapter, 'process_identity', return_value=None):
            result = value._stop_owned_app(self.original(), Path('case'))
        self.assertFalse(result['stopped'])
        self.assertFalse(result['platform_termination_requested'])
        value._checked.assert_not_called()
        value._platform_terminate.assert_not_called()

    def test_additional_same_app_process_blocks_bundle_request(self):
        value = self.instance()
        value._running_instances.return_value = [123, 456]
        with mock.patch.object(adapter, 'process_identity', return_value=self.original()):
            result = value._stop_owned_app(self.original(), Path('case'))
        self.assertFalse(result['stopped'])
        value._platform_terminate.assert_not_called()

    def test_changed_app_registration_stops_without_platform_request(self):
        value = self.instance()
        value._checked.side_effect = [json.dumps(IpadValidationTests().listing()), '/other/Lumen.app']
        with mock.patch.object(adapter, 'process_identity', return_value=self.original()):
            result = value._stop_owned_app(self.original(), Path('case'))
        self.assertIn('registration changed', result['error'])
        value._platform_terminate.assert_not_called()

    def test_unavailable_selected_device_stops_without_lifecycle_mutation(self):
        value = self.instance()
        value._checked.side_effect = [json.dumps(IpadValidationTests().listing(state='Shutdown'))]
        with mock.patch.object(adapter, 'process_identity', return_value=self.original()):
            result = value._stop_owned_app(self.original(), Path('case'))
        self.assertFalse(result['stopped'])
        self.assertEqual(value._checked.call_count, 1)
        value._platform_terminate.assert_not_called()

    def test_identity_change_at_final_guard_stops_without_termination(self):
        value = self.instance()
        with mock.patch.object(adapter, 'process_identity', side_effect=[self.original(), None]):
            result = value._stop_owned_app(self.original(), Path('case'))
        self.assertIn('ownership changed', result['error'])
        value._platform_terminate.assert_not_called()

    def test_still_running_app_cannot_be_reported_as_success(self):
        value = self.instance()
        with mock.patch.object(adapter, 'process_identity', return_value=self.original()), \
             mock.patch.object(adapter.time, 'monotonic', side_effect=[0, 11, 11]):
            result = value._stop_owned_app(self.original(), Path('case'))
        self.assertFalse(result['stopped'])
        self.assertFalse(result['app_gone'])
        self.assertIn('remained', result['error'])

    def request(self, process, cleanup=None, metrics=None, sampler_error=None):
        value = self.instance()
        del value._platform_terminate
        cleanup = cleanup or {'process_group_gone': True, 'remaining_group_members': {},
                              'sent_sigterm': False, 'sent_sigkill': False, 'error': None}
        metrics = metrics or {'samples': 1, 'telemetry_status': 'partial', 'errors': []}
        sampler = mock.Mock()
        sampler.stop.return_value = metrics
        sampler.stop.side_effect = sampler_error
        value.runtime = SimpleNamespace(
            monitor_module=mock.Mock(), Sampler=mock.Mock(return_value=sampler),
            terminate_owned=mock.Mock(return_value=cleanup),
            measurement_failure=lambda row: bool(row['metrics'].get('errors')),
            write_json=lambda path, record: path.write_text(json.dumps(record)))
        with tempfile.TemporaryDirectory() as directory, \
             mock.patch.object(adapter.subprocess, 'Popen', return_value=process) as popen:
            prefix = Path(directory) / 'case'
            result = value._platform_terminate(prefix)
            evidence = json.loads(prefix.with_suffix('.terminate.json').read_text())
            self.assertEqual(result, evidence)
        return result, value, popen, sampler

    def test_cli_targets_only_exact_device_and_bundle_and_is_observed(self):
        process = mock.Mock(pid=321)
        process.wait.return_value = 0
        result, value, popen, sampler = self.request(process)
        self.assertTrue(result['request_passed'])
        self.assertEqual(popen.call_args.args[0], ['xcrun', 'simctl', 'terminate',
                                                   adapter.AUTHORIZED_DEVICE, adapter.AUTHORIZED_BUNDLE])
        self.assertTrue(popen.call_args.kwargs['start_new_session'])
        process.wait.assert_called_once_with(timeout=20)
        value.runtime.terminate_owned.assert_called_once_with(process, sampler)
        sampler.stop.assert_called_once_with()

    def test_cli_nonzero_natural_exit_remains_failed(self):
        process = mock.Mock(pid=321)
        process.wait.return_value = 4
        result, _, _, _ = self.request(process)
        self.assertFalse(result['request_passed'])
        self.assertEqual(result['natural_exit_code'], 4)

    def test_cli_timeout_retains_owned_signal_cleanup_and_cannot_pass(self):
        process = mock.Mock(pid=321)
        process.wait.side_effect = adapter.subprocess.TimeoutExpired('simctl', 20)
        result, _, _, _ = self.request(process, cleanup={
            'process_group_gone': True, 'sent_sigterm': True, 'sent_sigkill': False, 'error': None})
        self.assertFalse(result['request_passed'])
        self.assertTrue(result['timed_out'])
        self.assertTrue(result['process_cleanup']['sent_sigterm'])
        self.assertIsNone(result['natural_exit_code'])

    def test_cli_collector_failure_stops_even_after_natural_exit(self):
        process = mock.Mock(pid=321)
        process.wait.return_value = 0
        result, _, _, _ = self.request(process, sampler_error=RuntimeError('collector failed'))
        self.assertFalse(result['request_passed'])
        self.assertIn('collector failed', result['metrics']['errors'][0])

    def test_app_sampling_continues_through_teardown_before_profile_removal(self):
        value = self.instance()
        value.capture_next = False
        value._verify_binary = mock.Mock()
        value._running_instances.return_value = []
        value._checked = mock.Mock(return_value=adapter.AUTHORIZED_BUNDLE + ': 123\n')
        events = []
        collector = SimpleNamespace(done=True, line=lambda line: None,
            result=lambda fallback: {'status': 'OK', 'subtests': [], 'subtest_count': 0})
        sampler = SimpleNamespace(stop=lambda: events.append('stop_sampler') or {
            'samples': 1, 'telemetry_status': 'partial', 'errors': []})
        value.runtime = SimpleNamespace(Collector=lambda: collector, Sampler=lambda pid, path: sampler)
        value._stop_owned_app = mock.Mock(side_effect=lambda identity, prefix:
                                         events.append('platform_teardown') or {'stopped': True})
        value._remove_owned_profile = mock.Mock(side_effect=lambda path: events.append('remove_profile'))
        with tempfile.TemporaryDirectory() as directory:
            value.temp_root = Path(directory) / 'tmp'
            value.temp_root.mkdir()
            with mock.patch.dict(adapter.os.environ, {}, clear=True), \
                 mock.patch.object(adapter, 'process_identity', return_value=self.original()), \
                 mock.patch.object(adapter, 'process_exists', return_value=True):
                result = value.run(Path('/sim/Lumen'), 'http://web-platform.test:8000/test.html',
                                   15, Path(directory) / 'case')
        self.assertEqual(result['status'], 'OK')
        self.assertEqual(events, ['platform_teardown', 'stop_sampler', 'remove_profile'])


if __name__ == '__main__':
    unittest.main()
