import importlib.util
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('monitor_process', Path(__file__).resolve().parents[1] / 'tools/monitor_process.py')
monitor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(monitor)


class MonitorTests(unittest.TestCase):
    def test_descendants_are_included_without_unrelated_processes(self):
        raw = '10 1 1.0 100 S\n11 10 2.0 200 S\n12 11 3.0 300 R\n20 1 99.0 900 S\n'
        tree = monitor.process_tree(raw, 10)
        self.assertEqual(sum(row[1] for row in tree), 6)
        self.assertEqual(sum(row[2] for row in tree), 600)

    def test_exited_process_is_not_a_zero_memory_sample(self):
        self.assertEqual(monitor.process_tree('10 1 0.0 0 Z\n', 10), [])

    def test_startup_does_not_count_as_steady_load(self):
        rows = [{'elapsed_s': t, 'cpu_percent': cpu, 'rss_kb': rss} for t, cpu, rss in [(0, 90, 1000), (10, 2, 2048), (11, 0, 3072)]]
        result = monitor.summarize(rows, 10)
        self.assertEqual(result['steady_cpu_mean_percent'], 1)
        self.assertEqual(result['rss_peak_mb'], 3)
        self.assertEqual(result['rss_steady_growth_mb'], 1)
        self.assertIsNone(monitor.summarize(rows, 20)['steady_cpu_mean_percent'])

    def test_bsd_cpu_times_include_minutes_hours_and_days(self):
        self.assertAlmostEqual(monitor.cpu_time_seconds('02:03.45'), 123.45)
        self.assertAlmostEqual(monitor.cpu_time_seconds('01:02:03.45'), 3723.45)
        self.assertAlmostEqual(monitor.cpu_time_seconds('1-01:02:03.45'), 90123.45)
        self.assertEqual(monitor.cpu_time_seconds('125:00.00'), 7500)
        for invalid in ('bad', '00:nan', '00:inf'):
            with self.assertRaises(ValueError):
                monitor.cpu_time_seconds(invalid)

    def test_interval_cpu_does_not_inherit_startup_rolling_cpu(self):
        before = monitor.process_snapshot('10 1 95.0 1024 R 00:10.00\n', 10)
        after = monitor.process_snapshot('10 1 85.0 1024 S 00:10.02\n', 10)
        self.assertEqual(monitor.interval_cpu_percent(before, after, 1), 2)
        self.assertIsNone(monitor.interval_cpu_percent(None, after, 1))
        self.assertIsNone(monitor.interval_cpu_percent(before, after, 0))

    def test_interval_cpu_includes_new_helpers_and_excludes_unrelated_cpu(self):
        before = monitor.process_snapshot('10 1 1.0 1024 S 00:01.00\n', 10)
        after = monitor.process_snapshot('10 1 1.0 1024 S 00:01.20\n11 10 20.0 2048 S 00:00.50\n20 1 99.0 4096 R 01:00.00\n', 10)
        self.assertEqual(monitor.interval_cpu_percent(before, after, 2), 35)
        self.assertEqual(sum(p['rss_kb'] for p in after.values()), 3072)

    def test_missing_cpu_time_is_not_reported_as_zero(self):
        before = monitor.process_snapshot('10 1 1.0 1024 S\n', 10)
        after = monitor.process_snapshot('10 1 1.0 1024 S 00:01.20\n', 10)
        self.assertIsNone(monitor.interval_cpu_percent(before, after, 1))
        self.assertIsNone(monitor.interval_cpu_percent(after, before, 1))

    def test_interval_p95_ignores_missing_baseline_and_startup(self):
        rows = [{'elapsed_s': t, 'cpu_percent': 90, 'interval_cpu_percent': cpu,
                 'rss_kb': rss} for t, cpu, rss in [(0, None, 1024), (1, 80, 2048), (10, 2, 3072), (11, 0, 4096)]]
        result = monitor.summarize(rows, 10)
        self.assertEqual(result['interval_cpu_samples'], 3)
        self.assertEqual(result['steady_interval_cpu_samples'], 2)
        self.assertEqual(result['steady_interval_cpu_mean_percent'], 1)
        self.assertEqual(result['steady_interval_cpu_p95_percent'], 2)
        self.assertEqual(result['rss_steady_median_mb'], 3.5)
        self.assertEqual(result['rss_steady_p95_mb'], 4)
        self.assertIsNone(monitor.summarize([], 0)['interval_cpu_mean_percent'])

    def test_reused_pid_with_higher_counter_is_a_new_process(self):
        before = monitor.process_snapshot('10 1 1.0 1024 S 00:01.00 Thu Oct 22 09:00:00 2026\n11 10 1.0 1024 S 00:01.00 Thu Oct 22 09:00:01 2026\n', 10)
        after = monitor.process_snapshot('10 1 1.0 1024 S 00:01.00 Thu Oct 22 09:00:00 2026\n11 10 1.0 1024 S 00:02.00 Thu Oct 22 09:00:02 2026\n', 10)
        self.assertEqual(monitor.interval_cpu_percent(before, after, 1), 200)
        self.assertEqual(after[11]['start_identity'], 'Thu Oct 22 09:00:02 2026')

    def test_owned_group_cleanup_reaches_children_after_root_exit(self):
        process = mock.Mock(pid=10)
        process.poll.return_value = 0
        with mock.patch.object(monitor, 'owned_group_members', side_effect=[{11: 'same-start'}, {}]), mock.patch.object(monitor.os, 'killpg') as kill:
            result = monitor.cleanup_owned_group(process, {11: 'same-start'})
        kill.assert_called_once_with(10, monitor.signal.SIGTERM)
        self.assertTrue(result['sent_sigterm'])
        self.assertIsNone(result['error'])

    def test_reused_or_unproven_process_group_is_never_killed(self):
        process = mock.Mock(pid=10)
        process.poll.return_value = 0
        with mock.patch.object(monitor, 'owned_group_members', return_value={11: 'new-start'}), mock.patch.object(monitor.os, 'killpg') as kill:
            result = monitor.cleanup_owned_group(process, {11: 'old-start'}, grace=0)
        kill.assert_not_called()
        self.assertIsNotNone(result['error'])

    def test_observed_root_reuse_stops_before_unrelated_sample_and_never_kills(self):
        snapshots = ['10 1 1.0 1024 S 00:01.00 Thu Oct 22 09:00:00 2026\n',
                     '10 1 90.0 99999 R 00:20.00 Thu Oct 22 09:00:01 2026\n']
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / 'sample'
            with mock.patch.object(monitor.sys, 'argv', ['monitor_process.py', '--pid', '10', '--duration', '10', '--output', str(prefix)]), mock.patch.object(monitor.subprocess, 'check_output', side_effect=snapshots), mock.patch.object(monitor.time, 'sleep'), mock.patch.object(monitor.os, 'killpg') as kill, contextlib.redirect_stdout(io.StringIO()):
                code = monitor.main()
            result = json.loads(prefix.with_suffix('.json').read_text())
        self.assertEqual(code, 1)
        self.assertEqual(result['samples'], 1)
        self.assertEqual(result['stop_reason'], 'root_identity_changed')
        self.assertFalse(result['completed_full_duration'])
        self.assertIsNone(result['owned_group_cleanup'])
        kill.assert_not_called()

    def test_early_process_exit_does_not_pass_full_duration_performance_gate(self):
        snapshots = ['10 1 1.0 1024 S 00:01.00 Thu Oct 22 09:00:00 2026\n', '']
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / 'sample'
            with mock.patch.object(monitor.sys, 'argv', ['monitor_process.py', '--pid', '10', '--duration', '10', '--settle', '0', '--max-steady-cpu', '3', '--output', str(prefix)]), mock.patch.object(monitor.subprocess, 'check_output', side_effect=snapshots), mock.patch.object(monitor.time, 'sleep'), contextlib.redirect_stdout(io.StringIO()):
                code = monitor.main()
            result = json.loads(prefix.with_suffix('.json').read_text())
        self.assertEqual(code, 1)
        self.assertEqual(result['measurement_status'], 'partial')
        self.assertEqual(result['stop_reason'], 'root_exited')
        self.assertFalse(result['completed_full_duration'])
        self.assertTrue(any('inconclusive' in value for value in result['violations']))

    def test_collector_error_still_writes_failure_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / 'sample'
            with mock.patch.object(monitor.sys, 'argv', ['monitor_process.py', '--pid', '10', '--duration', '10', '--output', str(prefix)]), mock.patch.object(monitor.subprocess, 'check_output', side_effect=OSError('ps unavailable')), contextlib.redirect_stdout(io.StringIO()):
                code = monitor.main()
            result = json.loads(prefix.with_suffix('.json').read_text())
        self.assertEqual(code, 1)
        self.assertEqual(result['measurement_status'], 'failed')
        self.assertEqual(result['stop_reason'], 'collector_error')
        self.assertIn('ps unavailable', result['collector_error'])


if __name__ == '__main__':
    unittest.main()
