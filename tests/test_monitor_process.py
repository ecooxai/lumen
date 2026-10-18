import importlib.util
from pathlib import Path
import unittest

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

if __name__ == '__main__':
    unittest.main()
