"""Pure parsing/counter cases; real Mac telemetry is validated separately."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from monitor_macos_host_10300110 import cpu_percent,parse_memory


class HostTelemetryTests(unittest.TestCase):
    def test_first_and_unchanged_cpu_samples_are_unavailable(self):
        self.assertIsNone(cpu_percent(None,[10,20,30,40]))
        self.assertIsNone(cpu_percent([10,20,30,40],[10,20,30,40]))

    def test_cpu_is_busy_ticks_over_all_logical_capacity(self):
        self.assertEqual(cpu_percent([100,20,1000,0],[120,25,1075,0]),25)

    def test_unsigned_tick_wrap_preserves_the_actual_interval(self):
        self.assertEqual(cpu_percent([2**32-10,20,100,0],[5,25,180,0]),20)
        with self.assertRaisesRegex(ValueError,'reset'):
            cpu_percent([1000,1000,1000,1000],[0,0,0,0])

    def test_free_ram_conversion_keeps_cache_in_used_and_decimal_units(self):
        result=parse_memory('Mach Virtual Memory Statistics: (page size of 16384 bytes)\nPages free: 100.\nPages inactive: 200.\nPages wired down: 300.\nPages occupied by compressor: 40.\n',16384*1000)
        self.assertEqual(result['memory_total_gb'],.016384)
        self.assertEqual(result['memory_free_gb'],.0016384)
        self.assertEqual(result['memory_used_gb'],.0147456)
        self.assertEqual(result['inactive_pages'],200)
        self.assertEqual(result['compressor_resident_pages'],40)

    def test_missing_or_impossible_physical_memory_does_not_become_zero(self):
        for text,total in [('Pages free: 100.\n',16384*1000),
                           ('page size of 16384 bytes\nPages active: 1.\n',16384*1000),
                           ('page size of 16384 bytes\nPages free: 1001.\n',16384*1000)]:
            with self.subTest(text=text),self.assertRaises(ValueError):parse_memory(text,total)


if __name__=='__main__':unittest.main()
