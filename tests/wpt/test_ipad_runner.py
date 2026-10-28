"""Focused isolated iPad CLI checks; execute only on the authorized Mac."""
from contextlib import ExitStack
import importlib.util
import json
from pathlib import Path
import plistlib
import shutil
import tempfile
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location('isolated_ipad_runner', Path(__file__).with_name('ipad_runner.py'))
entry = importlib.util.module_from_spec(spec)
spec.loader.exec_module(entry)


def result(status='OK', attached=True, **changes):
    row = {'status': status, 'reporter_state': {'status': 'ATTACHED'} if attached else None,
           'cleanup': {'stopped': True}, 'metrics': {'telemetry_status': 'partial', 'errors': []}}
    row.update(changes)
    return row


def metadata():
    common = {key: 'same-' + key for key in entry.COMMON_SIGNATURE_FIELDS}
    left = {'signature': dict(common, engine='lumen', execution_platform=entry.PLATFORM,
                              core_runner_sha256='shared-core', runner_sha256='ipad-entry')}
    right = {'signature': dict(common, engine='chromium', runner_sha256='shared-core'),
             'platform': 'macOS-26.6.2-arm64-arm-64bit'}
    return left, right


def make_bundle(app):
    app.mkdir(parents=True)
    (app / '_CodeSignature').mkdir()
    (app / 'Info.plist').write_bytes(plistlib.dumps({'CFBundleIdentifier': entry.AUTHORIZED_BUNDLE,
                                                  'CFBundleExecutable': 'Lumen',
                                                  'CFBundleSupportedPlatforms': ['iPhoneSimulator']}))
    (app / 'Lumen').write_bytes(b'executable unit fixture')
    (app / '_CodeSignature/CodeResources').write_bytes(b'resource envelope unit fixture')
    (app / 'icudtl.dat').write_bytes(b'ICU resource unit fixture')


