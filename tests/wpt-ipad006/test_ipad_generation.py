"""Actual-build provenance guards; execute only on the authorized Mac."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location('isolated_ipad006_generation',
                                            Path(__file__).with_name('ipad_generation.py'))
generation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generation)


def receipt():
    source = {'src/fixture.cc': {'sha256': '1' * 64}}
    entries = {'Lumen': {'kind': 'file', 'sha256': generation.BINARY_SHA}}
    return {'status': 'current_source_signed_simulator_build_verified', 'device': generation.DEVICE,
            'bundle_id': generation.BUNDLE, 'generation': generation.GENERATION,
            'build_output': generation.BUILD, 'helper_sha256': generation.BUILD_HELPER_SHA,
            'monitor_sha256': generation.MONITOR_SHA, 'source_manifest': source,
            'source_manifest_sha256': generation.canonical(source),
            'build_monitor': generation.BUILD_REPORT, 'source_signature_verified': True,
            'source_inputs_unchanged': True, 'old_source_and_evidence_unchanged': True,
            'installed': False, 'runtime_tests_executed': False, 'finished_unix': 1792701490,
            'build_command': ['make', '-j3', 'PLAT=sim', 'BUILD=' + generation.BUILD, 'app'],
            'native_snapshot': {'sha256': generation.NATIVE_SHA},
            'signed_source_bundle': {'app': str(generation.ROOT / generation.APP),
                'executable_sha256': generation.BINARY_SHA,
                'bundle_contents_sha256': generation.canonical(entries), 'entries': entries},
            'monitor_return_code': 0,
            'build_measurement': {'natural_exit_code': 0, 'ended_by_monitor': False,
                'collector_error': None, 'violations': [],
                'owned_group_cleanup': {'error': None, 'sent_sigterm': False, 'sent_sigkill': False}}}


def validate(record):
    original = receipt()
    with mock.patch.multiple(generation, SOURCE_MANIFEST_SHA=original['source_manifest_sha256'],
                             BUNDLE_SHA=original['signed_source_bundle']['bundle_contents_sha256']):
        generation.validate_build_receipt(record)


class Ipad006GenerationTests(unittest.TestCase):
    def test_closed_exact_build006_receipt_is_accepted(self):
        validate(receipt())

    def test_foreign_device_old_build_or_other_command_cannot_be_adopted(self):
        for key, value in (('device', '590ACCA9-0BA2-4803-970F-EF3DAC515255'),
                           ('bundle_id', 'com.example.other'), ('build_output', 'build-sim'),
                           ('generation', generation.GENERATION.replace('006', '005')),
                           ('build_command', ['make', '-j3', 'PLAT=ios', 'app'])):
            changed = receipt()
            changed[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate(changed)

    def test_source_executable_resource_or_native_generation_changes_are_rejected(self):
        cases = []
        changed = receipt(); changed['source_manifest']['src/extra.cc'] = {'sha256': '2' * 64}; cases.append(changed)
        changed = receipt(); changed['native_snapshot']['sha256'] = '3' * 64; cases.append(changed)
        for key, value in (('app', str(generation.ROOT / 'build-sim/Lumen.app')),
                           ('executable_sha256', '4' * 64), ('bundle_contents_sha256', '5' * 64)):
            changed = receipt(); changed['signed_source_bundle'][key] = value; cases.append(changed)
        changed = receipt(); changed['signed_source_bundle']['entries']['icudtl.dat'] = {'kind': 'file', 'sha256': '6' * 64}; cases.append(changed)
        for changed in cases:
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                validate(changed)

    def test_forced_incomplete_or_failed_build_monitor_cannot_pass(self):
        for key, value in (('natural_exit_code', None), ('natural_exit_code', 1),
                           ('ended_by_monitor', True), ('collector_error', 'ps failed'),
                           ('violations', ['deadline exceeded'])):
            changed = receipt(); changed['build_measurement'][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate(changed)
        for key, value in (('error', 'survivor'), ('sent_sigterm', True), ('sent_sigkill', True), ('sent_sigterm', None)):
            changed = receipt(); changed['build_measurement']['owned_group_cleanup'][key] = value
            with self.subTest(cleanup=key), self.assertRaises(ValueError):
                validate(changed)

    def test_unclosed_unsealed_or_modified_old_evidence_receipt_is_rejected(self):
        for key, value in (('status', 'source_frozen_building'), ('finished_unix', None),
                           ('source_signature_verified', False), ('old_source_and_evidence_unchanged', False),
                           ('source_inputs_unchanged', False), ('installed', True),
                           ('runtime_tests_executed', True), ('error', 'build failed'),
                           ('source_signature_verified', 1)):
            changed = receipt(); changed[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                validate(changed)

    def test_receipt_bytes_and_ordinary_path_are_bound(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'receipt.json'
            value = receipt()
            data = json.dumps(value).encode()
            path.write_bytes(data)
            expected = hashlib.sha256(data).hexdigest()
            self.assertEqual(generation.verified_json(path, expected), value)
            path.write_bytes(data + b' ')
            with self.assertRaises(ValueError):
                generation.verified_json(path, expected)
            alias = path.with_name('alias.json')
            alias.symlink_to(path)
            with self.assertRaises(ValueError):
                generation.verified_json(alias, hashlib.sha256(path.read_bytes()).hexdigest())

    def test_regular_input_changed_during_hash_or_with_wrong_bytes_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'binary'
            path.write_bytes(b'fixture executable')
            expected = hashlib.sha256(path.read_bytes()).hexdigest()
            self.assertEqual(generation.verified_digest(path, expected), expected)
            with self.assertRaises(ValueError):
                generation.verified_digest(path, '0' * 64)
            with mock.patch.object(generation, 'identity', side_effect=[('before',), ('after',)]):
                with self.assertRaises(ValueError):
                    generation.verified_digest(path, expected)


if __name__ == '__main__':
    unittest.main()
