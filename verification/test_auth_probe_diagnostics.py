import json
import unittest
from auth_case import sanitize_probe_diagnostic


class DiagnosticPrivacyTest(unittest.TestCase):
    def test_known_boolean_diagnostic_retains_no_unknown_fields(self):
        item=sanitize_probe_diagnostic('LUME_AUTH_PROBE '+json.dumps({'schema':1,'stage':'sampled','sampleDelayMs':5000,'observerPresent':False,'evaluationException':False,'accountText':'not retained','url':'not retained'}))
        self.assertEqual(item,{'schema':1,'stage':'sampled','sampleDelayMs':5000,'observerPresent':False,'evaluationException':False})

    def test_error_content_cannot_become_a_stage_or_boolean(self):
        self.assertIsNone(sanitize_probe_diagnostic('LUME_AUTH_PROBE '+json.dumps({'schema':1,'stage':'raw provider text'})))
        self.assertIsNone(sanitize_probe_diagnostic('LUME_AUTH_PROBE '+json.dumps({'schema':1,'stage':'connect','helperFailed':'raw error'})))

    def test_only_fixed_sample_times_are_allowed(self):
        self.assertIsNone(sanitize_probe_diagnostic('LUME_AUTH_PROBE '+json.dumps({'schema':1,'stage':'sampled','sampleDelayMs':'secret'})))


if __name__=='__main__':unittest.main()
