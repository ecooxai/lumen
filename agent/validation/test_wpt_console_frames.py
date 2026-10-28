import json
import unittest
from wpt_console_frames import parse_console_report

class ConsoleFrames(unittest.TestCase):
    def report(self, records, expected=None):
        header = {'status': 'OK', 'message': None}
        if expected is not None: header['reported_subtests_expected'] = expected
        return '\n'.join(['prefix ASTRAWPT-H '+json.dumps(header), *['prefix ASTRAWPT-S '+json.dumps(r,ensure_ascii=False) for r in records], 'prefix ASTRAWPT-E', ''])
    def test_unicode_separators_are_data(self):
        rows=[[s,'PASS',None] for s in ['line\u2028separator','paragraph\u2029separator','NEL\u0085separator','name ASTRAWPT-H marker']]
        raw=self.report(rows,4)
        self.assertGreater(len(raw.splitlines()),len(raw.split('\n')))
        result=parse_console_report(raw)
        self.assertEqual(result['status'],'OK');self.assertEqual(result['subtests'],rows)
        self.assertEqual(result['collector']['completion_vector_count'],4)
    def test_malformed_record_is_not_silently_dropped(self):
        result=parse_console_report(self.report([['valid','PASS',None]],2).replace('prefix ASTRAWPT-E','prefix ASTRAWPT-S [broken\nprefix ASTRAWPT-E'))
        self.assertEqual(result['status'],'COLLECTOR_ERROR');self.assertEqual(len(result['subtests']),1)
    def test_incomplete_report_is_not_a_pass(self):
        self.assertIsNone(parse_console_report('prefix ASTRAWPT-H {"status":"OK"}\nprefix ASTRAWPT-S ["x","PASS",null]\n'))
    def test_count_mismatch_is_reported(self):
        self.assertEqual(parse_console_report(self.report([['x','PASS',None]],2))['status'],'COLLECTOR_ERROR')
    def test_historical_header_preserves_unknown_count(self):
        result=parse_console_report(self.report([['x','FAIL','reason']]))
        self.assertEqual(result['status'],'OK');self.assertFalse(result['collector']['completion_vector_count_available'])
    def test_duplicate_header_is_rejected(self):
        self.assertEqual(parse_console_report('ASTRAWPT-H {"status":"OK"}\n'+self.report([]))['status'],'COLLECTOR_ERROR')
if __name__=='__main__':unittest.main()
