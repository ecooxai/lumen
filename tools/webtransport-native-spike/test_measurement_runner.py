#!/usr/bin/env python3
"""Verify that the experiment observer does not leave its child running."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).with_name('run_measured.py')


class MeasurementLifecycle(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='lumen-wt-observer-')
        self.base = Path(self.directory.name)
        self.runner = self.base / 'run_measured.py'
        shutil.copyfile(SOURCE, self.runner)

    def tearDown(self):
        self.directory.cleanup()

    def report(self, label):
        return json.loads((self.base / 'output' / (label + '.measurement.json')).read_text())

    def assert_exited(self, report):
        self.assertTrue(report['processExited'])
        with self.assertRaises(ProcessLookupError):
            os.kill(report['pid'], 0)

    def test_normal_child_exit_is_preserved(self):
        completed = subprocess.run([sys.executable, str(self.runner), '--label', 'normal', '--', sys.executable, '-c', 'raise SystemExit(0)'], capture_output=True, timeout=8)
        self.assertEqual(completed.returncode, 0)
        report = self.report('normal')
        self.assertEqual(report['exitCode'], 0)
        self.assertFalse(report['timedOut'])
        self.assertFalse(report['observerFailed'])
        self.assert_exited(report)

    def test_timeout_reaps_owned_child(self):
        completed = subprocess.run([sys.executable, str(self.runner), '--label', 'timeout', '--timeout', '0.05', '--', sys.executable, '-c', 'import time; time.sleep(30)'], capture_output=True, timeout=8)
        self.assertEqual(completed.returncode, 1)
        report = self.report('timeout')
        self.assertTrue(report['timedOut'])
        self.assertNotEqual(report['exitCode'], 0)
        self.assert_exited(report)

    def test_observer_failure_reaps_owned_child(self):
        spec = importlib.util.spec_from_file_location('owned_observer_fixture', self.runner)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        arguments = [str(self.runner), '--label', 'observer-failure', '--', sys.executable, '-c', 'import time; time.sleep(30)']
        with patch.object(sys, 'argv', arguments), patch.object(module, 'owned_sample', side_effect=OSError('synthetic monitor failure')), patch('builtins.print'):
            self.assertEqual(module.main(), 1)
        report = self.report('observer-failure')
        self.assertTrue(report['observerFailed'])
        self.assert_exited(report)


if __name__ == '__main__':
    unittest.main()
