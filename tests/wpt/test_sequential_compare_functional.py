"""Run on the authorized Mac after installing beside full_runner.py.

These tests use temporary fixture files and mocked subprocesses; they do not
launch a browser. They are authored here and have not been executed locally.
"""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


def module(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + '.py'))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


controller = module('sequential_compare_functional')
runner = module('full_runner')


def row(test='/url/a.html', **changes):
    value = {'type': 'testharness', 'test': test, 'status': 'OK', 'subtest_count': 1,
             'subtests': [{'name': 'assertion', 'status': 'PASS'}],
             'reporter_state': {'status': 'ATTACHED'}, 'cleanup': {'error': None},
             'metrics': {'telemetry_status': 'partial', 'errors': []}}
    value.update(changes)
    return value


class SnapshotAndPlanningTests(unittest.TestCase):
    def test_snapshot_stays_adjacent_readonly_and_unchanged_after_rebuild(self):
        with tempfile.TemporaryDirectory() as directory:
            original = Path(directory) / 'lumen'
            original.write_bytes(b'first executable fixture')
            original.chmod(0o755)
            record = controller.snapshot_native(original)
            snapshot = Path(record['snapshot']['path'])
            self.assertEqual(snapshot.parent.resolve(), original.parent.resolve())
            self.assertEqual(snapshot.read_bytes(), original.read_bytes())
            self.assertEqual(snapshot.stat().st_mode & 0o222, 0)
            self.assertTrue(snapshot.stat().st_mode & 0o111)
            original.write_bytes(b'new native build')
            controller.assert_files_unchanged({'native': record['snapshot']}, content=True)
            self.assertEqual(snapshot.read_bytes(), b'first executable fixture')

    def test_existing_writable_snapshot_is_not_silently_accepted(self):
        with tempfile.TemporaryDirectory() as directory:
            original = Path(directory) / 'lumen'
            original.write_bytes(b'fixture')
            original.chmod(0o755)
            saved = controller.snapshot_native(original)
            Path(saved['snapshot']['path']).chmod(0o755)
            with self.assertRaisesRegex(ValueError, 'writable'):
                controller.snapshot_native(original)

    def test_shards_cover_exact_per_class_selection_without_smoke_inflation(self):
        rows = ([{'type': 'testharness', 'test': '/url/' + format(i, '02d') + '.html'} for i in range(12)] +
                [{'type': 'test262', 'test': '/infrastructure/test262/' + format(i, '02d') + '.test262.html'} for i in range(9)] +
                [{'type': 'reftest', 'test': '/visual.html'}])
        inventory = {'tests': rows, 'test_variants_by_type': {'testharness': 12, 'test262': 9, 'reftest': 1}}
        units, coverage = controller.plan_units(runner, inventory, ['testharness', 'test262'], [], 3, 5, 2, 6)
        self.assertEqual([unit['name'] for unit in units[:2]], ['smoke-testharness', 'smoke-test262'])
        broad = [unit for unit in units if unit['phase'] == 'broad']
        self.assertEqual([unit['offset'] for unit in broad], [3, 5, 7, 3, 5, 7])
        self.assertEqual([unit['selected_files'] for unit in broad], [2, 2, 1, 2, 2, 1])
        self.assertEqual(coverage['testharness']['selected_broad_variants'], 5)
        self.assertEqual(coverage['test262']['selected_broad_variants'], 5)
        self.assertEqual(coverage['reftest']['plan'], 'NOTRUN')


