"""Synthetic ledger integrity tests; these are not browser pass-rate measurements."""
from pathlib import Path
from tempfile import TemporaryDirectory
from collections import Counter
import hashlib
import json
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/apple-session-gpt6as'))
from candidate_catalog_102900614 import build_candidate_catalog, FROZEN_COUNT, FROZEN_SHA

class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.base = self.root / 'output/session-102900614'
    def save(self, path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value) + '\n')
    def digest(self, path):
        return hashlib.sha256(path.read_bytes()).hexdigest()
    def cohort(self, name='frame', binary='a' * 64):
        path = self.base / ('wpt-' + name + '-native-1')
        schedule = [{'test': '/encoding/test-' + str(i)} for i in range(26)]
        remaining = [{'test': '/other/test-' + str(i)} for i in range(1489)]
        group = path / 'encoding-streams'
        self.save(group / 'schedule.json', schedule)
        schedule_sha = self.digest(group / 'schedule.json')
        plan = {'binary': {'sha256': binary}, 'fullFrozenVariantCount': FROZEN_COUNT, 'fullFrozenScheduleSHA256': FROZEN_SHA, 'groups': {'encoding-streams': schedule, 'remaining': remaining}, 'groupScheduleSHA256': {'encoding-streams': schedule_sha, 'remaining': 'r' * 64}}
        self.save(path / 'cohort.json', plan)
        rows = [{'test': schedule[0]['test'], 'status': 'OK', 'subtests': [{'name': 'x', 'status': 'PASS'}]}, {'test': schedule[1]['test'], 'status': 'OK', 'subtests': [{'name': 'x', 'status': 'FAIL'}]}]
        (group / 'results.jsonl').write_text(''.join(json.dumps(row) + '\n' for row in rows))
        self.save(group / 'run.json', {'signature': {'binary_sha256': binary, 'schedule_sha256': schedule_sha}})
        summary = {'binarySHA256': binary, 'cohortSHA256': self.digest(path / 'cohort.json'), 'updatedUTC': '2026-10-28T23:59:00Z', 'scheduled': 1515, 'run': 2, 'fullyPassing': 1, 'unrun': 1513, 'assertions': {'PASS': 1, 'FAIL': 1}, 'harnessStatuses': {'OK': 2}, 'groups': {'encoding-streams': {'scheduled': 26, 'run': 2, 'fullyPassing': 1, 'unrun': 24}, 'remaining': {'scheduled': 1489, 'run': 0, 'fullyPassing': 0, 'unrun': 1489}}}
        self.save(path / 'priority-summary.json', summary)
        return path, plan, summary, rows
    def catalog(self, reference=None):
        return build_candidate_catalog(self.root, '102900614', '0' * 64, reference)
    def test_frozen_denominator_and_real_failure_survive(self):
        self.cohort()
        result = self.catalog()
        row = result['latestMeasuredCohort']
        self.assertEqual((row['fullyPassing'], row['recorded'], row['scheduled'], row['unrun']), (1, 2, 1515, 1513))
        self.assertEqual(row['assertions'], {'PASS': 1, 'FAIL': 1})
        self.assertIsNone(result['fullOfficialWptPassRate'])
        self.assertFalse(row['acceptedArtifact'])
    def test_two_artifacts_never_merge(self):
        self.cohort('frame', 'a' * 64)
        self.cohort('legacy', 'b' * 64)
        rows = self.catalog()['cohorts']
        self.assertEqual(len(rows), 2)
        self.assertEqual({r['binarySha256'] for r in rows}, {'a' * 64, 'b' * 64})
        self.assertTrue(all(r['recorded'] == 2 and r['scheduled'] == 1515 for r in rows))
    def test_changed_cohort_and_invented_pass_count_rejected(self):
        path, plan, summary, _ = self.cohort()
        summary['fullyPassing'] = 2
        self.save(path / 'priority-summary.json', summary)
        self.assertFalse(self.catalog()['cohorts'])
        self.assertTrue(self.catalog()['validationErrors'])
        summary['fullyPassing'] = 1
        self.save(path / 'priority-summary.json', summary)
        plan['binary']['sha256'] = 'd' * 64
        self.save(path / 'cohort.json', plan)
        self.assertFalse(self.catalog()['cohorts'])
    def test_zero_assertion_ok_file_does_not_pass(self):
        path, _, summary, rows = self.cohort()
        rows[1]['subtests'] = []
        (path / 'encoding-streams/results.jsonl').write_text(''.join(json.dumps(r) + '\n' for r in rows))
        summary['assertions'] = {'PASS': 1}
        self.save(path / 'priority-summary.json', summary)
        self.assertEqual(self.catalog()['cohorts'][0]['fullyPassing'], 1)
    def test_stable_summary_prefix_while_writer_appends(self):
        path, _, _, _ = self.cohort()
        with (path / 'encoding-streams/results.jsonl').open('a') as output:
            output.write('{"incomplete_next_result":')
        self.assertEqual(self.catalog()['cohorts'][0]['recorded'], 2)
    def test_qualified_chrome_raw_failures_remain_failures(self):
        path, plan, _, _ = self.cohort()
        directory = self.base / 'chrome-qualified-streams-1'
        schedule = plan['groups']['encoding-streams']
        qualified = {'binarySHA256': 'c' * 64, 'evidenceSHA256': 'e' * 64}
        identity = {'referenceBinarySHA256': qualified['binarySHA256'], 'referenceQualificationSHA256': qualified['evidenceSHA256'], 'frozenScheduledVariants': 1515, 'nativeCohort': str((path / 'cohort.json').relative_to(self.root)), 'nativeCohortSHA256': self.digest(path / 'cohort.json'), 'nativeBinarySHA256': plan['binary']['sha256'], 'group': 'encoding-streams', 'groupScheduleSHA256': plan['groupScheduleSHA256']['encoding-streams']}
        self.save(directory / 'paired-identity.json', identity)
        self.save(directory / 'schedule.json', schedule)
        self.save(directory / 'run.json', {'signature': {'binary_sha256': qualified['binarySHA256'], 'schedule_sha256': identity['groupScheduleSHA256']}})
        rows = []
        for i, item in enumerate(schedule):
            statuses = ['PASS'] * (13 if i < 23 else 15) + ([] if i < 23 else ['FAIL'] * (4 if i == 25 else 5))
            rows.append({'test': item['test'], 'status': 'OK', 'subtests': [{'name': str(n), 'status': value} for n, value in enumerate(statuses)]})
        (directory / 'results.jsonl').write_text(''.join(json.dumps(row) + '\n' for row in rows))
        self.save(directory / 'paired-invocation-1.json', {'measurementQualified': True, 'passed': True, 'recorded': 26, 'rawFullyPassing': 23, 'resourceErrorCases': []})
        reference = self.catalog(qualified)['qualifiedChrome']
        self.assertEqual((reference['fullyPassing'], reference['recorded']), (23, 26))
        self.assertEqual(reference['assertions'], {'PASS': 344, 'FAIL': 14})
        self.assertEqual(self.catalog(qualified)['cohorts'][0]['fullyPassing'], 1)

if __name__ == '__main__':
    unittest.main()
