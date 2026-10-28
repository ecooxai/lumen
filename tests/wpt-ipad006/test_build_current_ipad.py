"""Execute these provenance guards only on the authorized Mac."""
from copy import deepcopy
import importlib.util
from pathlib import Path
import tempfile
import unittest


spec = importlib.util.spec_from_file_location('ipad006_build', Path(__file__).with_name('build_current_ipad.py'))
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


def clean():
    return {'natural_exit_code': 0, 'ended_by_monitor': False, 'collector_error': None,
            'violations': [], 'owned_group_cleanup': {'error': None, 'sent_sigterm': False, 'sent_sigkill': False}}


class BuildIdentityTests(unittest.TestCase):
    def test_only_clean_natural_build_completion_is_accepted(self):
        build.clean_monitor(clean(), 0)
        for updates in ({'natural_exit_code': None}, {'natural_exit_code': 1},
                        {'ended_by_monitor': True}, {'collector_error': 'ps failed'},
                        {'violations': ['duration exceeded']}):
            changed = clean()
            changed.update(updates)
            with self.subTest(updates=updates), self.assertRaises(ValueError):
                build.clean_monitor(changed, 0)
        with self.assertRaises(ValueError):
            build.clean_monitor(clean(), 1)

    def test_forced_or_unknown_owned_cleanup_cannot_pass(self):
        for updates in ({'sent_sigterm': True}, {'sent_sigkill': True}, {'error': 'survivor'},
                        {'sent_sigterm': None}):
            changed = deepcopy(clean())
            changed['owned_group_cleanup'].update(updates)
            with self.subTest(updates=updates), self.assertRaises(ValueError):
                build.clean_monitor(changed, 0)
        changed = clean()
        del changed['owned_group_cleanup']
        with self.assertRaises(ValueError):
            build.clean_monitor(changed, 0)

    def test_fingerprint_rejects_symlinks_and_external_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory).resolve()
            root = base / 'repo'
            root.mkdir()
            original = root / 'source.cc'
            original.write_text('original')
            self.assertEqual(build.fingerprint(original, root)['bytes'], 8)
            link = root / 'alias.cc'
            link.symlink_to(original)
            with self.assertRaises(ValueError):
                build.fingerprint(link, root)
            external = base / 'outside.cc'
            external.write_text('outside')
            with self.assertRaises(ValueError):
                build.fingerprint(external, root)

    def test_source_freeze_detects_added_removed_and_changed_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            names = ('src/app/main.c', 'Makefile', 'ios/Info.plist', 'ios/prepare-v8-link.py',
                     'tools/prepare-webtransport-link.py', 'assets/icon/lumen-256.png')
            for name in names:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('fixture:' + name)
            before = build.source_manifest(root)
            build.check_source_manifest(root, before)
            new = root / 'src/app/extra.c'
            new.write_text('new input')
            with self.assertRaises(ValueError):
                build.check_source_manifest(root, before)
            new.unlink()
            source = root / 'src/app/main.c'
            source.write_text('different source')
            with self.assertRaises(ValueError):
                build.check_source_manifest(root, before)
            source.unlink()
            with self.assertRaises(ValueError):
                build.check_source_manifest(root, before)

    def test_source_inventory_refuses_linked_source_directories(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            (root / 'src').mkdir()
            (root / 'src/linked').symlink_to(root, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'symlink'):
                build.source_manifest(root)


if __name__ == '__main__':
    unittest.main()
