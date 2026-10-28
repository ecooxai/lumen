import importlib.util
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location('cleanup_runner', Path(__file__).with_name('full_runner.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class ChromiumGracefulCleanupTests(unittest.TestCase):
    def browser(self, directory, events, request_error=None):
        browser = object.__new__(runner.Chromium)
        browser.directory = Path(directory)
        browser.profile = browser.directory / 'owned-profile'
        browser.profile.mkdir()
        browser.log = SimpleNamespace(close=lambda: events.append('log.close'))
        browser.process = SimpleNamespace(pid=123, poll=lambda: 0)

        def request(method, timeout):
            self.assertEqual(method, 'Browser.close')
            self.assertEqual(timeout, 5)
            events.append('Browser.close')
            if request_error:
                raise request_error
            return {}

        browser.browser = SimpleNamespace(call=request, close=lambda: events.append('DevTools.close'))
        browser.sampler = SimpleNamespace(
            mod=SimpleNamespace(owned_group_members=lambda pid: {}),
            stop=lambda: (events.append('sampler.stop') or {'telemetry_status': 'captured'}))
        return browser

    def test_graceful_shutdown_precedes_signal_fallback_and_verifies_exit(self):
        events = []
        with tempfile.TemporaryDirectory() as directory:
            browser = self.browser(directory, events)

            def cleanup(process, sampler, verification_wait_s):
                self.assertEqual(verification_wait_s, 10)
                events.append('verified fallback')
                return {'error': None, 'process_group_gone': True, 'sent_sigterm': False, 'sent_sigkill': False}

            with mock.patch.object(runner, 'terminate_owned', side_effect=cleanup):
                result = browser.close()
            self.assertEqual(events[:4], ['Browser.close', 'DevTools.close', 'sampler.stop', 'verified fallback'])
            self.assertTrue(result['graceful_close']['acknowledged'])
            self.assertTrue(result['graceful_close']['process_group_gone'])
            self.assertFalse(browser.profile.exists())
            self.assertEqual(result['errors'], [])

    def test_socket_close_before_reply_is_recorded_and_exit_is_still_verified(self):
        with tempfile.TemporaryDirectory() as directory:
            browser = self.browser(directory, [], EOFError('socket closed on normal browser exit'))
            with mock.patch.object(runner, 'terminate_owned', return_value={'error': None, 'process_group_gone': True}):
                result = browser.close()
            self.assertIn('EOFError', result['graceful_close']['request_error'])
            self.assertFalse(result['graceful_close']['acknowledged'])
            self.assertTrue(result['process']['process_group_gone'])
            self.assertEqual(result['errors'], [])

    def test_verified_cleanup_failure_is_not_weakened_by_graceful_request(self):
        with tempfile.TemporaryDirectory() as directory:
            browser = self.browser(directory, [])
            error = {'error': 'PermissionError: remaining helper', 'process_group_gone': False}
            with mock.patch.object(runner, 'terminate_owned', return_value=error):
                result = browser.close()
            self.assertEqual(result['process']['error'], error['error'])
            self.assertTrue(browser.profile.exists())
            self.assertEqual(result['process']['retained_profile'], str(browser.profile))

    def test_incomplete_startup_without_devtools_uses_owned_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            browser = self.browser(directory, [])
            del browser.browser
            with mock.patch.object(runner, 'terminate_owned', return_value={'error': None, 'process_group_gone': True}) as cleanup:
                result = browser.close()
            cleanup.assert_called_once_with(browser.process, browser.sampler, verification_wait_s=10)
            self.assertFalse(result['graceful_close']['attempted'])

    def test_acknowledged_close_waits_for_actual_delayed_group_exit(self):
        clock = {'now': 0.0}
        with tempfile.TemporaryDirectory() as directory:
            browser = self.browser(directory, [])
            browser.sampler.mod.owned_group_members = lambda _pid: {456: 'verified helper start'} if clock['now'] < 9 else {}
            with mock.patch.object(runner.time, 'monotonic', side_effect=lambda: clock['now']), \
                    mock.patch.object(runner.time, 'sleep', side_effect=lambda delay: clock.update(now=clock['now'] + delay)), \
                    mock.patch.object(runner, 'process_group_diagnostics', return_value={'members': [{'pid': 456, 'stat': '?E'}]}), \
                    mock.patch.object(runner, 'terminate_owned', return_value={'error': None, 'process_group_gone': True, 'sent_sigterm': False, 'sent_sigkill': False}):
                result = browser.close()
            self.assertTrue(result['graceful_close']['process_group_gone'])
            self.assertGreater(result['graceful_close']['drain_duration_ms'], 5000)
            self.assertLessEqual(result['graceful_close']['drain_duration_ms'], 20000)
            self.assertFalse(result['process']['sent_sigterm'])
            self.assertFalse(browser.profile.exists())

    def test_empty_group_does_not_hide_watchdog_or_external_crash_exit(self):
        for code in (2, -11):
            with self.subTest(code=code), tempfile.TemporaryDirectory() as directory:
                browser = self.browser(directory, [])
                evidence = {'error': None, 'process_group_gone': True, 'root_exit_code': code,
                            'sent_sigterm': False, 'sent_sigkill': False}
                with mock.patch.object(runner, 'terminate_owned', return_value=evidence):
                    result = browser.close()
                self.assertTrue(result['process']['process_group_gone'])
                self.assertEqual(result['process']['root_exit_code'], code)
                self.assertIn(str(code), result['process']['teardown_error'])
                self.assertIn(result['process']['teardown_error'], result['errors'])
                self.assertTrue(browser.profile.is_dir())
                self.assertEqual(result['process']['retained_profile'], str(browser.profile))

    def test_only_corresponding_recorded_runner_signal_explains_signal_exit(self):
        for code, term, kill, expected in ((-15, True, False, True),
                                           (-9, False, True, True),
                                           (-9, True, False, False),
                                           (2, True, False, False)):
            with self.subTest(code=code, term=term, kill=kill), tempfile.TemporaryDirectory() as directory:
                browser = self.browser(directory, [])
                browser.process.poll = lambda: None  # still live before the runner signals its group
                evidence = {'error': None, 'process_group_gone': True, 'root_exit_code': code,
                            'sent_sigterm': term, 'sent_sigkill': kill}
                with mock.patch.object(runner, 'terminate_owned', return_value=evidence):
                    result = browser.close()
                self.assertEqual(not result['errors'], expected)
                self.assertEqual(browser.profile.exists(), not expected)
                self.assertEqual(result['process']['sent_sigterm'], term)
                self.assertEqual(result['process']['sent_sigkill'], kill)

    def test_prior_root_signal_exit_is_not_excused_by_later_helper_signal(self):
        for code, term, kill in ((-15, True, False), (-9, False, True)):
            with self.subTest(code=code), tempfile.TemporaryDirectory() as directory:
                browser = self.browser(directory, [])
                browser.process.poll = lambda: code
                evidence = {'error': None, 'process_group_gone': True, 'root_exit_code': code,
                            'sent_sigterm': term, 'sent_sigkill': kill}
                with mock.patch.object(runner, 'terminate_owned', return_value=evidence):
                    result = browser.close()
                self.assertTrue(result['process']['root_exit_observation_verified'])
                self.assertEqual(result['process']['root_exit_before_signal_cleanup'], code)
                self.assertIn(str(code), result['process']['teardown_error'])
                self.assertTrue(result['errors'])
                self.assertTrue(browser.profile.is_dir())

    def test_unexpected_exit_keeps_independent_permission_error(self):
        with tempfile.TemporaryDirectory() as directory:
            browser = self.browser(directory, [])
            evidence = {'error': 'PermissionError: prior signal denied', 'process_group_gone': True,
                        'root_exit_code': 2, 'sent_sigterm': False, 'sent_sigkill': False}
            with mock.patch.object(runner, 'terminate_owned', return_value=evidence):
                result = browser.close()
            self.assertEqual(result['process']['error'], 'PermissionError: prior signal denied')
            self.assertEqual(result['process']['teardown_error'], 'Unexpected Chromium exit status 2')
            self.assertTrue(browser.profile.is_dir())


class PostSignalVerificationTests(unittest.TestCase):
    def verify(self, until, wait, initial_error=None):
        clock = {'now': 0.0}
        process = SimpleNamespace(pid=123, poll=lambda: 0)
        module = SimpleNamespace(
            cleanup_owned_group=lambda proc, identities: {'sent_sigterm': True, 'sent_sigkill': True, 'error': initial_error},
            owned_group_members=lambda _pid: {456: 'verified helper start'} if clock['now'] < until else {})
        sampler = SimpleNamespace(mod=module, observed_identities={456: 'verified helper start'})
        with mock.patch.object(runner.time, 'monotonic', side_effect=lambda: clock['now']), \
                mock.patch.object(runner.time, 'sleep', side_effect=lambda delay: clock.update(now=clock['now'] + delay)), \
                mock.patch.object(runner, 'process_group_diagnostics', return_value={'members': [{'pid': 456, 'stat': '?E'}]}):
            return runner.terminate_owned(process, sampler, verification_wait_s=wait)

    def test_delayed_post_signal_exit_is_verified_without_another_signal(self):
        result = self.verify(until=3, wait=10)
        self.assertTrue(result['process_group_gone'])
        self.assertEqual(result['remaining_group_members'], {})
        self.assertIsNone(result['error'])
        self.assertGreater(result['post_signal_verification']['duration_ms'], 1000)
        self.assertLess(result['post_signal_verification']['duration_ms'], 10000)

    def test_exiting_flag_does_not_hide_a_member_still_alive_at_deadline(self):
        result = self.verify(until=30, wait=2)
        self.assertFalse(result['process_group_gone'])
        self.assertIn('remains', result['error'])
        self.assertEqual(result['remaining_group_members'], {456: 'verified helper start'})
        self.assertGreaterEqual(result['post_signal_verification']['duration_ms'], 2000)
        self.assertLess(result['post_signal_verification']['duration_ms'], 2200)

    def test_permission_error_is_retained_even_if_group_later_disappears(self):
        result = self.verify(until=0, wait=10, initial_error='PermissionError: observed signal denial')
        self.assertTrue(result['process_group_gone'])
        self.assertEqual(result['error'], 'PermissionError: observed signal denial')

    def test_verification_exception_preserves_recorded_signal_and_permission_evidence(self):
        for original_error in (None, 'PermissionError: observed signal denial'):
            with self.subTest(original_error=original_error):
                process = SimpleNamespace(pid=123, poll=lambda: 0)
                original = {'sent_sigterm': True, 'sent_sigkill': False, 'error': original_error}
                module = SimpleNamespace(cleanup_owned_group=lambda *args: dict(original),
                                         owned_group_members=mock.Mock(side_effect=RuntimeError('ps verification failed')))
                sampler = SimpleNamespace(mod=module, observed_identities={123: 'verified root start'})
                result = runner.terminate_owned(process, sampler, verification_wait_s=10)
                self.assertTrue(result['sent_sigterm'])
                self.assertFalse(result['sent_sigkill'])
                self.assertEqual(result['verification_error'], 'RuntimeError: ps verification failed')
                self.assertEqual(result['error'], original_error or result['verification_error'])
                self.assertFalse(result['process_group_gone'])
                self.assertEqual(result['verification_status'], 'failed')


if __name__ == '__main__':
    unittest.main()
