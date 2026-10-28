"""Read-only identity proof for the single newly provisioned iPad generation.

The signed app is the proven build003 bundle. Later Mac source changes are
deliberately not represented as installed iPad changes by this generation.
"""
import hashlib
import json
from pathlib import Path


ROOT = Path('/Users/devin/repos/lumen')
DEVICE = 'A77F68E4-972B-472B-80B8-F65E71D361E5'
OLD_DEVICE = '590ACCA9-0BA2-4803-970F-EF3DAC515255'
BUNDLE = 'com.ecooxai.lumen'
GENERATION = 'output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-004'
CREATION_REPORT = GENERATION + '/device-create-001/isolated-ipad.json'
CREATION_SHA = '0415de2f2c054cd7558b650ad6d99e5276d28d9c28ca14bf8779a21dccf732bf'
CREATE_HELPER_SHA = '01a831c5362997f554646643e2792a942f95629d7e90bcacd90621ec7e7d92fd'
MONITOR_SHA = '36a0194c2d71c484841b31887773bf02f6f9e95d519132aeb5ee211ca3dde98a'
BUILD_REPORT = 'output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-003/simulator-build.json'
BUILD_REPORT_SHA = 'ae5a8405de51c9124008f081a8bfd17d0986e0a3ad24cc460ba445ac4d60dc12'
BINARY_SHA = 'b6bfb5ffa7ec86c144e4c49842ac911b60f40631d8a06fa87147e20333c0cd97'
RUNTIME = 'com.apple.CoreSimulator.SimRuntime.iOS-26-5'
RUNTIME_BUILD = '23F73'
DEVICE_TYPE = 'com.apple.CoreSimulator.SimDeviceType.iPad-Air-11-inch-M4'
DEVICE_NAME = 'Lumen gpt-6-astra-pro devinmac3d iPad 20261022-004'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def identity(path):
    value = Path(path).stat()
    return (value.st_dev, value.st_ino, value.st_mode, value.st_size,
            value.st_mtime_ns, value.st_ctime_ns)


def verified_digest(path, expected):
    path = Path(path)
    before = identity(path)
    with path.open('rb') as source:
        observed = hashlib.file_digest(source, 'sha256').hexdigest()
    require(identity(path) == before, 'Generation input changed while hashing: ' + str(path))
    require(observed == expected, 'Generation input does not match its recorded digest: ' + str(path))
    return observed


def verified_json(path, expected):
    path = Path(path)
    before = identity(path)
    with path.open('rb') as source:
        data = source.read(4 * 1024 * 1024 + 1)
    require(len(data) <= 4 * 1024 * 1024, 'Generation receipt unexpectedly exceeds 4 MiB')
    require(identity(path) == before, 'Generation receipt changed during inspection')
    require(hashlib.sha256(data).hexdigest() == expected, 'Generation receipt digest changed: ' + str(path))
    return json.loads(data)


def validate_creation(record):
    expected = {'status': 'isolated_ipad_ready_for_signed_install', 'old_device': OLD_DEVICE,
                'new_device': DEVICE, 'new_name': DEVICE_NAME, 'device_type': DEVICE_TYPE,
                'runtime': RUNTIME, 'runtime_build': RUNTIME_BUILD,
                'helper_sha256': CREATE_HELPER_SHA, 'monitor_sha256': MONITOR_SHA}
    for key, value in expected.items():
        require(record.get(key) == value, 'Provisioning receipt identity differs: ' + key)
    require(not record.get('error') and record.get('finished_unix') is not None,
            'Provisioning receipt is incomplete or failed')
    require(record.get('old_device_running_lumen') == [], 'Original iPad was not verified idle')
    for key, uuid, state in (('old_device_after', OLD_DEVICE, 'Shutdown'),
                             ('new_device_created', DEVICE, 'Shutdown'),
                             ('new_device_ready', DEVICE, 'Booted')):
        device = record.get(key, {})
        require(device.get('udid') == uuid and device.get('state') == state
                and device.get('runtime') == RUNTIME and device.get('device_type') == DEVICE_TYPE,
                'Provisioning receipt has inconsistent device state: ' + key)
        if uuid == DEVICE:
            require(device.get('name') == DEVICE_NAME, 'Fresh iPad name differs from its creation receipt')
    runtime = record.get('runtime_verified', {})
    require(runtime == {'identifier': RUNTIME, 'build': RUNTIME_BUILD,
                        'architecture': 'arm64', 'product_family': 'iPad'},
            'Provisioning receipt has inconsistent runtime evidence')


def verify_generation(root):
    """No host calls or mutations; bind exact receipts and executable bytes."""
    root = Path(root).resolve(strict=True)
    require(root == ROOT, 'This generation belongs only to the authorized Mac repository')
    paths = {name: (root / relative).resolve(strict=True) for name, relative in
             (('creation', CREATION_REPORT), ('build', BUILD_REPORT),
              ('binary', 'build-sim/Lumen.app/Lumen'), ('monitor', 'tools/monitor_process.py'))}
    require(all(path.is_relative_to(root) for path in paths.values()), 'A generation input escaped the repository')
    creation = verified_json(paths['creation'], CREATION_SHA)
    validate_creation(creation)
    built = verified_json(paths['build'], BUILD_REPORT_SHA)
    require(built.get('natural_exit_code') == 0 and built.get('ended_by_monitor') is False
            and not built.get('collector_error') and not (built.get('owned_group_cleanup') or {}).get('error')
            and not built.get('violations'), 'The proven build003 completion gate differs')
    verified_digest(paths['binary'], BINARY_SHA)
    verified_digest(paths['monitor'], MONITOR_SHA)
    return {'device': DEVICE, 'bundle_id': BUNDLE, 'runtime': RUNTIME, 'runtime_build': RUNTIME_BUILD,
            'device_type': DEVICE_TYPE, 'creation_report': CREATION_REPORT, 'creation_report_sha256': CREATION_SHA,
            'build_report': BUILD_REPORT, 'build_report_sha256': BUILD_REPORT_SHA,
            'source_executable_sha256': BINARY_SHA, 'monitor_sha256': MONITOR_SHA,
            'source_generation': 'Signed build003; later Mac-only changes are not included.'}
