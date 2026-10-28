"""Reporter transport regressions, executed only on the authorized Mac gate.

The Node subprocess evaluates the actual served JavaScript with controlled
testharness callbacks. These are harness unit checks, not browser WPT results.
"""
import importlib.util
import json
from pathlib import Path
import subprocess
import unittest

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('repeat_runner', HERE / 'full_runner.py')
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)
NODE = '/opt/homebrew/bin/node'

PRODUCER = r"""
const fs = require('node:fs'), vm = require('node:vm');
const lines = [], callbacks = {};
const context = {
  console: {log: line => lines.push(line)},
  add_result_callback: fn => callbacks.result = fn,
  add_completion_callback: fn => callbacks.complete = fn,
};
const scenario = process.argv[2];
if (scenario === 'child') context.window = {top: {}};
vm.runInNewContext(fs.readFileSync(process.argv[1], 'utf8'), context, {timeout: 2000});
if (scenario !== 'child') {
  const tests = [{name: 'slice()', status: 2, message: 'Test timed out'},
                 {name: 'text()', status: 3, message: null}];
  let status = {status: 1, message: "Cannot read properties of undefined (reading 'Blob')"};
  if (scenario === 'long') {
    tests[0].name = '\uD83D\uDCA9'.repeat(500);
    status.message = '\u00E9'.repeat(2000);
  }
  callbacks.result(tests[0]);
  callbacks.complete(tests, status);
  if (scenario === 'changed-status') status = {status: 0, message: null};
  if (scenario === 'changed-assertion') tests[0].status = 0;
  if (scenario === 'changed-name') tests[0].name = 'changed assertion';
  if (scenario === 'replaced-object') tests[0] = {...tests[0]};
  if (scenario === 'duplicate-object') tests[1] = tests[0];
  if (scenario !== 'single') callbacks.complete(tests, status);
  if (scenario === 'triple') callbacks.complete(tests, status);
}
process.stdout.write(JSON.stringify(lines));
"""


def collected(lines):
    collector = runner.Collector()
    for line in lines:
        collector.line(line)
    return collector, collector.result({'status': 'TIMEOUT', 'message': 'external watchdog'})


def record(kind, value):
    return 'LUME_WPT_' + kind + ' ' + json.dumps(value, ensure_ascii=True)


