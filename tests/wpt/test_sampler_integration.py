import importlib.util
from pathlib import Path
from types import SimpleNamespace
import tempfile
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
        value.root_cpu_identity = None
        value.observed_identities = {}
        value.previous_snapshot, value.previous_at = None, None
        value.stop_reason = None
        # Existing parser tests provide BSD-style ps fixtures. Precision-specific
        # integration below installs controlled Linux kernel observations.
        patch = mock.patch.object(value.mod, 'precise_linux_cpu_times', side_effect=lambda p: p)
        patch.start()
        self.addCleanup(patch.stop)
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

    def test_runner_uses_precise_counter_when_linux_ps_only_has_whole_seconds(self):
        sampler = self.sampler()
        raw = '10 1 0.0 1024 S 00:01 Thu Oct 22 09:00:00 2026\n'
        before = sampler.mod.process_snapshot(raw, 10)
        before[10].update(cpu_time_s=1.12, cpu_start_identity=99)
        after = {10: dict(before[10], cpu_time_s=1.21)}
        sampler.mod.precise_linux_cpu_times.side_effect = [before, after]
        sampler._record_snapshot(raw, 0)
        second = sampler._record_snapshot(raw, 1)
        self.assertEqual(second['interval_cpu_percent'], 9)
        self.assertEqual(sampler.root_cpu_identity, 99)

    def test_runner_detects_same_second_pid_reuse_and_refuses_cleanup(self):
        sampler = self.sampler()
        raw = '10 1 0.0 1024 S 00:01 Thu Oct 22 09:00:00 2026\n'
        before = sampler.mod.process_snapshot(raw, 10)
        before[10].update(cpu_time_s=1.12, cpu_start_identity=99)
        after = {10: dict(before[10], cpu_time_s=0.01, cpu_start_identity=100)}
        sampler.mod.precise_linux_cpu_times.side_effect = [before, after]
        sampler._record_snapshot(raw, 0)
        self.assertIsNone(sampler._record_snapshot(raw, 1))
        self.assertEqual(len(sampler.rows), 1)
        self.assertEqual(sampler.stop_reason, 'root_identity_changed')
        with mock.patch.object(sampler.mod, 'cleanup_owned_group') as cleanup:
            result = runner.terminate_owned(object(), sampler)
        cleanup.assert_not_called()
        self.assertIn('refusing', result['error'])

    def test_precision_errors_propagate_to_collector_failure_handler(self):
        sampler = self.sampler()
        sampler.mod.precise_linux_cpu_times.side_effect = PermissionError('proc unavailable')
        with self.assertRaises(PermissionError):
            sampler._record_snapshot('10 1 0.0 1024 S 00:01 Thu Oct 22 09:00:00 2026\n', 0)
        self.assertFalse(sampler.rows)

    def test_all_native_launchers_reject_namespace_mismatch_before_popen(self):
        module = SimpleNamespace(pid_namespace_error=lambda: 'PID namespace mismatch')
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / 'uncreated' / 'case'
            with mock.patch.object(runner, 'monitor_module', return_value=module), mock.patch.object(runner.subprocess, 'Popen') as launch:
                with self.assertRaisesRegex(RuntimeError, 'PID namespace mismatch'):
                    runner.run_lumen(Path('unused-browser'), 'http://localhost/', 1, prefix, '')
                with self.assertRaisesRegex(RuntimeError, 'PID namespace mismatch'):
                    runner.Chromium(Path('unused-chrome'), prefix, [])
            launch.assert_not_called()
            self.assertFalse(prefix.parent.exists())


if __name__ == '__main__':
    unittest.main()
