import hashlib
import importlib.util
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
        value._checked = mock.Mock(return_value='')
        return value

    def test_cleanup_never_terminates_reused_pid(self):
        instance = self.instance()
        original = {'pid': 123, 'start': 'old', 'executable': '/sim/Lumen'}
        replacement = dict(original, start='new')
        with mock.patch.object(adapter, 'process_identity', return_value=replacement), mock.patch.object(adapter.os, 'kill') as kill:
            result = instance._stop_owned_app(original)
        self.assertTrue(result['stopped'])
        kill.assert_not_called()
        instance._checked.assert_not_called()

    def test_cleanup_targets_verified_pid_without_terminating_bundle_replacement(self):
        instance = self.instance()
        original = {'pid': 123, 'start': 'same', 'executable': '/sim/Lumen'}
        with mock.patch.object(adapter, 'process_identity', return_value=original), mock.patch.object(adapter, 'process_exists', return_value=False), mock.patch.object(adapter.os, 'kill') as kill:
            result = instance._stop_owned_app(original)
        self.assertTrue(result['stopped'])
        self.assertEqual(result['signaled_pid'], 123)
        kill.assert_called_once_with(123, adapter.signal.SIGTERM)
        instance._checked.assert_not_called()

    def test_cleanup_permission_failure_remains_visible_without_escalation(self):
        instance = self.instance()
        original = {'pid': 123, 'start': 'same', 'executable': '/sim/Lumen'}
        with mock.patch.object(adapter, 'process_identity', return_value=original), mock.patch.object(adapter.os, 'kill', side_effect=PermissionError('denied')) as kill:
            result = instance._stop_owned_app(original)
        self.assertFalse(result['stopped'])
        self.assertIn('PermissionError', result['error'])
        kill.assert_called_once_with(123, adapter.signal.SIGTERM)
        instance._checked.assert_not_called()

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


if __name__ == '__main__':
    unittest.main()