class ReporterProducerTests(unittest.TestCase):
    def produce(self, scenario):
        result = subprocess.run([NODE, '-e', PRODUCER, str(HERE / 'testharnessreport.js'), scenario],
                                text=True, capture_output=True, timeout=8, check=True)
        self.assertEqual(result.stderr, '')
        return json.loads(result.stdout)

    def test_one_completion_retains_all_failure_outcomes_once(self):
        lines = self.produce('single')
        collector, row = collected(lines)
        self.assertEqual(row['status'], 'ERROR')
        self.assertEqual([test['status'] for test in row['subtests']], ['TIMEOUT', 'NOTRUN'])
        self.assertEqual(row['subtest_count'], 2)
        self.assertEqual(collector.parse_errors, [])
        self.assertEqual(sum(line.startswith('LUME_WPT_DONE ') for line in lines), 1)
        self.assertNotIn('reporter_diagnostics', row)

    def test_identical_repeat_is_explicit_and_cannot_turn_error_into_pass(self):
        lines = self.produce('identical')
        collector, row = collected(lines)
        self.assertEqual(sum(line.startswith('LUME_WPT_DONE ') for line in lines), 1)
        self.assertEqual(sum(line.startswith('LUME_WPT_REPEAT ') for line in lines), 1)
        self.assertEqual(row['status'], 'ERROR')
        self.assertIn("reading 'Blob'", row['message'])
        self.assertEqual(row['reporter_diagnostics']['identical_completion_repeats'], 1)
        self.assertEqual(collector.parse_errors, [])
        self.assertFalse(runner.row_passes(row))

    def test_multiple_unchanged_callbacks_have_a_bounded_counter(self):
        _, row = collected(self.produce('triple'))
        self.assertEqual(row['status'], 'ERROR')
        self.assertEqual(len(row['subtests']), 2)
        self.assertEqual(row['reporter_diagnostics']['completion_callbacks'], 3)
        self.assertEqual(row['reporter_diagnostics']['identical_completion_repeats'], 2)

    def test_changed_harness_status_remains_an_infrastructure_conflict(self):
        _, row = collected(self.produce('changed-status'))
        self.assertEqual(row['status'], 'INFRA_ERROR')
        self.assertTrue(row['parse_errors'])

    def test_changed_assertion_outcome_remains_an_infrastructure_conflict(self):
        _, row = collected(self.produce('changed-assertion'))
        self.assertEqual(row['status'], 'INFRA_ERROR')
        self.assertEqual(row['subtests'][0]['status'], 'TIMEOUT')

    def test_changed_assertion_name_remains_an_infrastructure_conflict(self):
        _, row = collected(self.produce('changed-name'))
        self.assertEqual(row['status'], 'INFRA_ERROR')
        self.assertEqual(row['subtests'][0]['name'], 'slice()')

    def test_replaced_or_duplicated_test_objects_do_not_pass_snapshot_validation(self):
        for scenario in ('replaced-object', 'duplicate-object'):
            with self.subTest(scenario=scenario):
                _, row = collected(self.produce(scenario))
                self.assertEqual(row['status'], 'INFRA_ERROR')

    def test_iframe_reporter_does_not_publish_into_the_top_level_transport(self):
        self.assertEqual(self.produce('child'), [])

    def test_chunked_unicode_repeats_stay_under_the_native_console_limit(self):
        lines = self.produce('long')
        self.assertTrue(all(line.isascii() and len(line.encode()) < 2000 for line in lines))
        collector, row = collected(lines)
        self.assertEqual(collector.parse_errors, [])
        self.assertEqual(collector.chunks, {})
        self.assertEqual(row['status'], 'ERROR')
        self.assertEqual(row['subtests'][0]['name'], '\U0001f4a9' * 500)
        self.assertEqual(row['message'], '\u00e9' * 2000)
        self.assertEqual(row['reporter_diagnostics']['identical_completion_repeats'], 1)


class CollectorRepeatTests(unittest.TestCase):
    def setUp(self):
        self.subtest = {'name': 'one', 'status': 'TIMEOUT', 'message': 'Test timed out'}
        self.done = {'status': 'ERROR', 'message': 'actual engine error', 'subtest_count': 1}
        self.lines = [record('READY', {'status': 'ATTACHED'}), record('SUB', self.subtest), record('DONE', self.done)]

    def repeat(self, **changes):
        value = {'status': 'IDENTICAL', 'completion_callback_count': 2, 'completion': dict(self.done)}
        value.update(changes)
        return record('REPEAT', value)

    def test_legacy_duplicate_done_is_still_rejected(self):
        _, row = collected(self.lines + [record('DONE', self.done)])
        self.assertEqual(row['status'], 'INFRA_ERROR')

    def test_repeat_before_first_completion_is_rejected(self):
        _, row = collected([self.repeat()] + self.lines)
        self.assertEqual(row['status'], 'INFRA_ERROR')

    def test_repeat_requires_a_contiguous_integer_callback_counter(self):
        for count in (0, 1, 3, True, '2', 2.0):
            with self.subTest(count=count):
                _, row = collected(self.lines + [self.repeat(completion_callback_count=count)])
                self.assertEqual(row['status'], 'INFRA_ERROR')

    def test_repeat_cannot_change_completion_payload(self):
        for field, value in (('status', 'OK'), ('message', 'hidden error'), ('subtest_count', 2)):
            changed = dict(self.done); changed[field] = value
            _, row = collected(self.lines + [self.repeat(completion=changed)])
            self.assertEqual(row['status'], 'INFRA_ERROR')

    def test_repeat_cannot_hide_an_extra_assertion(self):
        _, row = collected(self.lines + [record('SUB', dict(self.subtest, name='extra')), self.repeat()])
        self.assertEqual(row['status'], 'INFRA_ERROR')
        self.assertEqual(len(row['subtests']), 2)

    def test_reporter_conflict_is_never_accepted_as_a_repeated_result(self):
        _, row = collected(self.lines + [self.repeat(status='CONFLICT')])
        self.assertEqual(row['status'], 'INFRA_ERROR')


if __name__ == '__main__':
    unittest.main()
