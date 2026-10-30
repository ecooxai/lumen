import unittest
from resource_monitor import parse_cputime, add_cpu_deltas

class ResourceMeasurementTests(unittest.TestCase):
    def test_cpu_time_formats(self):
        self.assertAlmostEqual(parse_cputime('2:03.45'), 123.45)
        self.assertEqual(parse_cputime('1:02:03'), 3723)
        self.assertEqual(parse_cputime('2-01:02:03'), 176523)

    def test_one_core_percentage_from_delta_not_ps_average(self):
        before = {77: {'cpu_seconds': 1.0}}
        rows = [{'pid': 77, 'cpu_seconds': 1.03, 'cpu_percent': 70.0},
                {'pid': 78, 'cpu_seconds': 12.0, 'cpu_percent': 99.0}]
        add_cpu_deltas(rows, before, 1.0)
        self.assertAlmostEqual(rows[0]['cpu_interval_percent'], 3.0)
        self.assertIsNone(rows[1]['cpu_interval_percent'])

if __name__ == '__main__':
    unittest.main()

