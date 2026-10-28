"""Accept idle performance only when the complete measured window is healthy."""
import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / 'tools/apple-session-gpt6as/remote_wasm_idle.py'
spec = importlib.util.spec_from_file_location('apple_wasm_idle', SCRIPT)
idle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(idle)


class IdleGateTests(unittest.TestCase):
    def healthy(self):
        return {'groups': {'lumen': {'idle': {
            'observed_samples': 75, 'duration_seconds': 74,
            'cpu_status': 'within_threshold', 'cpu_p95_percent': 1.0}}},
            'errors': [], 'sampling_gaps_over_1_5_seconds': []}

    def test_complete_healthy_idle_window_passes(self):
        self.assertEqual(idle.idle_validation_issues(self.healthy()), [])

    def test_busy_p95_never_passes_even_if_a_status_is_stale(self):
        summary = self.healthy()
        summary['groups']['lumen']['idle']['cpu_p95_percent'] = 12
        self.assertTrue(any('CPU p95' in value for value in idle.idle_validation_issues(summary)))

    def test_failed_or_unverified_cpu_status_remains_failure(self):
        for status in ('over_threshold', 'unverified', None):
            with self.subTest(status=status):
                summary = self.healthy()
                summary['groups']['lumen']['idle']['cpu_status'] = status
                self.assertTrue(idle.idle_validation_issues(summary))

    def test_short_measurement_is_not_a_performance_pass(self):
        for key, value in (('observed_samples', 59), ('duration_seconds', 20)):
            with self.subTest(key=key):
                summary = self.healthy()
                summary['groups']['lumen']['idle'][key] = value
                self.assertTrue(idle.idle_validation_issues(summary))

    def test_collection_errors_and_gaps_prevent_a_pass(self):
        for field in ('errors', 'sampling_gaps_over_1_5_seconds'):
            summary = self.healthy()
            summary[field] = ['collector stalled']
            self.assertTrue(idle.idle_validation_issues(summary))

    def test_missing_and_nonfinite_measurements_are_unverified(self):
        self.assertTrue(idle.idle_validation_issues({}))
        for value in (None, float('nan'), float('inf'), -1, True):
            with self.subTest(value=value):
                summary = self.healthy()
                summary['groups']['lumen']['idle']['cpu_p95_percent'] = value
                self.assertTrue(idle.idle_validation_issues(summary))


if __name__ == '__main__':
    unittest.main()
