import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location('full_runner_streaming', Path(__file__).with_name('full_runner.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def write_rows(path, rows):
    with Path(path).open('w') as output:
        for row in rows:
            output.write(json.dumps(row, ensure_ascii=True) + '\n')


def result(name, status='PASS'):
    return {'type': 'testharness', 'test': '/' + name, 'status': 'OK',
            'subtests': [{'name': 'assertion \ud900', 'status': status}]}


class StreamingResultTests(unittest.TestCase):
    def test_reader_yields_valid_prefix_before_inspecting_invalid_later_record(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'results.jsonl'
            path.write_text('{"test":"first"}\nnot-json\n{"test":"third"}\n')
            rows = runner.iter_jsonl(path)
            self.assertEqual(next(rows), {'test': 'first'})
            with self.assertRaisesRegex(ValueError, 'line 2'):
                next(rows)

    def test_repair_never_discards_invalid_interior_results(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'results.jsonl'
            contents = '{"test":"first"}\nnot-json\n{"test":"third"}\n'
            path.write_text(contents)
            with self.assertRaises(ValueError):
                list(runner.iter_jsonl(path, repair=True))
            self.assertEqual(path.read_text(), contents)

    def test_invalid_complete_final_line_is_not_treated_as_interruption(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'results.jsonl'
            contents = '{"test":"first"}\nnot-json\n'
            path.write_text(contents)
            with self.assertRaises(ValueError):
                list(runner.iter_jsonl(path, repair=True))
            self.assertEqual(path.read_text(), contents)

    def test_valid_final_record_without_newline_can_be_resumed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'results.jsonl'
            path.write_text('{"test":"complete"}')
            self.assertEqual(list(runner.iter_jsonl(path, repair=True)), [{'test': 'complete'}])
            self.assertEqual(path.read_text(), '{"test":"complete"}\n')

    def test_stream_comparison_preserves_missing_assertions_and_unrun_files(self):
        schedule = [result(name) for name in ('a', 'b', 'c', 'd')]
        lumen = [result('a'), result('c', 'FAIL')]
        chrome = [result('a'), result('b'), result('c')]
        with tempfile.TemporaryDirectory() as directory:
            lp, cp = Path(directory) / 'lumen.jsonl', Path(directory) / 'chrome.jsonl'
            write_rows(lp, lumen)
            write_rows(cp, chrome)
            with mock.patch.object(runner, 'read_jsonl', side_effect=AssertionError('bulk reader used')):
                ls, cs, parity = runner.compare_streams(lp, cp, schedule)
            self.assertEqual(ls, runner.summarize(lumen, schedule))
            self.assertEqual(cs, runner.summarize(chrome, schedule))
            self.assertEqual(parity, runner.parity(lumen, chrome, schedule))
            self.assertEqual(parity['chromium_pass_retention'], 1 / 3)
            self.assertEqual(parity['counts']['lumen_missing'], 1)
            self.assertEqual(parity['counts']['chromium_unrun_files'], 1)

    def test_out_of_order_results_are_rejected_instead_of_mispaired(self):
        schedule = [result('a'), result('b')]
        with tempfile.TemporaryDirectory() as directory:
            lp, cp = Path(directory) / 'lumen.jsonl', Path(directory) / 'chrome.jsonl'
            write_rows(lp, [result('b'), result('a')])
            write_rows(cp, schedule)
            with self.assertRaisesRegex(ValueError, 'schedule order'):
                runner.compare_streams(lp, cp, schedule)

    def test_duplicate_results_are_rejected(self):
        schedule = [result('a')]
        with tempfile.TemporaryDirectory() as directory:
            lp, cp = Path(directory) / 'lumen.jsonl', Path(directory) / 'chrome.jsonl'
            write_rows(lp, [result('a'), result('a')])
            write_rows(cp, schedule)
            with self.assertRaisesRegex(ValueError, 'duplicated'):
                runner.compare_streams(lp, cp, schedule)

    def test_resume_rejects_ordered_but_noncontiguous_results(self):
        schedule = [result('a'), result('b'), result('c')]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'results.jsonl'
            write_rows(path, [result('a'), result('c')])
            with self.assertRaisesRegex(ValueError, 'contiguous'):
                runner.restore_accounting(path, schedule)
            write_rows(path, [result('a'), result('b')])
            accounting = runner.restore_accounting(path, schedule)
            self.assertEqual(accounting.value()['remaining_files'], 1)

    def test_edited_schedule_is_rejected_even_if_metadata_still_matches(self):
        schedule = [result('a'), result('b')]
        signature = {'schedule_sha256': runner.digest(schedule)}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'schedule.json'
            runner.write_json(path, schedule)
            self.assertEqual(runner.verified_schedule(directory, signature), schedule)
            runner.write_json(path, [result('different')])
            with self.assertRaisesRegex(ValueError, 'Stored schedule differs'):
                runner.verified_schedule(directory, signature)


class ProvenanceAndMonitoringTests(unittest.TestCase):
    def test_binary_identity_changes_when_path_is_replaced(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'browser'
            binary.write_text('first binary')
            before = runner.binary_identity(binary)
            replacement = Path(directory) / 'next-browser'
            replacement.write_text('replacement binary')
            replacement.replace(binary)
            self.assertNotEqual(before, runner.binary_identity(binary))

    def test_cleanup_checks_group_exit_after_signal_delivery(self):
        cleanup = {'sent_sigterm': True, 'sent_sigkill': True, 'error': None}
        module = SimpleNamespace(cleanup_owned_group=lambda *args: dict(cleanup),
                                 owned_group_members=lambda pid: {11: 'verified-start'})
        sampler = SimpleNamespace(mod=module, observed_identities={11: 'verified-start'})
        process = SimpleNamespace(pid=10, poll=lambda: 0)
        with mock.patch.object(runner.time, 'monotonic', side_effect=[0, 2, 2]), \
                mock.patch.object(runner, 'process_group_diagnostics', return_value={'members': []}):
            observed = runner.terminate_owned(process, sampler)
        self.assertFalse(observed['process_group_gone'])
        self.assertIn('remains', observed['error'])
        module.owned_group_members = lambda pid: {}
        observed = runner.terminate_owned(process, sampler)
        self.assertTrue(observed['process_group_gone'])
        self.assertIsNone(observed['error'])

    def test_native_ca_bytes_and_routing_change_the_resume_signature(self):
        with tempfile.TemporaryDirectory() as directory:
            ca = Path(directory) / 'test-ca.pem'
            ca.write_text('test certificate one')
            env = {'SSL_CERT_FILE': str(ca), 'LUMEN_HOST_MAP': 'web-platform.test=127.0.0.1', 'LUMEN_HOST_MAP_MODE': 'exact'}
            with mock.patch.dict(os.environ, env, clear=True):
                first = runner.native_fixture_signature()
                ca.write_text('test certificate two')
                second = runner.native_fixture_signature()
                os.environ['LUMEN_HOST_MAP_MODE'] = 'suffix'
                third = runner.native_fixture_signature()
            self.assertNotEqual(first, second)
            self.assertNotEqual(second, third)
            self.assertEqual(first['host_map'], env['LUMEN_HOST_MAP'])

    def test_short_case_missing_samples_do_not_equal_collector_failure(self):
        self.assertIsNone(runner.measurement_failure({'metrics': {'telemetry_status': 'unavailable', 'errors': []}}))
        self.assertIsNone(runner.measurement_failure({'metrics': {'telemetry_status': 'partial', 'errors': []}}))
        self.assertEqual(runner.measurement_failure({'metrics': {'telemetry_status': 'failed', 'errors': ['ps failed']}}), ['ps failed'])
        self.assertEqual(runner.measurement_failure({'telemetry_errors': ['collector stopped']}), ['collector stopped'])

    def test_one_startup_sample_is_partial_and_not_an_interval_cpu_measurement(self):
        sampler = object.__new__(runner.Sampler)
        sampler.final_summary, sampler.root_identity = None, 'verified-start'
        sampler.errors, sampler.stop_reason = [], 'requested_stop'
        sampler.rows = [{'elapsed_s': 0.01, 'cpu_percent': 0, 'interval_cpu_percent': None, 'rss_kb': 1024, 'processes': 1}]
        sampler.totals = runner.SampleTotals()
        sampler.totals.add(sampler.rows[0])
        sampler.stopped = threading.Event()
        sampler.thread = SimpleNamespace(join=lambda **kwargs: None, is_alive=lambda: False)
        sampler.mod = runner.monitor_module()
        observed = sampler.stop()
        self.assertEqual(observed['telemetry_status'], 'partial')
        self.assertEqual(observed['interval_cpu_coverage'], 'unavailable')
        self.assertEqual(observed['observed_span_s'], 0)
        self.assertIsNone(runner.measurement_failure({'metrics': observed}))

    def test_bounded_window_preserves_lifetime_peaks_without_inventing_quantiles(self):
        totals = runner.SampleTotals()
        rows = [{'elapsed_s': i, 'cpu_percent': 100 if i == 0 else 1,
                 'interval_cpu_percent': None if i == 0 else 2,
                 'rss_kb': 100 * 1024 if i == 0 else 1024, 'processes': 1}
                for i in range(601)]
        for row in rows:
            totals.add(row)
        recent = runner.monitor_module().summarize(rows[-600:], 0)
        observed = totals.extend_summary(recent, 600)
        self.assertEqual(observed['samples'], 601)
        self.assertEqual(observed['interval_cpu_samples'], 600)
        self.assertEqual(observed['rss_peak_mb'], 100)
        self.assertEqual(observed['cpu_peak_percent'], 100)
        self.assertEqual(observed['cpu_mean_percent'], round(700 / 601, 3))
        self.assertIsNone(observed['steady_cpu_p95_percent'])
        self.assertEqual(observed['recent_window_summary']['steady_cpu_p95_percent'], 1)

    def test_context_cleanup_failure_preserves_assertions_and_remains_visible(self):
        browser = object.__new__(runner.Chromium)
        browser.port, browser.process = 12345, SimpleNamespace(pid=99)
        browser.sampler = SimpleNamespace(errors=[], stop_reason=None, stopped=threading.Event())

        def call(method, params=None):
            if method == 'Target.createBrowserContext':
                return {'browserContextId': 'owned-context'}
            if method == 'Target.createTarget':
                return {'targetId': 'owned-target'}
            if method == 'Target.disposeBrowserContext':
                raise RuntimeError('context disposal failed')
            raise AssertionError(method)

        browser.browser = SimpleNamespace(call=call)
        messages = [
            'LUME_WPT_READY {"status":"ATTACHED"}',
            'LUME_WPT_SUB {"name":"passed assertion","status":"PASS"}',
            'LUME_WPT_DONE {"status":"OK","subtest_count":1}',
        ]
        events = iter({'method': 'Runtime.consoleAPICalled', 'params': {'args': [{'value': message}]}} for message in messages)
        page = SimpleNamespace(call=lambda *args, **kwargs: {}, event=lambda timeout: next(events), close=lambda: None)
        targets = json.dumps([{'id': 'owned-target', 'webSocketDebuggerUrl': 'ws://127.0.0.1:12345/page'}]).encode()
        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.object(runner, 'CDP', return_value=page), mock.patch.object(runner.urllib.request, 'urlopen', return_value=io.BytesIO(targets)):
                observed = browser.run('http://web-platform.test:8000/example.html', 5, Path(directory) / 'case')
        self.assertEqual(observed['status'], 'OK')
        self.assertEqual(observed['subtests'][0]['status'], 'PASS')
        self.assertIn('context disposal failed', observed['cleanup']['error'])
        self.assertEqual(observed['browser_pid'], 99)


class FullScheduleTests(unittest.TestCase):
    def inventory(self):
        rows = [
            {'type': 'testharness', 'test': '/a/test.any.worker.html'},
            {'type': 'test262', 'test': '/third_party/test262/a.test262.strict.html'},
            {'type': 'testharness', 'test': '/a/test.any.serviceworker.html'},
            {'type': 'reftest', 'test': '/visual/reference.html'},
            {'type': 'testharness', 'test': '/a/test.any.html'},
            {'type': 'testharness', 'test': '/b/test.https.html'},
        ]
        return {'tests': rows, 'test_variants_by_type': {'testharness': 4, 'test262': 1, 'reftest': 1}}

    def test_filtered_shards_preserve_worker_and_secure_variants_in_stable_order(self):
        selected, descriptor = runner.select_schedule(self.inventory(), 'testharness', ['/a/'], 1, 2)
        self.assertEqual([row['test'] for row in selected], ['/a/test.any.serviceworker.html', '/a/test.any.worker.html'])
        self.assertEqual(descriptor['eligible_files_before_slice'], 3)
        self.assertEqual(descriptor['offset'], 1)
        self.assertEqual(descriptor['selected_files'], 2)
        full, _ = runner.select_schedule(self.inventory(), 'testharness')
        self.assertEqual(len(full), 4)
        self.assertTrue(any(row['test'].endswith('.https.html') for row in full))

    def test_invalid_selection_arguments_cannot_silently_change_scope(self):
        for types, offset, limit in [('unknown', 0, None), ('testharness', -1, None),
                                     ('testharness', 0, 0), ('testharness', 0, -2),
                                     ('testharness', 99, 1)]:
            with self.subTest(types=types, offset=offset, limit=limit), self.assertRaises(ValueError):
                runner.select_schedule(self.inventory(), types, offset=offset, limit=limit)

    def test_test262_and_web_platform_rates_are_separate(self):
        rows = [result('web', 'FAIL'), dict(result('javascript'), type='test262'),
                {'type': 'test262', 'test': '/empty', 'status': 'OK', 'subtests': []}]
        summary = runner.summarize(rows, rows)
        self.assertEqual(summary['by_type']['testharness']['attempted_file_pass_rate'], 0)
        self.assertEqual(summary['by_type']['test262']['attempted_file_pass_rate'], 0.5)
        self.assertEqual(summary['zero_assertion_files_by_type']['test262'], 1)
        self.assertEqual(summary['files_passing_all_assertions'], 1)

    def test_test262_passes_do_not_hide_missing_web_platform_assertions(self):
        chrome = [result('web'), dict(result('javascript'), type='test262')]
        lumen = [result('web', 'FAIL'), dict(result('javascript'), type='test262')]
        compared = runner.parity(lumen, chrome, chrome)
        self.assertEqual(compared['by_type']['testharness']['chromium_pass_retention'], 0)
        self.assertEqual(compared['by_type']['test262']['chromium_pass_retention'], 1)
        empty = {'type': 'test262', 'test': '/empty', 'status': 'PASS', 'subtests': []}
        self.assertIsNone(runner.parity([empty], [empty], [empty])['chromium_pass_retention'])

    def test_unsupported_inventory_classes_remain_explicitly_notrun(self):
        inventory = self.inventory()
        selected, _ = runner.select_schedule(inventory, 'testharness', limit=2)
        coverage = runner.inventory_coverage(inventory, selected)
        self.assertEqual(coverage['reftest']['execution_plan'], 'NOTRUN')
        self.assertFalse(coverage['reftest']['executor_available'])
        self.assertEqual(coverage['test262']['execution_plan'], 'NOTRUN')
        self.assertEqual(coverage['testharness']['outside_selection_variants'], 2)

    def test_stale_or_modified_upstream_sources_fail_before_browser_launch(self):
        inventory = {'wpt_root': '/fixture', 'wpt_revision': 'pinned', 'manifest_sha256': 'manifest'}
        with mock.patch.object(runner, 'git_value', return_value='different'), mock.patch.object(runner, 'file_digest', return_value='manifest'):
            with self.assertRaisesRegex(ValueError, 'changed since inventory'):
                runner.source_fixture_signature(inventory)
        with mock.patch.object(runner, 'git_value', return_value='pinned'), mock.patch.object(runner, 'file_digest', return_value='manifest'), \
                mock.patch.object(runner.subprocess, 'run', return_value=SimpleNamespace(stdout=b'changed tracked test')):
            with self.assertRaisesRegex(ValueError, 'modified'):
                runner.source_fixture_signature(inventory)


if __name__ == '__main__':
    unittest.main()
