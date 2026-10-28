import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location('full_runner', Path(__file__).with_name('full_runner.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class ManifestTests(unittest.TestCase):
    def test_generated_urls_and_variants_are_not_lost(self):
        manifest = {'version': 9, 'url_base': '/', 'items': {
            'testharness': {'url': {'a.any.js': ['hash', ['url/a.any.html', {}], ['url/a.any.worker.html?mode=x', {'timeout': 'long'}]]}},
            'reftest': {'css': {'a.html': ['hash', [None, [['/css/ref.html', '==']], {}]]}},
            'support': {'resources': {'a.js': ['hash', []]}}
        }}
        # Upstream support entries vary; a hash alone is the non-runnable form.
        manifest['items']['support']['resources']['a.js'] = ['hash']
        result = runner.inventory_manifest(manifest)
        self.assertEqual(result['total_test_variants'], 3)
        self.assertEqual(result['source_files_by_type']['support'], 1)
        self.assertIn('/url/a.any.worker.html?mode=x', [x['test'] for x in result['tests']])
        self.assertEqual(next(x for x in result['tests'] if x['type'] == 'reftest')['test'], '/css/a.html')

    def test_unknown_manifest_fails_loudly(self):
        with self.assertRaises(ValueError):
            runner.inventory_manifest({'version': 999, 'items': {}})

    def test_nonroot_manifest_base_joins_relative_variants(self):
        m = {'version': 9, 'url_base': '/mount/', 'items': {'testharness': {'a.any.js': ['h', ['a.any.worker.html?q=1', {}]]}}}
        self.assertEqual(runner.inventory_manifest(m)['tests'][0]['test'], '/mount/a.any.worker.html?q=1')

    def test_protocol_matches_upstream_serviceworker_and_query_flags(self):
        args = SimpleNamespace(origin='http://web-platform.test:8000', secure_origin='https://web-platform.test:18443', h2_origin='https://web-platform.test:9000')
        self.assertEqual(runner.test_url({'test': '/a.any.serviceworker.html'}, args), 'https://web-platform.test:18443/a.any.serviceworker.html')
        self.assertEqual(runner.test_url({'test': '/a.html?wpt_flags=https&wpt_flags=www'}, args), 'https://www.web-platform.test:18443/a.html?wpt_flags=https&wpt_flags=www')
        self.assertEqual(runner.test_url({'test': '/a.h2.html'}, args), 'https://web-platform.test:9000/a.h2.html')


class AccountingTests(unittest.TestCase):
    def test_zero_assertion_ok_is_not_a_passing_file(self):
        row = {'type': 'testharness', 'test': '/empty', 'status': 'OK', 'subtests': []}
        result = runner.summarize([row], [row])
        self.assertEqual(result['files_passing_all_assertions'], 0)
        self.assertEqual(result['zero_assertion_testharness_files'], 1)

    def test_timeouts_reduce_attempted_pass_rate(self):
        rows = [{'type': 'testharness', 'test': '/pass', 'status': 'OK', 'subtests': [{'name': 'x', 'status': 'PASS'}]}]
        rows += [{'type': 'testharness', 'test': '/timeout' + str(i), 'status': 'TIMEOUT', 'subtests': []} for i in range(9)]
        result = runner.summarize(rows, rows)
        self.assertEqual(result['attempted_file_pass_rate'], 0.1)
        self.assertEqual(result['all_assertions_pass_file_rate'], 1.0)

    def test_file_harness_ok_does_not_mean_file_passed(self):
        row = {'type': 'testharness', 'test': '/a', 'status': 'OK', 'subtests': [{'name': 'x', 'status': 'FAIL'}]}
        result = runner.summarize([row], [row, {'type': 'testharness', 'test': '/b'}])
        self.assertEqual(result['files_passing_all_assertions'], 0)
        self.assertEqual(result['valid_completed_files'], 1)
        self.assertEqual(result['remaining_files'], 1)

    def test_missing_lumen_assertions_reduce_chromium_parity(self):
        schedule = [{'type': 'testharness', 'test': '/a'}, {'type': 'testharness', 'test': '/b'}]
        chrome = [dict(schedule[0], status='OK', subtests=[{'name': 'x', 'status': 'PASS'}, {'name': 'y', 'status': 'PASS'}])]
        lumen = [dict(schedule[0], status='OK', subtests=[{'name': 'x', 'status': 'PASS'}])]
        result = runner.parity(lumen, chrome, schedule)
        self.assertEqual(result['chromium_pass_retention'], 0.5)
        self.assertEqual(result['counts']['chromium_unrun_files'], 1)
        self.assertEqual(result['counts']['lumen_missing'], 1)

    def test_unscheduled_or_duplicate_results_are_rejected(self):
        row = {'type': 'testharness', 'test': '/a', 'status': 'OK', 'subtests': []}
        with self.assertRaises(ValueError):
            runner.summarize([row], [])
        with self.assertRaises(ValueError):
            runner.summarize([row, row], [row])

    def test_timeouts_and_notrun_stay_visible(self):
        rows = [{'type': 'testharness', 'test': '/a', 'status': 'TIMEOUT', 'subtests': []},
                {'type': 'reftest', 'test': '/b', 'status': 'NOTRUN', 'subtests': []}]
        result = runner.summarize(rows, rows)
        self.assertIsNone(result['all_assertions_pass_file_rate'])
        self.assertEqual(result['scheduled_file_pass_fraction'], 0)
        self.assertEqual(result['file_outcomes']['TIMEOUT'], 1)
        self.assertEqual(result['file_outcomes']['NOTRUN'], 1)

    def test_reporter_truncation_is_infrastructure_error(self):
        c = runner.Collector()
        c.line('LUME_WPT_DONE {"status":"OK","subtest_count":1}')
        self.assertEqual(c.result({})['status'], 'INFRA_ERROR')

    def test_marker_text_inside_a_subtest_name_is_not_a_second_record(self):
        collector = runner.Collector()
        subtest = {'name': 'LUME_WPT_DONE and LUME_WPT_SUB marker text', 'status': 'PASS', 'message': 'LUME_WPT_DONE literal'}
        collector.line('LUME_WPT_SUB ' + json.dumps(subtest))
        collector.line('LUME_WPT_DONE {"status":"OK","subtest_count":1}')
        self.assertEqual(collector.result({})['status'], 'OK')
        self.assertEqual(collector.subtests, [subtest])

    def test_long_surrogate_names_survive_native_console_chunking(self):
        collector = runner.Collector()
        subtest = {'name': 'LUME_WPT_DONE ' + ('\ud900\u2028\U0001f680' * 800), 'status': 'PASS', 'message': None}
        payload = json.dumps(subtest, ensure_ascii=True)
        pieces = [payload[index:index+600] for index in range(0, len(payload), 600)]
        for index, piece in enumerate(pieces):
            line = 'LUME_WPT_CHUNK ' + json.dumps({'id': 1, 'kind': 'SUB', 'index': index, 'total': len(pieces), 'data': piece}, ensure_ascii=True)
            self.assertLess(len(line.encode('utf-8')), 2000)
            collector.line('[js log] ' + line)
        collector.line('LUME_WPT_DONE {"status":"OK","subtest_count":1}')
        self.assertEqual(collector.result({})['status'], 'OK')
        self.assertEqual(collector.subtests, [subtest])

    def test_missing_chunk_cannot_be_reported_as_a_pass(self):
        collector = runner.Collector()
        collector.line('LUME_WPT_CHUNK {"id":1,"kind":"SUB","index":0,"total":2,"data":"{\\\"name\\\":"}')
        collector.line('LUME_WPT_DONE {"status":"OK","subtest_count":0}')
        self.assertEqual(collector.result({})['status'], 'INFRA_ERROR')

    def test_machine_json_preserves_lone_surrogates(self):
        value = {'name': '\ud900', 'message': 'paired \U0001f680 and unpaired \udfff'}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'unicode.json'
            runner.write_json(path, value)
            self.assertEqual(json.loads(path.read_text()), value)

    def test_resume_repairs_only_interrupted_final_record(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'results.jsonl'
            path.write_text('{"test":"a"}\n{"test":')
            self.assertEqual(runner.read_jsonl(path, repair=True), [{'test': 'a'}])
            self.assertEqual(path.read_text(), '{"test":"a"}\n')
            self.assertTrue(path.with_suffix('.interrupted-tail').exists())


if __name__ == '__main__':
    unittest.main()
