import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('telemetry', Path(__file__).with_name('telemetry.py'))
telemetry = importlib.util.module_from_spec(spec)
spec.loader.exec_module(telemetry)


class TelemetryTests(unittest.TestCase):
    def test_mac_ps_duration_days_and_fraction(self):
        self.assertEqual(telemetry.cpu_seconds('01:02.50'), 62.5)
        self.assertEqual(telemetry.cpu_seconds('02:03:04'), 7384)
        self.assertEqual(telemetry.cpu_seconds('1-02:03:04'), 93784)

    def test_ps_parse_does_not_record_arguments(self):
        rows = telemetry.parse_ps('12 1 0.3 2048 0:02.34 /Applications/Lumen.app/Contents/MacOS/Lumen\n13 12 1.2 4096 0:03.50 /Applications/Google Chrome.app/Contents/Frameworks/Google Chrome Helper\n')
        self.assertEqual(rows[13]['name'], 'Google Chrome Helper')
        self.assertEqual(rows[12]['rss_bytes'], 2097152)

    def test_descendants_include_grandchild_but_not_other_simulator(self):
        rows = {1:{'ppid':0}, 10:{'ppid':1}, 11:{'ppid':10}, 12:{'ppid':11}, 21:{'ppid':1}}
        self.assertEqual(telemetry.descendants({10}, rows), {10,11,12})

    def test_task_attribution_survives_reparenting(self):
        previous = {10:{'name':'Lumen','cpu_seconds':2}, 11:{'name':'Helper','cpu_seconds':1}}
        rows = {10:{'name':'Lumen','ppid':1,'cpu_seconds':3}, 11:{'name':'Helper','ppid':1,'cpu_seconds':2}, 20:{'name':'Simulator','ppid':1,'cpu_seconds':100}}
        self.assertEqual(telemetry.owned_pids({10}, previous, rows), {10,11})

    def test_pid_reuse_is_excluded(self):
        previous = {11:{'name':'Helper','cpu_seconds':100}}
        rows = {11:{'name':'Different','ppid':1,'cpu_seconds':1}}
        self.assertEqual(telemetry.owned_pids(set(), previous, rows), set())

    def test_duplicate_root_assignment_rejected(self):
        with self.assertRaises(ValueError):
            telemetry.validate_targets({'groups':[{'name':'Lumen','pids':[10]},{'name':'Chrome','pids':[10]}]})

    def test_summary_flags_missing_samples_and_high_idle(self):
        samples = [{'type':'sample','elapsed_seconds':n,'phase':'idle','groups':{'lumen':{'observed':True,'cpu_percent':9,'rss_bytes':1024,'new_pids':[]}}} for n in range(12)]
        result = telemetry.summarize(samples)
        self.assertEqual(result['groups']['lumen']['idle']['cpu_status'], 'above_threshold')
        self.assertEqual(result['groups']['lumen']['idle']['samples'],12)

    def test_summary_never_passes_short_or_missing_process(self):
        samples=[{'type':'sample','elapsed_seconds':0,'phase':'idle','groups':{'lumen':{'observed':False,'cpu_percent':0,'rss_bytes':0,'new_pids':[]}}}]
        self.assertEqual(telemetry.summarize(samples)['groups']['lumen']['idle']['cpu_status'],'unverified')

    def test_summary_retains_peak_not_only_mean(self):
        samples = [{'type':'sample','elapsed_seconds':n,'phase':'idle','groups':{'lumen':{'observed':True,'cpu_percent':0 if n<19 else 100,'rss_bytes':n*1024,'new_pids':[]}}} for n in range(20)]
        stats=telemetry.summarize(samples)['groups']['lumen']['idle']
        self.assertEqual(stats['cpu_peak_percent'],100)
        self.assertEqual(stats['samples_above_threshold'],1)

    def test_restarts_do_not_create_false_memory_growth_trends(self):
        samples=[{'type':'sample','elapsed_seconds':n*10,'phase':'idle','launch':n+1,'groups':{'lumen':{'observed':True,'cpu_percent':0,'rss_bytes':(n+1)*1024,'new_pids':[]}}}for n in range(2)]
        phases=telemetry.summarize(samples)['groups']['lumen']
        self.assertEqual(set(phases),{'idle:launch-1','idle:launch-2'})
        self.assertEqual(phases['idle:launch-2']['rss_change_mib'],0)


if __name__ == '__main__':
    unittest.main()
