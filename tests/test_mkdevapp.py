#!/usr/bin/env python3
"""Provenance regressions for selecting an immutable, tested macOS executable."""
from contextlib import redirect_stdout, redirect_stderr
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location('lumen_mkdevapp', TOOLS / 'mkdevapp.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ImmutableExecutableTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='lumen-mkdevapp-')
        self.addCleanup(self.temporary.cleanup)
        self.root = (Path(self.temporary.name) / 'project').resolve()
        (self.root / 'tools').mkdir(parents=True)
        (self.root / 'build').mkdir()
        self.default = self.root / 'build/lumen'
        self.default.write_bytes(b'mutable default executable')
        self.candidate = self.root / 'build/lumen-tested-candidate'
        self.candidate.write_bytes(b'previously tested immutable executable')
        self.output = self.root / 'out/Candidate.app'
        self.inspected = []

    @staticmethod
    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def inspect(self, root, executable):
        self.inspected.append(executable)
        return {'maximumDeclaredMinimum': '26.0',
                'files': [{'path': str(executable), 'sha256': self.digest(executable)}]}

    def run_packager(self, *arguments, inspect=None):
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch.object(MODULE, '__file__', str(self.root / 'tools/mkdevapp.py')), \
             patch.object(MODULE.sys, 'platform', 'darwin'), \
             patch.object(MODULE.sys, 'argv', ['mkdevapp.py', '--output', str(self.output), *arguments]), \
             patch.object(MODULE, 'inspect_closure', side_effect=inspect or self.inspect), \
             patch.object(MODULE.subprocess, 'run') as commands, \
             redirect_stdout(stdout), redirect_stderr(stderr):
            MODULE.main()
        return json.loads(stdout.getvalue()), commands.call_args_list

    def test_explicit_candidate_is_copied_and_reported_instead_of_mutable_default(self):
        expected = self.digest(self.candidate)
        result, commands = self.run_packager(
            '--binary', 'build/lumen-tested-candidate', '--expected-binary-sha256', expected)
        self.assertEqual((self.output / 'Contents/MacOS/lumen').read_bytes(), self.candidate.read_bytes())
        self.assertEqual(result['sourceExecutable'], str(self.candidate))
        self.assertEqual(result['binaryBeforeBundleSigningSha256'], expected)
        self.assertTrue(result['expectedBinarySha256Verified'])
        self.assertEqual(self.inspected[0], self.candidate)
        self.assertEqual(len(commands), 2)

    def test_default_remains_supported_and_has_explicit_provenance(self):
        result, _ = self.run_packager()
        self.assertEqual((self.output / 'Contents/MacOS/lumen').read_bytes(), self.default.read_bytes())
        self.assertEqual(result['sourceExecutable'], str(self.default))
        self.assertFalse(result['expectedBinarySha256Verified'])

    def test_mismatched_tested_hash_rejected_before_output_or_dependency_inspection(self):
        with self.assertRaises(SystemExit):
            self.run_packager('--binary', str(self.candidate), '--expected-binary-sha256', '0' * 64)
        self.assertFalse(self.output.exists())
        self.assertEqual(self.inspected, [])

    def test_malformed_expected_hash_is_rejected_before_output(self):
        with self.assertRaises(SystemExit):
            self.run_packager('--expected-binary-sha256', 'not-a-sha256')
        self.assertFalse(self.output.exists())
        self.assertEqual(self.inspected, [])

    def test_external_executable_is_rejected_even_with_matching_hash(self):
        external = Path(self.temporary.name) / 'external-lumen'
        external.write_bytes(b'external executable')
        with self.assertRaises(SystemExit):
            self.run_packager('--binary', str(external), '--expected-binary-sha256', self.digest(external))
        self.assertFalse(self.output.exists())
        self.assertEqual(self.inspected, [])

    def test_symlink_cannot_escape_the_project(self):
        external = Path(self.temporary.name) / 'external-lumen'
        external.write_bytes(b'external executable')
        link = self.root / 'build/escape'
        link.symlink_to(external)
        with self.assertRaises(SystemExit):
            self.run_packager('--binary', str(link), '--expected-binary-sha256', self.digest(external))
        self.assertFalse(self.output.exists())
        self.assertEqual(self.inspected, [])

    def test_executable_changed_during_dependency_inspection_is_not_packaged(self):
        expected = self.digest(self.candidate)
        def race(root, executable):
            closure = self.inspect(root, executable)
            if executable == self.candidate:
                executable.write_bytes(b'a different build replaced the tested candidate')
            return closure
        with self.assertRaisesRegex(RuntimeError, 'copy verification|changed during packaging'):
            self.run_packager('--binary', str(self.candidate), '--expected-binary-sha256', expected,
                              inspect=race)


class PortableResourceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.v8 = self.root / 'v8'
        self.v8.mkdir()
        (self.v8 / 'icudtl.dat').write_bytes(b'fixture ICU data')
        self.ca = self.root / 'ca.pem'
        self.ca.write_bytes(b'-----BEGIN CERTIFICATE-----\nfixture\n-----END CERTIFICATE-----\n')
        self.closure = {'files': [{'path': str(self.v8 / 'libv8.dylib')}]}

    def test_icu_resource_comes_from_the_inspected_v8_directory(self):
        resources = MODULE.portable_resources(self.closure, self.ca)
        self.assertEqual(resources['icudtl.dat'], (self.v8 / 'icudtl.dat').resolve())
        self.assertEqual(resources['cacert.pem'], self.ca.resolve())

    def test_missing_or_ambiguous_icu_data_is_rejected(self):
        (self.v8 / 'icudtl.dat').unlink()
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            MODULE.portable_resources(self.closure, self.ca)
        (self.v8 / 'icudtl.dat').write_bytes(b'fixture')
        other = self.root / 'different-v8'
        other.mkdir(); (other / 'icudtl.dat').write_bytes(b'incompatible fixture')
        self.closure['files'].append({'path': str(other / 'libv8_libbase.dylib')})
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            MODULE.portable_resources(self.closure, self.ca)

    def test_ca_bundle_must_not_accidentally_publish_a_private_key(self):
        for data in [b'no certificate resource', self.ca.read_bytes() + b'-----BEGIN PRIVATE KEY-----']:
            with self.subTest(data=data):
                self.ca.write_bytes(data)
                with self.assertRaisesRegex(ValueError, 'certificates and no private keys'):
                    MODULE.portable_resources(self.closure, self.ca)


if __name__ == '__main__':
    unittest.main()
