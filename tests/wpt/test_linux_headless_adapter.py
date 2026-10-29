"""Evidence qualification regressions for explicit Linux execution."""
import importlib.util
from pathlib import Path
import unittest

PATH = Path(__file__).with_name('linux_headless_runner.py')
spec = importlib.util.spec_from_file_location('linux_headless_adapter_checks', PATH)
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)


class QualificationTests(unittest.TestCase):
    def test_completed_pass_cannot_hide_late_adapter_exception(self):
        common = adapter.load_helpers()
        collector = common.Collector()
        collector.done = {'status': 'OK'}
        collector.subtests = [{'name': 'actual pass', 'status': 'PASS'}]
        result = adapter.adapter_failure(collector, RuntimeError('late read failure'))
        self.assertEqual(result['status'], 'INFRA_ERROR')
        self.assertEqual(result['original_harness_status'], 'OK')
        self.assertEqual(result['subtests'], collector.subtests)
        self.assertTrue(result['telemetry_errors'])
        self.assertEqual(adapter.summarize([result], 1)['fullyPassing'], 0)

    def test_monitor_error_disqualifies_otherwise_passing_harness(self):
        row = {'status': 'OK', 'subtests': [{'name': 'real pass', 'status': 'PASS'}],
               'metrics': {'errors': ['sampler did not stop']}}
        summary = adapter.summarize([row], 1515)
        self.assertEqual(summary['fullyPassing'], 0)
        self.assertEqual(summary['recorded'], 1)
        self.assertEqual(summary['unrun'], 1514)

    def test_unrun_and_timeout_remain_in_frozen_denominator(self):
        rows = [{'status': 'OK', 'subtests': [{'name': 'pass', 'status': 'PASS'}]},
                {'status': 'TIMEOUT', 'subtests': [{'name': 'partial', 'status': 'PASS'}]}]
        summary = adapter.summarize(rows, 1515)
        self.assertEqual(summary['fullyPassing'], 1)
        self.assertEqual(summary['recorded'], 2)
        self.assertEqual(summary['unrun'], 1513)
        self.assertEqual(summary['harnessStatuses']['TIMEOUT'], 1)


if __name__ == '__main__':
    unittest.main()
