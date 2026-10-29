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
    def setUp(self):
        # These tests supply synthetic ps snapshots; host namespace validation
        # is covered separately below and must not alter the mocked process IDs.
        self.namespace = mock.patch.object(monitor, 'pid_namespace_error', return_value=None)
        self.namespace.start()
        self.addCleanup(self.namespace.stop)
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
        # The interval ending at t=10 began at t=1 and crosses the settling
        # boundary. Only intervals entirely within the steady window qualify.
        self.assertEqual(result['steady_interval_cpu_samples'], 1)
        self.assertEqual(result['steady_interval_cpu_mean_percent'], 0)
        self.assertEqual(result['steady_interval_cpu_p95_percent'], 0)
        self.assertEqual(result['rss_steady_median_mb'], 3.5)
        self.assertEqual(result['rss_steady_p95_mb'], 4)
        self.assertIsNone(monitor.summarize([], 0)['interval_cpu_mean_percent'])

    def test_interval_gate_catches_repeated_busy_seconds_hidden_by_average(self):
        rows = [{'elapsed_s': t, 'cpu_percent': 0,
                 'interval_cpu_percent': None if t == 0 else (12 if t % 5 == 0 else 0),
                 'rss_kb': 1024} for t in range(21)]
        result = monitor.summarize(rows, 0)
        result['completed_full_duration'] = True
        self.assertLess(result['steady_interval_cpu_mean_percent'], 3)
        self.assertEqual(result['steady_interval_cpu_p95_percent'], 12)
        violations = monitor.performance_violations(result, max_steady_interval_cpu=3)
        self.assertTrue(any('interval CPU p95' in value for value in violations))

    def test_short_quiet_window_does_not_pass_interval_cpu_gate(self):
        rows = [{'elapsed_s': t, 'cpu_percent': 0,
                 'interval_cpu_percent': None if t == 0 else 0,
                 'rss_kb': 1024} for t in range(5)]
        result = monitor.summarize(rows, 0)
        result['completed_full_duration'] = True
        violations = monitor.performance_violations(result, max_steady_interval_cpu=3)
        self.assertTrue(any('at least 10' in value for value in violations))

    def test_missing_second_makes_low_cpu_gate_inconclusive(self):
        rows = [{'elapsed_s': t, 'cpu_percent': 0,
                 'interval_cpu_percent': None if t == 0 else 0,
                 'rss_kb': 1024} for t in list(range(6)) + list(range(9, 21))]
        result = monitor.summarize(rows, 0)
        result['completed_full_duration'] = True
        self.assertEqual(result['max_sampling_gap_s'], 4)
        violations = monitor.performance_violations(result, max_steady_interval_cpu=3)
        self.assertTrue(any('sampling gap' in value for value in violations))

    def test_start_and_finish_collection_gaps_remain_visible(self):
        rows = [{'elapsed_s': t, 'cpu_percent': 0,
                 'interval_cpu_percent': None if t == 3 else 0,
                 'rss_kb': 1024} for t in range(3, 21)]
        result = monitor.summarize(rows, 0, monitoring_duration=25)
        result['completed_full_duration'] = True
        self.assertEqual(result['max_sampling_gap_s'], 5)
        self.assertTrue(monitor.performance_violations(result, max_steady_interval_cpu=3))

    def test_complete_quiet_window_passes_interval_cpu_gate(self):
        rows = [{'elapsed_s': t, 'cpu_percent': 80,
                 'interval_cpu_percent': None if t == 0 else 1,
                 'rss_kb': 1024} for t in range(21)]
        result = monitor.summarize(rows, 5)
        result['completed_full_duration'] = True
        self.assertEqual(monitor.performance_violations(result, max_steady_interval_cpu=3), [])

    def test_interval_gate_cli_rejects_process_that_exits_before_requested_window(self):
        snapshots = ['10 1 0.0 1024 S 00:01.00 Thu Oct 22 09:00:00 2026\n', '']
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / 'sample'
            with mock.patch.object(monitor.sys, 'argv', ['monitor_process.py', '--pid', '10', '--duration', '20', '--settle', '0', '--max-steady-interval-cpu', '3', '--output', str(prefix)]), mock.patch.object(monitor.subprocess, 'check_output', side_effect=snapshots), mock.patch.object(monitor.time, 'sleep'), contextlib.redirect_stdout(io.StringIO()):
                code = monitor.main()
            result = json.loads(prefix.with_suffix('.json').read_text())
        self.assertEqual(code, 1)
        self.assertTrue(any('inconclusive' in value for value in result['violations']))
        self.assertEqual(result['performance_gate']['interval_cpu_statistic'], 'steady p95')

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


class NamespaceTests(unittest.TestCase):
    def test_matching_linux_identity_is_accepted(self):
        with mock.patch.object(monitor.sys, 'platform', 'linux'), mock.patch.object(monitor.os, 'getpid', return_value=2468), mock.patch.object(monitor.os, 'readlink', return_value='2468'):
            self.assertIsNone(monitor.pid_namespace_error())

    def test_mismatched_identity_rejected_before_launch_output_or_signal(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory) / 'not-created' / 'sample'
            with mock.patch.object(monitor.sys, 'platform', 'linux'), mock.patch.object(monitor.os, 'getpid', return_value=6), mock.patch.object(monitor.os, 'readlink', return_value='15666'), mock.patch.object(monitor.sys, 'argv', ['monitor_process.py', '--output', str(prefix), '--', 'fixture-command']), mock.patch.object(monitor.subprocess, 'Popen') as launch, mock.patch.object(monitor.subprocess, 'check_output') as inspect, mock.patch.object(monitor.os, 'killpg') as signal, contextlib.redirect_stderr(io.StringIO()) as error:
                with self.assertRaises(SystemExit) as raised:
                    monitor.main()
            self.assertEqual(raised.exception.code, 2)
            self.assertIn('PID namespace mismatch', error.getvalue())
            self.assertFalse(prefix.parent.exists())
            launch.assert_not_called()
            inspect.assert_not_called()
            signal.assert_not_called()

    def test_unreadable_linux_identity_is_not_assumed_safe(self):
        with mock.patch.object(monitor.sys, 'platform', 'linux'), mock.patch.object(monitor.os, 'readlink', side_effect=OSError('missing proc')):
            self.assertIn('cannot verify', monitor.pid_namespace_error())

    def test_macos_does_not_require_proc_mount(self):
        with mock.patch.object(monitor.sys, 'platform', 'darwin'), mock.patch.object(monitor.os, 'readlink') as readlink:
            self.assertIsNone(monitor.pid_namespace_error())
            readlink.assert_not_called()


if __name__ == '__main__':
    unittest.main()
