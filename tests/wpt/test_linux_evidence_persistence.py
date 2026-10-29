"""Fault regressions for missing rows, competing writers and interrupted writes."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('linux_persistence', Path(__file__).with_name('linux_headless_runner.py'))
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)

class PersistenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.schedule = [{'type': 'testharness', 'test': '/first.html'},
                         {'type': 'testharness', 'test': '/second.html'}]
        self.rows = [{**test, 'sequence': i, 'status': 'OK',
                      'subtests': [{'name': 'actual assertion ' + str(i), 'status': 'PASS'}]}
                     for i, test in enumerate(self.schedule)]

    def retain(self):
        for index, row in enumerate(self.rows):
            adapter.retain_case(self.directory, index, self.schedule[index], row)

    def test_complete_atomic_aggregate_and_hashes_bind_every_actual_case(self):
        self.retain()
        proof = adapter.publish_group(self.directory, self.schedule, self.rows)
        self.assertEqual(proof['recorded'], 2)
        self.assertEqual(proof['resultsJSONLSHA256'], adapter.sha(self.directory / 'results.jsonl'))
        self.assertEqual([json.loads(s) for s in (self.directory / 'results.jsonl').read_text().splitlines()], self.rows)
        for case in proof['cases']:
            self.assertEqual(case['sha256'], adapter.sha(self.directory / case['path']))

    def test_missing_last_case_cannot_be_counted_from_memory(self):
        self.retain()
        adapter.case_path(self.directory, 1, self.schedule[1]).unlink()
        with self.assertRaisesRegex(RuntimeError, 'differ'):
            adapter.publish_group(self.directory, self.schedule, self.rows)
        self.assertFalse((self.directory / 'results-evidence.json').exists())

    def test_truncated_aggregate_is_rebuilt_only_from_real_immutable_cases(self):
        self.retain()
        adapter.publish_group(self.directory, self.schedule, self.rows)
        (self.directory / 'results.jsonl').write_text(json.dumps(self.rows[0]) + '\n')
        retained = adapter.retained_rows(self.directory, self.schedule)
        self.assertEqual(retained, self.rows)
        proof = adapter.publish_group(self.directory, self.schedule, retained)
        self.assertEqual(proof['recorded'], 2)
        self.assertEqual(len((self.directory / 'results.jsonl').read_text().splitlines()), 2)

    def test_conflicting_duplicate_never_replaces_prior_evidence(self):
        self.retain()
        path = adapter.case_path(self.directory, 0, self.schedule[0])
        before = path.read_bytes()
        altered = {**self.rows[0], 'status': 'TIMEOUT'}
        with self.assertRaisesRegex(RuntimeError, 'Conflicting'):
            adapter.retain_case(self.directory, 0, self.schedule[0], altered)
        self.assertEqual(path.read_bytes(), before)

    def test_case_digest_corruption_rejected(self):
        self.retain()
        path = adapter.case_path(self.directory, 0, self.schedule[0])
        value = adapter.read(path)
        value['row']['status'] = 'FAIL'
        path.write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError, 'Malformed'):
            adapter.retained_rows(self.directory, self.schedule)

    def test_foreign_case_rejected_even_with_valid_json(self):
        self.retain()
        (self.directory / 'cases/case-999999-foreign.json').write_text('{}')
        with self.assertRaisesRegex(ValueError, 'Foreign'):
            adapter.retained_rows(self.directory, self.schedule)

    def test_interrupted_atomic_metadata_write_preserves_old_file(self):
        target = self.directory / 'receipt.json'
        adapter.save(target, {'version': 1})
        with mock.patch.object(adapter.os, 'replace', side_effect=OSError('interrupted')):
            with self.assertRaisesRegex(OSError, 'interrupted'):
                adapter.save(target, {'version': 2})
        self.assertEqual(adapter.read(target), {'version': 1})
        self.assertEqual(sorted(p.name for p in self.directory.iterdir()), ['receipt.json'])

    def test_competing_writer_rejected_and_lock_released(self):
        first = adapter.writer_lease(self.directory)
        try:
            with self.assertRaisesRegex(RuntimeError, 'Another invocation'):
                adapter.writer_lease(self.directory)
        finally:
            first.close()
        adapter.writer_lease(self.directory).close()

    def test_finalized_missing_case_rejects_resume_instead_of_rerunning(self):
        self.retain()
        adapter.publish_group(self.directory, self.schedule, self.rows)
        adapter.case_path(self.directory, 1, self.schedule[1]).unlink()
        with self.assertRaisesRegex(ValueError, 'Finalized immutable case'):
            adapter.retained_rows(self.directory, self.schedule)

    def test_finalized_case_cannot_change_even_with_recomputed_row_digest(self):
        self.retain()
        adapter.publish_group(self.directory, self.schedule, self.rows)
        path = adapter.case_path(self.directory, 1, self.schedule[1])
        record = adapter.read(path)
        record['row']['subtests'][0]['status'] = 'FAIL'
        record['rowSHA256'] = adapter.digest(record['row'])
        path.write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, 'Finalized immutable case'):
            adapter.retained_rows(self.directory, self.schedule)

    def test_interrupted_later_invocation_can_retain_real_additional_cases(self):
        adapter.retain_case(self.directory, 0, self.schedule[0], self.rows[0])
        adapter.publish_group(self.directory, self.schedule, self.rows[:1])
        adapter.retain_case(self.directory, 1, self.schedule[1], self.rows[1])
        self.assertEqual(adapter.retained_rows(self.directory, self.schedule), self.rows)
        self.assertEqual(adapter.publish_group(self.directory, self.schedule, self.rows)['recorded'], 2)

    def test_legacy_append_only_rows_are_not_silently_promoted(self):
        (self.directory / 'results.jsonl').write_text(json.dumps(self.rows[0]) + '\n')
        with self.assertRaisesRegex(ValueError, 'Legacy aggregate'):
            adapter.retained_rows(self.directory, self.schedule)

if __name__ == '__main__':
    unittest.main()
