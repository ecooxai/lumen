"""A fresh native pass must never relabel a stale browser candidate as current."""
from pathlib import Path
import json
import tempfile
import unittest
from run_native_webtransport_browser_isolated import candidate_chain, digest


class CandidateChainTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.out = self.root / 'out/native-verification'
        self.out.mkdir(parents=True)
        self.gate = {'evidenceTag': 'native-gate04'}
        self.gate_path = self.out / 'webtransport-native-gate04.json'
        self.gate_path.write_text(json.dumps(self.gate))
        self.native = {'path': str(self.gate_path.relative_to(self.root)),
                       'sha256': digest(self.gate_path), 'evidenceTag': self.gate['evidenceTag']}
        self.report_path = self.out / 'candidate-relink01.json'
        self.report = {'passed': True, 'running': False, 'nativeGate': self.native,
                       'sourceBinarySha256': '11' * 32, 'signedExecutableSha256': '22' * 32}
        self.report_path.write_text(json.dumps(self.report))
        self.manifest = {'nativeGate': self.native,
                         'relinkReport': {'path': str(self.report_path.relative_to(self.root)),
                                          'sha256': digest(self.report_path)},
                         'binaryBeforeBundleSigningSha256': self.report['sourceBinarySha256'],
                         'binarySha256': self.report['signedExecutableSha256']}

    def check(self):
        return candidate_chain(self.root, self.manifest, self.gate_path, self.gate)

    def test_exact_immutable_candidate_chain_passes(self):
        hashes, native, relink = self.check()
        self.assertEqual(native, self.native)
        self.assertEqual(hashes, {relink['path']: relink['sha256']})

    def test_revised_native_gate_cannot_inherit_old_candidate(self):
        self.gate['correctedComponent'] = True
        self.gate_path.write_text(json.dumps(self.gate))
        with self.assertRaisesRegex(RuntimeError, 'exact successful native gate'):
            self.check()

    def test_modified_relink_report_cannot_keep_old_reference(self):
        self.report['passed'] = False
        self.report_path.write_text(json.dumps(self.report))
        with self.assertRaisesRegex(RuntimeError, 'absent or changed'):
            self.check()

    def test_different_unsigned_or_signed_binary_cannot_reuse_relink(self):
        for key in ('binaryBeforeBundleSigningSha256', 'binarySha256'):
            with self.subTest(key=key):
                original = self.manifest[key]
                self.manifest[key] = '33' * 32
                with self.assertRaisesRegex(RuntimeError, 'does not bind'):
                    self.check()
                self.manifest[key] = original

    def test_new_report_hash_does_not_turn_failed_relink_into_pass(self):
        for change in ({'passed': False}, {'running': True}, {'nativeGate': {**self.native, 'evidenceTag': 'native-gate03'}}):
            with self.subTest(change=change):
                report = {**self.report, **change}
                self.report_path.write_text(json.dumps(report))
                self.manifest['relinkReport']['sha256'] = digest(self.report_path)
                with self.assertRaises(RuntimeError):
                    self.check()


if __name__ == '__main__':
    unittest.main()
