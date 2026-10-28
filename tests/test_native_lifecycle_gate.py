"""Incomplete or over-budget native observations must never become a pass."""
import copy
import importlib.util
from pathlib import Path
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'tools/apple-session-gpt6as/remote_native_lifecycle.py'
SPEC = importlib.util.spec_from_file_location('native_lifecycle', SOURCE)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class NativeLifecycleGateTests(unittest.TestCase):
    def setUp(self):
        self.summary = {'groups': {'lumen': {'idle:after': {
            'samples': 50, 'observed_samples': 50, 'duration_seconds': 49,
            'cpu_p95_percent': .5, 'cpu_status': 'within_threshold',
        }}}}

    def issues(self, **changes):
        summary = copy.deepcopy(self.summary)
        summary['groups']['lumen']['idle:after'].update(changes)
        return MODULE.phase_issues(summary, 'idle:after', 45, 3)

    def test_complete_low_cpu_window_passes(self):
        self.assertEqual(self.issues(), [])

    def test_reported_green_cannot_hide_over_budget_cpu(self):
        self.assertTrue(self.issues(cpu_p95_percent=3.001))

    def test_missing_and_nonfinite_cpu_are_unverified(self):
        for value in (None, float('nan'), float('inf'), -1, True):
            with self.subTest(value=value):
                self.assertTrue(self.issues(cpu_p95_percent=value))

    def test_missing_observations_fail_even_with_enough_samples(self):
        self.assertTrue(self.issues(samples=51))

    def test_short_window_fails_even_with_many_samples(self):
        self.assertTrue(self.issues(duration_seconds=44.999))

    def test_incomplete_cpu_verdict_fails(self):
        self.assertTrue(self.issues(cpu_status='unverified'))

    def test_absent_phase_fails(self):
        self.assertTrue(MODULE.phase_issues({}, 'idle:after', 45, 3))


if __name__ == '__main__':
    unittest.main()