class EvidenceTests(unittest.TestCase):
    def test_fixture_readiness_and_digests_are_required_before_server_use(self):
        args = SimpleNamespace(server_config=Path('/fixture/config.json'), ca_file=Path('/fixture/ca.pem'),
                               runner=Path('/fixture/full_runner.py'), origin='http://web-platform.test:8000',
                               secure_origin='https://web-platform.test:18443', h2_origin='https://web-platform.test:9000')
        inventory = {'wpt_revision': 'revision', 'manifest_sha256': 'manifest'}
        fixture = {'fixture_ready': True, 'server_identity_error': None, **inventory,
                   'server_config_sha256': 'bytes', 'ca_sha256': 'bytes', 'reporter_sha256': 'bytes',
                   'browser_host': 'web-platform.test', 'resolved_ports': {'http': [8000], 'https': [18443], 'h2': [9000]},
                   'server_pid': 123, 'server_start_identity': 'verified start'}
        with mock.patch.object(controller, 'file_digest', return_value='bytes'), mock.patch.object(controller, 'verify_server_live') as live:
            self.assertEqual(controller.validate_fixture(fixture, inventory, args)['server_pid'], 123)
            live.assert_called_once()
            with self.assertRaisesRegex(ValueError, 'readiness'):
                controller.validate_fixture(dict(fixture, fixture_ready=False), inventory, args)
            with self.assertRaisesRegex(ValueError, 'ca_sha256'):
                controller.validate_fixture(dict(fixture, ca_sha256='wrong'), inventory, args)

    def test_reused_server_pid_cannot_keep_old_fixture_valid(self):
        fixture = {'server_pid': 123, 'server_start_identity': 'original start'}
        with mock.patch.object(controller.subprocess, 'run', return_value=SimpleNamespace(returncode=0, stdout='replacement start\n')):
            with self.assertRaisesRegex(ValueError, 'replaced'):
                controller.verify_server_live(fixture)

    def test_chromium_runtime_version_is_pinned_across_pairs(self):
        args = SimpleNamespace(host_map='map', host_map_mode='exact', ca_file=Path('/ca.pem'))
        instance = controller.Controller(args, runner, {}, {})
        first = {'product': 'Chrome/155.0.1', 'revision': 'first', 'jsVersion': '15.5'}
        second = {'product': 'Chrome/155.0.2', 'revision': 'second', 'jsVersion': '15.5'}
        instance.pin_chromium_version({'browser_version': first})
        instance.pin_chromium_version({'browser_version': first})
        with self.assertRaisesRegex(controller.Halt, 'runtime version changed'):
            instance.pin_chromium_version({'browser_version': second})
        self.assertEqual(instance.state['chromium_browser_version'], first)

    def test_short_telemetry_is_not_a_failure_but_collector_failure_is(self):
        self.assertEqual(controller.row_issues(row()), [])
        self.assertEqual(controller.row_issues(row(metrics={'telemetry_status': 'unavailable', 'errors': []})), [])
        self.assertIn('resource collector failed', controller.row_issues(row(metrics={'telemetry_status': 'failed', 'errors': ['ps failed']})))

    def test_missing_reporter_and_incomplete_smoke_are_explicit(self):
        observed = row(status='TIMEOUT', reporter_state=None, subtests=[])
        observed.pop('subtest_count')
        self.assertEqual(controller.row_issues(observed), [])
        self.assertIn('synchronous reporter did not attach', controller.row_issues(observed, smoke=True))
        self.assertIn('smoke did not produce a complete assertion report', controller.row_issues(observed, smoke=True))

    def test_claimed_pass_without_reporter_is_unsafe_even_in_broad_run(self):
        self.assertIn('synchronous reporter did not attach', controller.row_issues(row(reporter_state=None)))

    def test_live_watch_waits_for_complete_lines_and_stops_at_first_infrastructure_error(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'results.jsonl'
            watcher = controller.ResultWatch(path, smoke=False)
            ordinary_timeout = row(status='TIMEOUT', reporter_state=None, subtests=[])
            broken = row('/url/b.html', status='INFRA_ERROR', message='CDP disconnected')
            with path.open('w') as output:
                output.write(json.dumps(ordinary_timeout) + '\n')
                output.write(json.dumps(broken)[:-1])
            self.assertIsNone(watcher.first_issue())
            with path.open('a') as output:
                output.write('}\n')
            issue = watcher.first_issue()
            self.assertEqual(issue['test'], '/url/b.html')
            self.assertEqual(issue['status'], 'INFRA_ERROR')

    def test_partial_budget_is_not_complete_even_if_summary_says_finished(self):
        schedule = [row('/url/a.html'), row('/url/b.html')]
        selection = {'fixture': 'selection'}
        unit = {'phase': 'broad', 'schedule_sha256': runner.digest(schedule), 'selection': selection}
        signature = {'engine': 'lumen', 'binary_sha256': 'binary', 'schedule_sha256': runner.digest(schedule), 'selection': selection}
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            controller.write_json(directory / 'schedule.json', schedule)
            controller.write_json(directory / 'run.json', {'signature': signature, 'finished_schedule': True, 'stopped_for_time_budget': True})
            (directory / 'results.jsonl').write_text(json.dumps(schedule[0]) + '\n')
            observed = controller.inspect_run(runner, directory, unit, 'lumen', 'binary')
        self.assertFalse(observed['complete'])
        self.assertEqual(observed['recorded_files'], 1)
        self.assertEqual(observed['issues'], [])

    def test_broken_cleanup_keeps_assertions_but_blocks_next_run(self):
        passing = row(cleanup={'error': 'owned process still alive'})
        self.assertTrue(runner.row_passes(passing))
        self.assertIn('browser/context/profile cleanup failed', controller.row_issues(passing))

    def test_runtime_switches_change_provenance_without_exposing_values(self):
        first = controller.environment_signature({'LUMEN_HOST_MAP_MODE': 'exact', 'UNRELATED': 'one'})
        unrelated = controller.environment_signature({'LUMEN_HOST_MAP_MODE': 'exact', 'UNRELATED': 'two'})
        changed = controller.environment_signature({'LUMEN_HOST_MAP_MODE': 'suffix', 'UNRELATED': 'one'})
        self.assertEqual(first, unrelated)
        self.assertNotEqual(first, changed)
        self.assertEqual(len(first), 64)


class AggregationAndStopTests(unittest.TestCase):
    def test_aggregate_uses_weighted_completed_shards_and_excludes_smoke(self):
        def summary(attempted, passed, assertions, passing):
            return {'attempted_files': attempted, 'files_passing_all_assertions': passed,
                    'executed_subtests_excluding_notrun': assertions, 'subtest_outcomes': {'PASS': passing}}

        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            units = [{'name': 'smoke', 'phase': 'smoke', 'type': 'testharness', 'selected_files': 6},
                     {'name': 'a', 'phase': 'broad', 'type': 'testharness', 'selected_files': 1},
                     {'name': 'b', 'phase': 'broad', 'type': 'testharness', 'selected_files': 3},
                     {'name': 'partial', 'phase': 'broad', 'type': 'testharness', 'selected_files': 2}]
            cohort = {'units': units, 'inventory_coverage': {}, 'files': {'fixture_manifest': {'sha256': 'fixture'}}}
            state = {'units': {}}
            for name, attempted, passed, assertions, passing in [('a', 1, 1, 3, 2), ('b', 3, 1, 7, 2)]:
                comparison = directory / (name + '.json')
                controller.write_json(comparison, {'lumen': summary(attempted, passed, assertions, passing),
                                                   'chromium': summary(attempted, attempted, assertions, assertions),
                                                   'parity': {'counts': {'chromium_passing_assertions': assertions, 'also_passed_by_lumen': passing}}})
                state['units'][name] = {'paired_complete': True, 'comparison': str(comparison),
                                        'comparison_sha256': controller.file_digest(comparison)}
            controller.aggregate(runner, directory, cohort, state)
            result = json.loads((directory / 'aggregate.json').read_text())['by_type']['testharness']
        self.assertEqual(result['planned_files'], 6)
        self.assertEqual(result['paired_completed_files'], 4)
        self.assertEqual(result['not_yet_paired_files'], 2)
        self.assertEqual(result['lumen']['attempted_file_pass_rate'], 0.5)
        self.assertEqual(result['lumen']['subtest_pass_rate'], 0.4)
        self.assertEqual(result['chromium_pass_retention'], 0.4)

    def test_disk_pressure_pauses_without_launching_any_browser(self):
        args = SimpleNamespace(host_map='web-platform.test=127.0.0.1', host_map_mode='exact',
                               ca_file=Path('/fixture/ca.pem'), output=Path('/fixture'), max_shards=None, min_free_gib=3)
        cohort = {'units': [{'name': 'first', 'phase': 'smoke'}]}
        instance = controller.Controller(args, runner, cohort, {'units': {}})
        with mock.patch.object(instance, 'save'), mock.patch.object(instance, 'run_engine') as run_engine, \
                mock.patch.object(controller.shutil, 'disk_usage', return_value=SimpleNamespace(free=2 * 1024 ** 3)):
            with self.assertRaisesRegex(controller.Pause, 'Disk headroom'):
                instance.run()
            run_engine.assert_not_called()


class FunctionalReferenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.session = self.directory / 'chromium-session'
        self.session.mkdir()
        (self.session / 'chromium-process.log').write_text('Teardown watchdog expired; recording dump and terminating.\n')
        controller.write_json(self.session / 'chromium.metrics-summary.json',
                              {'telemetry_status': 'partial', 'samples': 2, 'interval_cpu_samples': 1, 'errors': []})
        self.schedule = [{'type': 'testharness', 'test': '/url/a.html'}]
        self.unit = {'phase': 'smoke', 'schedule_sha256': runner.digest(self.schedule), 'selection': {'one': True}}
        self.meta = {'signature': {'engine': 'chromium', 'binary_sha256': 'binary',
                                  'schedule_sha256': self.unit['schedule_sha256'], 'selection': self.unit['selection'],
                                  'strict_infrastructure': True},
                     'finished_schedule': True, 'stopped_for_cleanup_failure': True,
                     'browser_version': {'product': 'Chrome/156.0.8078.47'},
                     'chromium_sessions': [str(self.session)],
                     'chromium_cleanup': {
                         'errors': ['Unexpected Chromium exit status 2'],
                         'process': {'error': None, 'teardown_error': 'Unexpected Chromium exit status 2',
                                     'root_exit_code': 2, 'root_exit_before_signal_cleanup': 2,
                                     'root_exit_observation_verified': True, 'sent_sigterm': False, 'sent_sigkill': False,
                                     'process_group_gone': True, 'remaining_group_members': {}},
                         'graceful_close': {'method': 'Browser.close', 'attempted': True,
                                            'acknowledged': True, 'process_group_gone': True}}}
        self.result = row()

    def tearDown(self):
        self.temporary.cleanup()

    def inspect(self, allow=True):
        controller.write_json(self.directory / 'schedule.json', self.schedule)
        controller.write_json(self.directory / 'run.json', self.meta)
        (self.directory / 'results.jsonl').write_text(json.dumps(self.result) + '\n')
        return controller.inspect_run(runner, self.directory, self.unit, 'chromium', 'binary',
                                      allow_known_reference_teardown=allow)

    def test_default_policy_still_blocks_known_reference_watchdog(self):
        value = self.inspect(allow=False)
        self.assertIn('stopped_for_cleanup_failure', value['issues'])
        self.assertIsNone(value['functional_reference_incident'])

    def test_explicit_policy_preserves_failure_and_allows_reported_functional_results(self):
        value = self.inspect()
        self.assertTrue(value['complete'])
        self.assertEqual(value['issues'], [])
        self.assertTrue(value['stops']['stopped_for_cleanup_failure'])
        self.assertFalse(value['functional_reference_incident']['cleanup_passed'])
        self.assertFalse(value['functional_reference_incident']['performance_release_gate_passed'])
        self.assertTrue(json.loads((self.directory / 'run.json').read_text())['stopped_for_cleanup_failure'])

    def test_other_case_failure_is_never_excused_by_reference_teardown_policy(self):
        self.result['cleanup'] = {'error': 'context disposal failed'}
        value = self.inspect()
        self.assertTrue(value['issues'])
        self.assertIsNone(value['functional_reference_incident'])

    def test_collector_failure_is_never_excused(self):
        controller.write_json(self.session / 'chromium.metrics-summary.json',
                              {'telemetry_status': 'failed', 'errors': ['ps failed']})
        self.assertTrue(self.inspect()['issues'])

    def test_smoke_missing_assertions_or_reporter_is_never_excused(self):
        self.result.update(status='TIMEOUT', subtests=[], reporter_state=None)
        self.result.pop('subtest_count')
        self.assertTrue(self.inspect()['issues'])

    def test_broad_timeout_stays_a_functional_failure_without_blocking_safe_teardown(self):
        self.unit['phase'] = 'broad'
        self.meta['signature']['strict_infrastructure'] = False
        self.result.update(status='TIMEOUT', subtests=[], reporter_state=None)
        self.result.pop('subtest_count')
        value = self.inspect()
        self.assertEqual(value['issues'], [])
        self.assertEqual(value['reporting_counts']['external_watchdog_timeout_files'], 1)
        self.assertFalse(runner.row_passes(self.result))

    def test_missing_watchdog_diagnostic_does_not_allow_an_arbitrary_exit_two(self):
        (self.session / 'chromium-process.log').write_text('A different browser failure\n')
        self.assertTrue(self.inspect()['issues'])

    def test_wrong_reference_version_and_runner_signal_are_blocked(self):
        self.meta['browser_version']['product'] = 'Chrome/155.0.0.0'
        self.assertTrue(self.inspect()['issues'])
        self.meta['browser_version']['product'] = 'Chrome/156.0.8078.47'
        self.meta['chromium_cleanup']['process']['sent_sigterm'] = True
        self.assertTrue(self.inspect()['issues'])

    def test_short_metrics_do_not_become_a_performance_pass(self):
        controller.write_json(self.session / 'chromium.metrics-summary.json',
                              {'telemetry_status': 'unavailable', 'samples': 0, 'interval_cpu_samples': 0, 'errors': []})
        value = self.inspect()
        self.assertEqual(value['issues'], [])
        self.assertFalse(value['functional_reference_incident']['performance_release_gate_passed'])

    def test_comparison_annotation_keeps_original_failure_and_assertion_counts(self):
        observed = self.inspect()
        path = self.directory / 'comparison.json'
        original = {'lumen': {'subtest_outcomes': {'PASS': 7, 'FAIL': 3}},
                    'run_state': {'chromium': {'stops': {'stopped_for_cleanup_failure': True}}}}
        controller.write_json(path, original)
        cohort = {'configuration': {'allow_known_reference_teardown': True}}
        controller.qualify_comparison(path, cohort, observed)
        saved = json.loads(path.read_text())
        self.assertEqual(saved['lumen'], original['lumen'])
        self.assertEqual(saved['run_state'], original['run_state'])
        self.assertFalse(saved['reference_qualification']['reference_performance_release_gate_passed'])


if __name__ == '__main__':
    unittest.main()
