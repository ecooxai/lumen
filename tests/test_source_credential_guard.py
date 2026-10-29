"""Security regression controls for the source-history publication scanner."""

import importlib.util
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('guard', ROOT / 'tools/apple-session-gpt6as/source_credential_guard.py')
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)


class SourceCredentialGuardTests(unittest.TestCase):
    def test_only_the_two_reviewed_historical_fixture_blobs_are_allowed(self):
        for oid in guard.REVIEWED_FIXTURE_BLOBS:
            with self.subTest(oid=oid):
                data = subprocess.check_output(['git', 'cat-file', 'blob', oid], cwd=ROOT)
                matches = guard.inspect_blob(data, oid, guard.FIXTURE_PATH)
                self.assertEqual(len(matches), 1)
                self.assertEqual(matches[0]['literalBytes'], 27)
                for altered, altered_oid, altered_path in (
                    (data + b'\n# a future edit\n', oid, guard.FIXTURE_PATH),
                    (data, '0' * 40, guard.FIXTURE_PATH),
                    (data, oid, 'tests/a_different_fixture.py'),
                    (data.replace(guard.FIXTURE_LITERAL, guard.FIXTURE_LITERAL + b'\nencoded-key-body'), oid, guard.FIXTURE_PATH),
                ):
                    with self.assertRaisesRegex(RuntimeError, 'publication withheld'):
                        guard.inspect_blob(altered, altered_oid, altered_path)

    def test_all_private_key_variants_remain_rejected(self):
        for key_type in (b'', b'RSA ', b'EC ', b'OPENSSH ', b'DSA '):
            data = b'-----BEGIN ' + key_type + b'PRIVATE KEY-----\n' + b'encoded-body\n' + b'-----END ' + key_type + b'PRIVATE KEY-----'
            with self.subTest(key_type=key_type), self.assertRaises(RuntimeError):
                guard.inspect_blob(data, '0' * 40, 'fixture.pem')

    def test_other_credential_patterns_remain_rejected(self):
        tokens = [prefix + b'A' * 40 for prefix in (b'ghp_', b'gho_', b'github_pat_', b'sk-proj-')]
        tokens.append(b'AKIA' + b'A' * 16)
        for token in tokens:
            with self.subTest(prefix=token[:4]), self.assertRaises(RuntimeError):
                guard.inspect_blob(token, '0' * 40, 'fixture.txt')

    def test_exact_private_value_is_checked_even_in_an_allowed_fixture(self):
        for oid in guard.REVIEWED_FIXTURE_BLOBS:
            data = subprocess.check_output(['git', 'cat-file', 'blob', oid], cwd=ROOT)
            private_value = b'test_ca_bundle_must_not_accidentally_publish_a_private_key'
            with self.assertRaisesRegex(RuntimeError, 'Private message key'):
                guard.inspect_blob(data, oid, guard.FIXTURE_PATH, [private_value])

    def test_plain_source_remains_publishable(self):
        self.assertEqual(guard.inspect_blob(b'print("hello")\n', '0' * 40, 'example.py'), [])


if __name__ == '__main__':
    unittest.main()
