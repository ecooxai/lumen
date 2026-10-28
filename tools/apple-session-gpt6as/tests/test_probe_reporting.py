"""Use actual web-emitter output to verify native console report recovery."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('probe_existing_release', Path(__file__).resolve().parents[1] / 'probe_existing_release.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class NativeReportRecoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        program = '''
const {diagnosticConsoleLines}=await import(process.argv[1]);
const report={exportedAt:'2026-10-28T00:00:00.000Z',context:{padding:'x'.repeat(7000)},results:[{id:'wasm',state:'pass',evidence:'42 → verified',durationMs:1}],summary:{pass:1,total:1}};
process.stdout.write(JSON.stringify({report,lines:diagnosticConsoleLines(report)}));
'''
        result = subprocess.run(['node', '--input-type=module', '--eval', program,
                                 (ROOT / 'tools/apple-web-lab-gpt6as/public/model.js').as_uri()],
                                capture_output=True, text=True, check=True)
        cls.fixture = json.loads(result.stdout)

    def read_lines(self, lines):
        with tempfile.TemporaryDirectory(prefix='lumen-report-protocol-') as directory:
            root = Path(directory)
            source = root / 'console.raw'
            source.write_text('\n'.join(lines) + '\n')
            with source.open('rb') as stream:
                reader = probe.BoundedLog(stream, root / 'console.log')
                reader.thread.join(timeout=3)
                self.assertFalse(reader.thread.is_alive())
            return reader

    def test_actual_emitter_round_trip_survives_large_context_and_out_of_order_chunks(self):
        reader = self.read_lines(list(reversed(self.fixture['lines'])) + ['LUME_LAB_CLEANUP {"activeResourceCount":0}'])
        self.assertIsNone(reader.error)
        report = next(row for row in reader.reports if row['marker'] == 'LUME_LAB_REPORT')
        self.assertEqual(report['value'], self.fixture['report'])
        self.assertEqual(report['transport'], 'chunked-console')
        self.assertTrue(reader.cleanup_seen)

    def test_missing_chunk_is_not_an_implicit_report_pass(self):
        lines = list(self.fixture['lines'])
        lines.pop(next(i for i, line in enumerate(lines) if line.startswith('LUME_LAB_REPORT_CHUNK ')))
        reader = self.read_lines(lines)
        self.assertIn('incomplete', reader.error)
        self.assertFalse(any(row['marker'] == 'LUME_LAB_REPORT' for row in reader.reports))

    def test_conflicting_chunk_and_nonzero_cleanup_are_rejected(self):
        prefix = 'LUME_LAB_REPORT_CHUNK '
        original = next(line for line in self.fixture['lines'] if line.startswith(prefix))
        chunk = json.loads(original[len(prefix):])
        chunk['data'] = 'conflicting content'
        reader = self.read_lines([original, prefix + json.dumps(chunk), 'LUME_LAB_CLEANUP {"activeResourceCount":1}'])
        self.assertIn('conflicting', reader.error)
        self.assertFalse(reader.cleanup_seen)


if __name__ == '__main__':
    unittest.main()
