import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('full_runner', Path(__file__).with_name('full_runner.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class SamplerIntegrationTests(unittest.TestCase):
    def sampler(self):
        value = object.__new__(runner.Sampler)
        value.pid = 10
        value.mod = runner.monitor_module()
        value.rows, value.errors = [], []
        value.totals = runner.SampleTotals()
        value.root_identity = None
        value.observed_identities = {}
        value.previous_snapshot, value.previous_at = None, None
        value.stop_reason = None
        return value

    def test_actual_shared_monitor_produces_interval_fields_in_runner_rows(self):
        sampler = self.sampler()
        first = sampler._record_snapshot('10 1 95.0 1024 S 00:10.00 Thu Oct 22 09:00:00 2026\n', 0)
        second = sampler._record_snapshot('10 1 85.0 2048 S 00:10.02 Thu Oct 22 09:00:00 2026\n', 1)
        self.assertIsNone(first['interval_cpu_percent'])
        self.assertEqual(second['interval_cpu_percent'], 2)
        self.assertEqual(second['cpu_percent'], 85)
        result = sampler.mod.summarize(sampler.rows, 0)
        self.assertEqual(result['interval_cpu_samples'], 1)
        self.assertEqual(result['steady_interval_cpu_mean_percent'], 2)

    def test_runner_rejects_reused_root_before_sampling_unrelated_resource_use(self):
        sampler = self.sampler()
        sampler._record_snapshot('10 1 1.0 1024 S 00:01.00 Thu Oct 22 09:00:00 2026\n', 0)
        row = sampler._record_snapshot('10 1 99.0 999999 R 00:20.00 Thu Oct 22 09:00:01 2026\n', 1)
        self.assertIsNone(row)
        self.assertEqual(len(sampler.rows), 1)
        self.assertEqual(sampler.stop_reason, 'root_identity_changed')
        self.assertEqual(sampler.observed_identities[10], 'Thu Oct 22 09:00:00 2026')

    def test_runner_tracks_helper_identity_for_safe_group_cleanup(self):
        sampler = self.sampler()
        sampler._record_snapshot('10 1 1.0 1024 S 00:01.00 Thu Oct 22 09:00:00 2026\n11 10 2.0 2048 S 00:00.50 Thu Oct 22 09:00:01 2026\n20 1 99.0 9999 R 01:00.00 Thu Oct 22 08:00:00 2026\n', 0)
        self.assertEqual(set(sampler.observed_identities), {10, 11})
        self.assertEqual(sampler.rows[0]['rss_kb'], 3072)

    def test_cleanup_failure_is_evidence_and_never_raises_over_test_result(self):
        cleanup = mock.Mock(side_effect=PermissionError('not permitted'))
        sampler = SimpleNamespace(mod=SimpleNamespace(cleanup_owned_group=cleanup),
                                  observed_identities={10: 'same-start'})
        process = object()
        result = runner.terminate_owned(process, sampler)
        cleanup.assert_called_once_with(process, {10: 'same-start'})
        self.assertIn('PermissionError', result['error'])
        self.assertFalse(result['sent_sigkill'])


if __name__ == '__main__':
    unittest.main()
