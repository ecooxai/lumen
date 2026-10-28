"""Provenance gates for generation004; execute only on the authorized Mac."""
from copy import deepcopy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location('isolated_ipad_generation',
                                            Path(__file__).with_name('ipad_generation.py'))
generation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generation)


def receipt():
    record = {'status': 'isolated_ipad_ready_for_signed_install', 'old_device': generation.OLD_DEVICE,
              'new_device': generation.DEVICE, 'new_name': generation.DEVICE_NAME,
              'device_type': generation.DEVICE_TYPE, 'runtime': generation.RUNTIME,
              'runtime_build': generation.RUNTIME_BUILD, 'helper_sha256': generation.CREATE_HELPER_SHA,
              'monitor_sha256': generation.MONITOR_SHA, 'finished_unix': 1792698384.535321,
              'old_device_running_lumen': [], 'runtime_verified':
              {'identifier': generation.RUNTIME, 'build': generation.RUNTIME_BUILD,
               'architecture': 'arm64', 'product_family': 'iPad'}}
    for key, uuid, state in (('old_device_after', generation.OLD_DEVICE, 'Shutdown'),
                            ('new_device_created', generation.DEVICE, 'Shutdown'),
                            ('new_device_ready', generation.DEVICE, 'Booted')):
        record[key] = {'udid': uuid, 'state': state, 'runtime': generation.RUNTIME,
                       'device_type': generation.DEVICE_TYPE, 'name': generation.DEVICE_NAME}
    return record


class IpadGenerationTests(unittest.TestCase):
    def test_successful_exact_creation_receipt_is_accepted(self):
        generation.validate_creation(receipt())

    def test_original_or_foreign_device_cannot_be_adopted(self):
        for uuid in (generation.OLD_DEVICE, '00000000-0000-0000-0000-000000000000'):
            for key in ('new_device', 'new_device_created', 'new_device_ready'):
                altered = receipt()
                if key == 'new_device':
                    altered[key] = uuid
                else:
                    altered[key]['udid'] = uuid
                with self.subTest(uuid=uuid, key=key), self.assertRaises(ValueError):
                    generation.validate_creation(altered)

    def test_runtime_or_device_family_changes_are_rejected(self):
        for key, value in (('runtime', 'com.apple.CoreSimulator.SimRuntime.iOS-27-0'),
                           ('runtime_build', 'different'), ('device_type', 'iPhone')):
            altered = receipt()
            altered[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                generation.validate_creation(altered)
        altered = receipt()
        altered['runtime_verified']['product_family'] = 'iPhone'
        with self.assertRaises(ValueError):
            generation.validate_creation(altered)

    def test_incomplete_failed_or_non_idle_creation_is_rejected(self):
        for change in ({'status': 'running'}, {'error': 'boot timed out'}, {'finished_unix': None},
                       {'old_device_running_lumen': [{'pid': 42}]}):
            altered = receipt()
            altered.update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                generation.validate_creation(altered)
        altered = receipt()
        altered['new_device_ready']['state'] = 'Shutdown'
        with self.assertRaises(ValueError):
            generation.validate_creation(altered)

    def test_receipt_hash_binds_exact_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'receipt.json'
            data = json.dumps(receipt()).encode()
            path.write_bytes(data)
            expected = hashlib.sha256(data).hexdigest()
            self.assertEqual(generation.verified_json(path, expected), receipt())
            path.write_bytes(data + b' ')
            with self.assertRaisesRegex(ValueError, 'digest changed'):
                generation.verified_json(path, expected)

    def test_file_change_during_hashing_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'binary'
            path.write_bytes(b'fixture executable')
            expected = hashlib.sha256(path.read_bytes()).hexdigest()
            with mock.patch.object(generation, 'identity', side_effect=[('before',), ('after',)]):
                with self.assertRaisesRegex(ValueError, 'changed while hashing'):
                    generation.verified_digest(path, expected)

    def test_full_binding_rejects_a_changed_signed_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            binary = root / 'build-sim/Lumen.app/Lumen'
            monitor = root / 'tools/monitor_process.py'
            build = root / generation.BUILD_REPORT
            created = root / generation.CREATION_REPORT
            for path in (binary, monitor, build, created):
                path.parent.mkdir(parents=True, exist_ok=True)
            binary.write_bytes(b'proven executable')
            monitor.write_bytes(b'proven monitor')
            build.write_text(json.dumps({'natural_exit_code': 0, 'ended_by_monitor': False,
                                         'collector_error': None, 'owned_group_cleanup': {'error': None},
                                         'violations': []}))
            created_record = deepcopy(receipt())
            created_record['monitor_sha256'] = hashlib.sha256(monitor.read_bytes()).hexdigest()
            created.write_text(json.dumps(created_record))
            values = {'ROOT': root, 'CREATION_SHA': hashlib.sha256(created.read_bytes()).hexdigest(),
                      'BUILD_REPORT_SHA': hashlib.sha256(build.read_bytes()).hexdigest(),
                      'BINARY_SHA': hashlib.sha256(binary.read_bytes()).hexdigest(),
                      'MONITOR_SHA': created_record['monitor_sha256']}
            with mock.patch.multiple(generation, **values):
                proof = generation.verify_generation(root)
                self.assertEqual(proof['device'], generation.DEVICE)
                self.assertEqual(proof['source_executable_sha256'], values['BINARY_SHA'])
                binary.write_bytes(b'different build')
                with self.assertRaisesRegex(ValueError, 'recorded digest'):
                    generation.verify_generation(root)


if __name__ == '__main__':
    unittest.main()
