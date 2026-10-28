"""Source-only until root runs these bounded archive/provenance gates on Mac."""
import hashlib
import os
from pathlib import Path
import stat
import tempfile
import unittest

import verify_ipad006_relocation as relocation


class RelocationArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name).resolve()
        self.source = self.root / 'profile'
        self.source.mkdir(mode=0o700)
        for name in ('downloads', 'indexeddb', 'localstorage'):
            (self.source / name).mkdir(mode=0o700)
        self.bytes = {'history.txt': b'http://web-platform.test:8000/example\n',
                      'wpt-ca.pem': b'public fixture CA bytes\n'}
        for name, value in self.bytes.items():
            (self.source / name).write_bytes(value)
        self.expected = relocation.profile_tree(self.source)
        self.hashes = {name: hashlib.sha256(value).hexdigest() for name, value in self.bytes.items()}
        self.destination = self.root / 'archive'

    def tearDown(self):
        # Only this test's TemporaryDirectory; restore owner access to copies.
        for directory, directories, files in os.walk(self.root, followlinks=False):
            os.chmod(directory, 0o700)
            for name in files:
                path = Path(directory) / name
                if not path.is_symlink():
                    os.chmod(path, 0o600)
        self.temp.cleanup()

    def test_archive_retains_all_bytes_empty_directories_and_exact_source_identity(self):
        result = relocation.archive_profile(self.source, self.destination, self.expected, self.hashes)
        self.assertEqual(relocation.profile_tree(self.source), self.expected)
        self.assertEqual(set(result['identity']), set(self.expected))
        self.assertEqual(result['relative_directories'], ['.', 'downloads', 'indexeddb', 'localstorage'])
        for name, value in self.bytes.items():
            self.assertEqual((self.destination / name).read_bytes(), value)
            self.assertEqual(result['files'][name]['sha256'], self.hashes[name])
        self.assertFalse(result['historical_content_digest_available'])
        self.assertIn('First observed after relocation', result['content_provenance'])

    def test_archive_removes_write_bits_without_changing_source_modes(self):
        result = relocation.archive_profile(self.source, self.destination, self.expected, self.hashes)
        self.assertTrue(result['read_only_copy'])
        self.assertTrue(all(value['mode'] & 0o222 == 0 for value in result['identity'].values()))
        self.assertEqual(relocation.profile_tree(self.source), self.expected)

    def test_wrong_observed_digest_refuses_before_archive_creation(self):
        altered = dict(self.hashes, **{'history.txt': '0' * 64})
        with self.assertRaisesRegex(RuntimeError, 'digest changed'):
            relocation.archive_profile(self.source, self.destination, self.expected, altered)
        self.assertFalse(self.destination.exists())

    def test_changed_source_bytes_refuses_even_with_same_length(self):
        path = self.source / 'history.txt'
        path.write_bytes(b'X' * len(self.bytes['history.txt']))
        with self.assertRaises(RuntimeError):
            relocation.archive_profile(self.source, self.destination, self.expected, self.hashes)
        self.assertFalse(self.destination.exists())

    def test_nanosecond_identity_is_exact_above_javascript_integer_precision(self):
        self.assertGreater(self.expected['history.txt']['mtime_ns'], 2 ** 53)
        altered = {name: dict(value) for name, value in self.expected.items()}
        altered['history.txt']['mtime_ns'] += 1
        with self.assertRaisesRegex(RuntimeError, 'metadata changed'):
            relocation.contents(self.source, altered, self.hashes)

    def test_new_file_is_not_silently_omitted(self):
        (self.source / 'unexpected').write_bytes(b'extra')
        with self.assertRaises(RuntimeError):
            relocation.archive_profile(self.source, self.destination, self.expected, self.hashes)
        self.assertFalse(self.destination.exists())

    def test_symlink_source_root_is_rejected(self):
        alias = self.root / 'alias'
        alias.symlink_to(self.source, target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, 'aliased'):
            relocation.archive_profile(alias, self.destination, self.expected, self.hashes)
        self.assertFalse(self.destination.exists())

    def test_symlink_inside_profile_is_rejected_without_following_it(self):
        outside = self.root / 'outside'
        outside.write_bytes(b'not profile evidence')
        (self.source / 'link').symlink_to(outside)
        with self.assertRaisesRegex(RuntimeError, 'symlink'):
            relocation.profile_tree(self.source)

    def test_existing_archive_is_never_overwritten(self):
        self.destination.mkdir()
        marker = self.destination / 'existing'
        marker.write_bytes(b'keep')
        with self.assertRaises(FileExistsError):
            relocation.archive_profile(self.source, self.destination, self.expected, self.hashes)
        self.assertEqual(marker.read_bytes(), b'keep')
        self.assertEqual(relocation.profile_tree(self.source), self.expected)

    def test_changed_archive_is_rejected_by_later_verification(self):
        result = relocation.archive_profile(self.source, self.destination, self.expected, self.hashes)
        path = self.destination / 'history.txt'
        os.chmod(path, stat.S_IMODE(path.stat().st_mode) | 0o200)
        path.write_bytes(b'X' * len(self.bytes['history.txt']))
        with self.assertRaises(RuntimeError):
            relocation.contents(self.destination, result['identity'], self.hashes)


if __name__ == '__main__':
    unittest.main()
