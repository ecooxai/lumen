import importlib.util
import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest import mock
import zipfile

spec = importlib.util.spec_from_file_location('adopt_cft156', Path(__file__).with_name('adopt_cft156.py'))
adoption = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adoption)


class ExtractedArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.archive = self.root / 'browser.zip'
        self.extracted = self.root / 'extracted'
        self.extracted.mkdir()
        self.validator = adoption.load_preparer()
        self.entries = [
            ('chrome-mac-arm64/Contents/tool', b'executable', stat.S_IFREG | 0o755),
            ('chrome-mac-arm64/Contents/data', b'resource bytes', stat.S_IFREG | 0o644),
            ('chrome-mac-arm64/link', b'Contents/tool', stat.S_IFLNK | 0o777)]
        with zipfile.ZipFile(self.archive, 'w') as archive:
            for name, content, mode in self.entries:
                info = zipfile.ZipInfo(name)
                info.create_system = 3
                info.external_attr = mode << 16
                archive.writestr(info, content)
                target = self.extracted / name
                target.parent.mkdir(parents=True, exist_ok=True)
                if stat.S_ISLNK(mode):
                    target.symlink_to(content.decode())
                else:
                    target.write_bytes(content)
                    target.chmod(stat.S_IMODE(mode))

    def tearDown(self):
        self.temporary.cleanup()

    def verify(self):
        return adoption.verify_extracted_archive(self.archive, self.extracted, self.validator)

    def test_all_files_links_and_implicit_directories_are_recorded(self):
        result = self.verify()
        self.assertEqual(result['archive_plan']['member_count'], 3)
        self.assertEqual(len(result['entries']), 5)
        self.assertEqual(len(result['tree_sha256']), 64)
        self.assertEqual(result['tree_sha256'], self.verify()['tree_sha256'])

    def test_changed_framework_or_resource_bytes_are_rejected(self):
        path = self.extracted / 'chrome-mac-arm64/Contents/data'
        path.write_bytes(b'changed bytes!')
        with self.assertRaisesRegex(RuntimeError, 'bytes differ'):
            self.verify()

    def test_extra_and_missing_members_are_rejected(self):
        path = self.extracted / 'chrome-mac-arm64/Contents/extra'
        path.write_bytes(b'not in the official archive')
        with self.assertRaisesRegex(RuntimeError, 'missing or additional'):
            self.verify()
        path.unlink()
        (self.extracted / 'chrome-mac-arm64/Contents/data').unlink()
        with self.assertRaisesRegex(RuntimeError, 'missing or additional'):
            self.verify()

    def test_changed_symlink_target_is_rejected_even_if_it_resolves_inside(self):
        path = self.extracted / 'chrome-mac-arm64/link'
        path.unlink()
        path.symlink_to('Contents/data')
        with self.assertRaisesRegex(RuntimeError, 'symlink differs'):
            self.verify()

    def test_regular_member_cannot_be_replaced_with_a_symlink(self):
        path = self.extracted / 'chrome-mac-arm64/Contents/data'
        path.unlink()
        path.symlink_to('tool')
        with self.assertRaisesRegex(RuntimeError, 'type differs'):
            self.verify()

    def test_executable_permissions_are_bound(self):
        (self.extracted / 'chrome-mac-arm64/Contents/tool').chmod(0o644)
        with self.assertRaisesRegex(RuntimeError, 'permissions differ'):
            self.verify()

    def test_hash_file_does_not_follow_an_unexpected_symlink(self):
        with self.assertRaisesRegex(RuntimeError, 'ordinary file'):
            adoption.hash_file(self.extracted / 'chrome-mac-arm64/link')

    def test_tree_changes_during_hashing_cannot_be_silently_accepted(self):
        original = adoption.hash_file
        changed = False

        def mutate_after_hash(path):
            nonlocal changed
            value = original(path)
            if not changed:
                changed = True
                (self.extracted / 'chrome-mac-arm64/new').write_bytes(b'late mutation')
            return value

        with mock.patch.object(adoption, 'hash_file', side_effect=mutate_after_hash):
            with self.assertRaisesRegex(RuntimeError, 'changed while verifying'):
                self.verify()


if __name__ == '__main__':
    unittest.main()
