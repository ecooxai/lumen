import copy
import unittest
import importlib.util
from pathlib import Path
_module_path = Path(__file__).resolve().parents[2] / "tools/apple-session-gpt6as/audit_wpt_candidate.py"
_spec = importlib.util.spec_from_file_location("lumen_candidate_audit", _module_path)
_audit = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_audit)
SIGNATURE_FIELDS = _audit.SIGNATURE_FIELDS
audit_rows, compare_cohorts, summarize_rows = _audit.audit_rows, _audit.compare_cohorts, _audit.summarize_rows


def row(name="/a.html", status="OK", subtests=None):
    return {"type": "testharness", "test": name, "url": "http://web-platform.test:8000" + name,
            "status": status, "subtests": [{"name": "first", "status": "PASS"}] if subtests is None else subtests}


class CandidateAuditTests(unittest.TestCase):
    def cohorts(self):
        plan = {"wptRevision": "revision", "fullFrozenScheduleSHA256": "frozen", "fullFrozenVariantCount": 1,
                "frozenSchedule": [{"type": "testharness", "test": "/a.html"}], "groups": {"core": [{"type": "testharness", "test": "/a.html"}]},
                "binary": {"path": "native", "sha256": "a" * 64}}
        cohort = {"plan": plan, "rows": [row()], "signatures": {"core": {field: "fixed" for field in SIGNATURE_FIELDS}},
                  "planSHA256": "plan", "summary": summarize_rows([row()], 1), "evidence": {}}
        return cohort, copy.deepcopy(cohort)

    def test_changed_manifest_rejected(self):
        baseline, candidate = self.cohorts()
        candidate["signatures"]["core"]["inventory_sha256"] = "other"
        with self.assertRaisesRegex(ValueError, "Paired protocol"):
            compare_cohorts(baseline, candidate)

    def test_missing_protocol_field_rejected(self):
        baseline, candidate = self.cohorts()
        del candidate["signatures"]["core"]["timeout_s"]
        with self.assertRaisesRegex(ValueError, "Paired protocol"):
            compare_cohorts(baseline, candidate)

    def test_changed_frozen_denominator_rejected(self):
        baseline, candidate = self.cohorts()
        candidate["plan"]["fullFrozenVariantCount"] = 2
        with self.assertRaisesRegex(ValueError, "Frozen comparison"):
            compare_cohorts(baseline, candidate)

    def test_retains_timeout_error_and_unrun_in_separate_counts(self):
        result = summarize_rows([row(), row("/timeout", "TIMEOUT", []), row("/error", "ERROR", [])], 7)
        self.assertEqual((result["fullyPassing"], result["recorded"], result["unrun"]), (1, 3, 4))
        self.assertEqual(result["harnessStatuses"], {"OK": 1, "TIMEOUT": 1, "ERROR": 1})

    def test_empty_harness_is_not_passing(self):
        self.assertEqual(summarize_rows([row(subtests=[])], 1)["fullyPassing"], 0)

    def test_no_baseline_cannot_claim_no_regression(self):
        self.assertFalse(audit_rows([], [row()])["noRegression"])

    def test_partial_pass_on_timeout_is_not_qualified_pass(self):
        result = summarize_rows([row(status="TIMEOUT")], 1)
        self.assertEqual(result["assertions"], {"PASS": 1})
        self.assertEqual(result["qualifiedPassingAssertions"], 0)

    def test_disappearing_pass_fails_regression_gate(self):
        result = audit_rows([row()], [row(subtests=[])])
        self.assertFalse(result["noRegression"])
        self.assertEqual(result["lostPassingAssertions"][0]["candidateStatus"], "MISSING_ASSERTION")

    def test_deleted_result_remains_explicit(self):
        result = audit_rows([row()], [])
        self.assertEqual(result["missingCandidateVariants"], ["/a.html"])
        self.assertFalse(result["noRegression"])

    def test_timeout_cannot_retain_recorded_pass(self):
        result = audit_rows([row()], [row(status="TIMEOUT")])
        self.assertFalse(result["noRegression"])
        self.assertEqual(result["lostPassingAssertions"][0]["candidateStatus"], "UNQUALIFIED_HARNESS_TIMEOUT")

    def test_cleanup_error_invalidates_passing_row(self):
        candidate = row()
        candidate["cleanup"] = {"profile_error": "failed"}
        self.assertFalse(audit_rows([row()], [candidate])["noRegression"])

    def test_measurement_error_invalidates_passing_row(self):
        candidate = row()
        candidate["metrics"] = {"errors": ["sampling failed"]}
        self.assertEqual(summarize_rows([candidate], 1)["fullyPassing"], 0)

    def test_genuine_fixed_failure_recorded(self):
        baseline = row(subtests=[{"name": "first", "status": "FAIL"}])
        result = audit_rows([baseline], [row()])
        self.assertTrue(result["noRegression"])
        self.assertEqual(len(result["gainedPassingAssertions"]), 1)

    def test_new_coverage_cannot_replace_lost_existing_case(self):
        result = audit_rows([row()], [row("/new.html")])
        self.assertFalse(result["noRegression"])
        self.assertEqual(result["newlyRecordedVariants"], ["/new.html"])

    def test_duplicate_assertion_names_rejected(self):
        candidate = row(subtests=[{"name": "same", "status": "PASS"}, {"name": "same", "status": "PASS"}])
        with self.assertRaisesRegex(ValueError, "Duplicate assertion"):
            audit_rows([row()], [candidate])

    def test_duplicate_test_results_rejected(self):
        with self.assertRaisesRegex(ValueError, "Duplicate variant"):
            audit_rows([row()], [row(), row()])

    def test_actual_url_mismatch_rejected(self):
        candidate = row()
        candidate["url"] = "https://web-platform.test:8443/a.html"
        with self.assertRaisesRegex(ValueError, "Actual URL"):
            audit_rows([row()], [candidate])

    def test_status_message_only_change_does_not_invent_gain(self):
        baseline = row(subtests=[{"name": "first", "status": "FAIL", "message": "before"}])
        candidate = copy.deepcopy(baseline)
        candidate["subtests"][0]["message"] = "after"
        result = audit_rows([baseline], [candidate])
        self.assertEqual(result["gainedPassingAssertions"], [])
        self.assertTrue(result["noRegression"])


if __name__ == "__main__":
    unittest.main()