class IsolatedIpadRunnerTests(unittest.TestCase):
    def test_exact_authorized_ipad_and_bundle_are_required(self):
        entry.allowed_target(entry.AUTHORIZED_DEVICE.lower(), entry.AUTHORIZED_BUNDLE)
        for device, bundle in [('booted', entry.AUTHORIZED_BUNDLE),
                               ('00000000-0000-0000-0000-000000000000', entry.AUTHORIZED_BUNDLE),
                               (entry.AUTHORIZED_DEVICE, 'com.example.other')]:
            with self.subTest(device=device, bundle=bundle), self.assertRaises(ValueError):
                entry.allowed_target(device, bundle)

    def test_raw_simulator_binary_is_not_a_signed_app_substitute(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            (repo / 'build-sim').mkdir()
            (repo / 'build-sim/lumen').write_bytes(b'raw executable')
            with self.assertRaises(FileNotFoundError):
                entry.signed_binary(repo)
            app = repo / 'build-sim/Lumen.app'
            app.mkdir()
            (app / 'Lumen').write_bytes(b'signed executable fixture')
            self.assertEqual(entry.signed_binary(repo), (app / 'Lumen').resolve())

    def test_short_telemetry_never_establishes_a_failure_or_performance_pass(self):
        self.assertEqual(entry.unsafe_result(result(metrics={'telemetry_status': 'unavailable', 'errors': []}), True), [])
        self.assertIn('resource collector failed', entry.unsafe_result(result(metrics={'telemetry_status': 'failed', 'errors': ['ps failed']}), True))

    def test_reportless_browser_failure_can_continue_only_in_explicit_broad_mode(self):
        for status in ('TIMEOUT', 'CRASH', 'ERROR'):
            with self.subTest(status=status):
                self.assertEqual(entry.unsafe_result(result(status, attached=False), False), [])
                self.assertIn('synchronous reporter did not attach', entry.unsafe_result(result(status, attached=False), True))
        self.assertIn('synchronous reporter did not attach', entry.unsafe_result(result('OK', attached=False), False))

    def test_cleanup_failure_blocks_next_case_without_changing_assertion_status(self):
        row = result(cleanup={'stopped': False, 'error': 'owned app did not exit'})
        self.assertIn('owned iPad app or profile cleanup failed', entry.unsafe_result(row, False))
        self.assertEqual(row['status'], 'OK')

    def test_modified_execution_inputs_are_detected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'fixture.json'
            source.write_text('first fixture')
            guards = entry.input_guards([source])
            entry.check_guards(guards)
            source.write_text('different fixture content')
            with self.assertRaisesRegex(ValueError, 'input changed'):
                entry.check_guards(guards)

    def test_bundle_provenance_verifies_both_signatures_and_binds_all_contents(self):
        with tempfile.TemporaryDirectory() as directory:
            source, installed = Path(directory) / 'source.app', Path(directory) / 'installed.app'
            make_bundle(source)
            shutil.copytree(source, installed)
            with mock.patch.object(entry.subprocess, 'run', return_value=mock.Mock(returncode=0, stderr='')) as command:
                proof, guards = entry.verify_signed_bundles(source, installed)
            self.assertEqual([call.args[0] for call in command.call_args_list],
                             [['codesign', '--verify', '--strict', '--deep', str(app.resolve())]
                              for app in (source, installed)])
            self.assertTrue(proof['source_verified'] and proof['installed_verified'])
            self.assertEqual(proof['critical_files_sha256']['source'], proof['critical_files_sha256']['installed'])
            self.assertIn('_CodeSignature/CodeResources', proof['critical_files_sha256']['source'])
            self.assertIn('icudtl.dat', proof['critical_files_sha256']['source'])
            entry.check_bundle_guards(guards)

    def test_matching_executable_does_not_hide_a_different_bundle_resource(self):
        with tempfile.TemporaryDirectory() as directory:
            source, installed = Path(directory) / 'source.app', Path(directory) / 'installed.app'
            make_bundle(source)
            shutil.copytree(source, installed)
            (installed / 'icudtl.dat').write_bytes(b'changed ICU content')
            with mock.patch.object(entry.subprocess, 'run', return_value=mock.Mock(returncode=0, stderr='')):
                with self.assertRaisesRegex(ValueError, 'contents differ.*icudtl.dat'):
                    entry.verify_signed_bundles(source, installed)

    def test_failed_code_signature_verification_cannot_produce_bundle_proof(self):
        with tempfile.TemporaryDirectory() as directory:
            source, installed = Path(directory) / 'source.app', Path(directory) / 'installed.app'
            make_bundle(source)
            shutil.copytree(source, installed)
            with mock.patch.object(entry.subprocess, 'run', return_value=mock.Mock(returncode=1, stderr='invalid resource seal')):
                with self.assertRaisesRegex(ValueError, 'code-signature verification failed'):
                    entry.verify_signed_bundles(source, installed)

    def test_bundle_guard_detects_new_or_modified_resources(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / 'Lumen.app'
            make_bundle(app)
            guards = {str(app.resolve()): entry.bundle_tree(app)}
            (app / 'new-resource.bin').write_bytes(b'new resource')
            with self.assertRaisesRegex(ValueError, 'bundle changed'):
                entry.check_bundle_guards(guards)
            guards = {str(app.resolve()): entry.bundle_tree(app)}
            (app / 'icudtl.dat').write_bytes(b'changed existing resource')
            with self.assertRaisesRegex(ValueError, 'bundle changed'):
                entry.check_bundle_guards(guards)

    def test_bundle_symlink_must_not_escape_the_guarded_app(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / 'Lumen.app'
            make_bundle(app)
            outside = root / 'unguarded-resource'
            outside.write_bytes(b'outside')
            (app / 'resource-link').symlink_to(outside)
            with self.assertRaisesRegex(ValueError, 'escapes the app'):
                entry.bundle_tree(app)

    def test_mac_results_cannot_be_mislabeled_as_ipad(self):
        left, right = metadata()
        entry.verify_comparison(left, right)
        left['signature']['execution_platform'] = 'macOS'
        with self.assertRaisesRegex(ValueError, 'requires iPad'):
            entry.verify_comparison(left, right)

    def test_cross_platform_comparison_requires_shared_core_and_schedule(self):
        left, right = metadata()
        right['signature']['runner_sha256'] = 'another-core'
        with self.assertRaisesRegex(ValueError, 'same reviewed core'):
            entry.verify_comparison(left, right)
        right['signature']['runner_sha256'] = 'shared-core'
        right['signature']['schedule_sha256'] = 'another-schedule'
        with self.assertRaisesRegex(ValueError, 'identical schedule_sha256'):
            entry.verify_comparison(left, right)

    def test_comparison_rejects_explicit_or_inferred_active_invocations(self):
        left, right = metadata()
        left['invocation_open'] = True
        with self.assertRaisesRegex(ValueError, 'active or unclosed'):
            entry.verify_comparison(left, right)
        left['invocation_open'] = False
        right.update(last_invocation_started_unix_ms=20, last_checkpoint_unix_ms=10)
        with self.assertRaisesRegex(ValueError, 'active or unclosed'):
            entry.verify_comparison(left, right)
        right['last_checkpoint_unix_ms'] = 30
        entry.verify_comparison(left, right)

    def test_comparison_uses_complete_result_rows_and_preserves_cleanup_stop(self):
        state = entry.comparison_run_state({'invocation_open': False, 'finished_schedule': True,
                                            'completed_result_rows': 999, 'stopped_for_cleanup_failure': True},
                                           {'recorded_files': 4, 'scheduled_files': 6})
        self.assertFalse(state['invocation_open'])
        self.assertEqual(state['completed_result_rows'], 4)
        self.assertFalse(state['all_scheduled_rows_recorded'])
        self.assertTrue(state['finished_schedule'])
        self.assertEqual(state['stops'], {'stopped_for_cleanup_failure': True})

    def test_abrupt_previous_invocation_cannot_launch_again_without_cleanup_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            entry.core.write_json(output / 'run.json', {'signature': {'identity': 1}, 'invocation_open': True})
            with self.assertRaisesRegex(ValueError, 'final cleanup'):
                entry.ensure_fresh_or_matching(output, {'identity': 1}, resume=True)

    def test_abnormal_exit_during_adapter_call_keeps_cleanup_unverified(self):
        schedule = [{'type': 'testharness', 'test': '/url/a.html'}]
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory).resolve()
            inventory = repo / 'inventory.json'
            inventory.write_text(json.dumps({'wpt_revision': 'fixture-revision', 'manifest_sha256': 'fixture-manifest',
                                              'test_variants_by_type': {'testharness': 1}, 'tests': schedule}))
            binary = repo / 'build-sim/Lumen.app/Lumen'
            adapter = mock.Mock(binary=binary, app=binary.parent, metadata={})
            adapter.run.side_effect = SystemExit('abnormal test exit')
            args = entry.argparse.Namespace(repo=repo, inventory=inventory, server_config=repo / 'server.json',
                                            fixture_manifest=repo / 'fixture.json', ca_file=repo / 'ca.pem',
                                            output=repo / 'result', device=entry.AUTHORIZED_DEVICE,
                                            bundle_id=entry.AUTHORIZED_BUNDLE, host_map='web-platform.test=127.0.0.1',
                                            timeout=15, long_timeout=65, max_seconds=None, types='testharness',
                                            prefix=['/url/'], offset=0, limit=1, allow_reportless_failures=False,
                                            resume=False, screenshot_first=False, origin='http://web-platform.test:8000',
                                            secure_origin='https://web-platform.test:18443', h2_origin='https://web-platform.test:9000')
            with ExitStack() as patches:
                patches.enter_context(mock.patch.dict(entry.os.environ, {}, clear=False))
                patches.enter_context(mock.patch.object(entry.platform, 'system', return_value='Darwin'))
                patches.enter_context(mock.patch.object(entry.signal, 'signal', return_value=None))
                for name, value in {'validate_fixture': {}, 'server_alive': None, 'signed_binary': binary,
                                    'input_guards': {}, 'check_guards': None, 'identity_set': {'fixture': 'identity'},
                                    'verify_signed_bundles': ({'verification': 'unit fixture'}, {}), 'IpadAdapter': adapter}.items():
                    patches.enter_context(mock.patch.object(entry, name, return_value=value))
                for name, value in {'select_schedule': (schedule, {'fixture': 'selection'}),
                                    'source_fixture_signature': {}, 'verify_reporter_route': 'reporter-digest',
                                    'native_fixture_signature': {}, 'file_digest': 'fixture-digest'}.items():
                    patches.enter_context(mock.patch.object(entry.core, name, return_value=value))
                with self.assertRaises(SystemExit):
                    entry.run_exclusive(args)
            meta = json.loads((args.output / 'run.json').read_text())
            self.assertTrue(meta['invocation_open'])
            self.assertIn('cleanup evidence', meta['cleanup_unverified'])
            self.assertEqual(meta['completed_result_rows'], 0)
            self.assertFalse(meta['finished_schedule'])

    def test_unsafe_cohort_is_preserved_but_clean_time_budget_can_resume(self):
        schedule = [{'type': 'testharness', 'test': '/url/a.html'}]
        signature = {'schedule_sha256': entry.core.digest(schedule)}
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            entry.core.write_json(output / 'schedule.json', schedule)
            meta = {'signature': signature, 'invocation_open': False, 'stopped_for_time_budget': True}
            entry.core.write_json(output / 'run.json', meta)
            self.assertEqual(entry.ensure_fresh_or_matching(output, signature, resume=True), meta)
            meta['stopped_for_unsafe_result'] = {'reasons': ['cleanup failed']}
            entry.core.write_json(output / 'run.json', meta)
            with self.assertRaisesRegex(ValueError, 'Unsafe prior'):
                entry.ensure_fresh_or_matching(output, signature, resume=True)


if __name__ == '__main__':
    unittest.main()
