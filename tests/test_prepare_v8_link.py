import importlib.util
import json
from pathlib import Path
import shlex
import sys
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location(
    'prepare_v8_link', Path(__file__).resolve().parents[1] / 'ios/prepare-v8-link.py')
prepare_link = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(prepare_link)

FAKE_TOOL = r'''
import json
from pathlib import Path
import sys
source, target = map(Path, sys.argv[-2:])
with Path(__file__).with_name('calls.jsonl').open('a') as log:
    log.write(json.dumps(sys.argv[1:]) + '\n')
data = source.read_bytes()
if b'FAIL' in data:
    target.write_bytes(b'partial output')
    sys.exit(9)
assert sys.argv[1] == '--redefine-sym=_rust_eh_personality=_lumen_v8_rust_eh_personality'
target.write_bytes(data.replace(b'_rust_eh_personality', b'_lumen_v8_rust_eh_personality'))
'''


class PrepareV8LinkTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='v8 link "$ ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.v8_dir = self.root / 'v8'
        self.v8_out = self.v8_dir / 'out/simulator'
        self.metadata = self.v8_out / 'obj/v8_monolith.ninja'
        self.metadata.parent.mkdir(parents=True)
        self.monolith = self.archive('obj/libv8_monolith.a')
        self.library = self.archive('obj/rust/libstd.rlib')
        self.other = self.archive('local_rustc_sysroot/lib/libstd.rlib')
        self.metadata.write_text(
            'build obj/libv8_monolith.a: alink obj/a.o\n'
            '  rlibs = obj/rust/libstd.rlib local_rustc_sysroot/lib/libstd.rlib\n')
        self.tool = self.v8_dir / prepare_link.TOOL_PATH
        self.tool.parent.mkdir(parents=True)
        self.tool.write_text('#!' + sys.executable + '\n' + FAKE_TOOL)
        self.tool.chmod(0o755)
        self.response = self.root / 'app build/v8 libs.rsp'

    def archive(self, name, extra=b''):
        path = self.v8_out / name
        path.parent.mkdir(parents=True, exist_ok=True)
        # The fake objcopy models the symbol operation; real Mach-O validation
        # belongs to the Mac link gate, not this portable orchestration test.
        path.write_bytes(b'!<arch>\ndef _rust_eh_personality\nref _rust_eh_personality\n' + extra)
        return path.resolve()

    def run_prepare(self, jobs=3):
        return prepare_link.prepare(self.v8_dir, self.v8_out, self.response, jobs)

    def copies(self):
        return [Path(name) for name in shlex.split(self.response.read_text())]

    def calls(self):
        log = self.tool.with_name('calls.jsonl')
        return [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []

    def test_ninja_escapes_continuation_variables_and_literal_filename_quotes(self):
        spaced = self.archive('obj/rust/one file$:.rlib')
        quoted = self.archive('obj/rust/two"quote\\.rlib')
        self.metadata.write_text(
            'root = obj/rust\n'
            'build obj/libv8_monolith.a: alink obj/a.o\n'
            '  rlibs = ${root}/one$ file$$$:.rlib $\n'
            '      obj/rust/two"quote\\.rlib\n')
        expected = [self.monolith, spaced, quoted]
        self.assertEqual(prepare_link.archive_paths(self.metadata, self.v8_out), expected)
        original = {path: path.read_bytes() for path in expected}
        result = self.run_prepare()
        self.assertEqual(result['archives'], 3)
        self.assertEqual(len(self.copies()), 3)
        for path in self.copies():
            self.assertTrue(path.is_absolute())
            self.assertTrue(path.is_relative_to((self.v8_out / 'lumen-link').resolve()))
            self.assertEqual(path.read_bytes().count(b'_lumen_v8_rust_eh_personality'), 2)
        self.assertEqual({path: path.read_bytes() for path in expected}, original)

    def test_declared_closure_keeps_same_basename_archives_and_caches_outputs(self):
        result = self.run_prepare()
        self.assertEqual(result['transformed'], 3)
        self.assertEqual(len(set(self.copies())), 3)
        self.assertEqual(len(self.calls()), 3)
        response_time = self.response.stat().st_mtime_ns
        manifest = self.v8_out / 'lumen-link/manifest.json'
        manifest_time = manifest.stat().st_mtime_ns
        self.assertEqual(self.run_prepare()['transformed'], 0)
        self.assertEqual(len(self.calls()), 3)
        self.assertEqual(self.response.stat().st_mtime_ns, response_time)
        self.assertEqual(manifest.stat().st_mtime_ns, manifest_time)

    def test_archive_tool_and_metadata_changes_invalidate_only_necessary_work(self):
        self.run_prepare()
        old_paths = self.copies()
        self.library.write_bytes(self.library.read_bytes() + b'new code')
        self.assertEqual(self.run_prepare()['transformed'], 1)
        self.assertEqual(self.copies()[0], old_paths[0])
        self.assertNotEqual(self.copies()[1], old_paths[1])
        self.tool.write_text(self.tool.read_text() + '\n# updated tool\n')
        self.assertEqual(self.run_prepare()['transformed'], 3)
        added = self.archive('obj/rust/new dependency.rlib')
        self.metadata.write_text(self.metadata.read_text().rstrip('\n') + ' obj/rust/new$ dependency.rlib\n')
        self.assertEqual(self.run_prepare()['transformed'], 1)
        self.assertEqual(len(self.copies()), 4)
        self.assertEqual(Path(self.calls()[-1][-2]), added)

    def test_missing_or_modified_cached_copy_is_repaired(self):
        self.run_prepare()
        self.copies()[1].unlink()
        self.assertEqual(self.run_prepare()['transformed'], 1)
        self.copies()[2].write_bytes(b'broken cached file')
        self.assertEqual(self.run_prepare()['transformed'], 1)
        self.assertEqual(self.run_prepare()['transformed'], 0)

    def test_failed_update_does_not_publish_partial_response_or_damage_prior_copies(self):
        self.run_prepare()
        response = self.response.read_bytes()
        previous_copies = {path: path.read_bytes() for path in self.copies()}
        self.library.write_bytes(self.library.read_bytes() + b'new code')
        self.other.write_bytes(self.other.read_bytes() + b'FAIL')
        with self.assertRaisesRegex(prepare_link.PreparationError, 'llvm-objcopy failed'):
            self.run_prepare(jobs=1)
        self.assertEqual(self.response.read_bytes(), response)
        self.assertEqual({path: path.read_bytes() for path in previous_copies}, previous_copies)
        archive_dir = self.v8_out / 'lumen-link/archives'
        self.assertTrue(all(path.suffix in ('.a', '.rlib') for path in archive_dir.iterdir()))

    def test_missing_archive_and_non_archive_fail_before_publication(self):
        self.library.unlink()
        with self.assertRaisesRegex(prepare_link.PreparationError, 'cannot read required archive'):
            self.run_prepare()
        self.assertFalse(self.response.exists())
        self.library.write_bytes(b'not an archive')
        with self.assertRaisesRegex(prepare_link.PreparationError, 'not a regular ar archive'):
            self.run_prepare()
        self.assertFalse(self.response.exists())
        self.assertEqual(self.calls(), [])

    def test_unknown_variables_and_omitted_rust_dependencies_are_errors(self):
        for binding, message in [
            ('  rlibs = $unknown/libfoo.rlib\n', 'unknown Ninja variable'),
            ('  rlibs = $@bad.rlib\n', 'unsupported Ninja escape'),
            ('  rlibs = \n', 'no populated rlibs'),
            ('  rlibs = local_rustc_sysroot/lib/libstd.rlib\n', 'missing from rlibs'),
        ]:
            with self.subTest(binding=binding):
                self.metadata.write_text('build obj/libv8_monolith.a: alink obj/rust/libstd.rlib\n' + binding)
                with self.assertRaisesRegex(prepare_link.PreparationError, message):
                    self.run_prepare()
                self.assertFalse(self.response.exists())

    def test_legacy_monolith_only_requires_valid_build_metadata(self):
        self.metadata.write_text('build obj/libv8_monolith.a: alink obj/a.o\n')
        self.assertEqual(self.run_prepare()['archives'], 1)
        self.assertEqual(len(self.copies()), 1)
        for invalid in ('# no target\n', 'build obj/other.a: alink obj/a.o\n',
                        'build obj/libv8_monolith.a: phony\n'):
            with self.subTest(metadata=invalid):
                self.metadata.write_text(invalid)
                with self.assertRaises(prepare_link.PreparationError):
                    self.run_prepare()

    def test_corrupt_cache_manifest_is_rebuilt_without_touching_originals(self):
        self.run_prepare()
        manifest = self.v8_out / 'lumen-link/manifest.json'
        for corrupt in ('{bad json', '[]'):
            with self.subTest(manifest=corrupt):
                manifest.write_text(corrupt)
                self.assertEqual(self.run_prepare()['transformed'], 3)
                self.assertIn(b'def _rust_eh_personality', self.monolith.read_bytes())


if __name__ == '__main__':
    unittest.main()
