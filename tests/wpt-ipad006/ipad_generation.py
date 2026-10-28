"""Bind the actual isolated build006 and preserved003/005 evidence to one iPad."""
import hashlib
import importlib.util
import json
from pathlib import Path


ROOT = Path('/Users/devin/repos/lumen')
DEVICE = 'A77F68E4-972B-472B-80B8-F65E71D361E5'
BUNDLE = 'com.ecooxai.lumen'
GENERATION = 'output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006'
BUILD = 'build-sim-ipad006'
APP = BUILD + '/Lumen.app'
BUILD_REPORT = GENERATION + '/build-identity-001/simulator-build.json'
BUILD_RECEIPT = GENERATION + '/build-identity-001/build-identity.json'
BUILD_RECEIPT_SHA = 'f660fae6cbc10298b9efd24b9a32e64317261adecf30dc245716bb516fdf7ae9'
BUILD_HELPER_SHA = '06931fd158e51b7423b013535223b612f654c8d868cf4ad36217be4ffb5bfcc3'
BINARY_SHA = '351a9f393b76d3f6793409a4033dd121441f68714d02729056d8919bc800d539'
BUNDLE_SHA = '9eb2aa287b8ebe73c51193c2ce659a19313a81b3c646df0aeb902b7e1d8cb479'
SOURCE_MANIFEST_SHA = '758bc59ccc49b38056e5f3d39ab2e5e9f14b6824081e272d9e198684822d82ed'
NATIVE_SHA = '14814f31ee1f7771ea3e159adb21f02c13058cb601ab1c5f7af27de9c4a6d538'
MONITOR_SHA = '36a0194c2d71c484841b31887773bf02f6f9e95d519132aeb5ee211ca3dde98a'
PREVIOUS_READER_SHA = '38d6f962059e932e12a158ae94df37ca289f434380e6478b1ede856b31fafb73'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def identity(path):
    value = Path(path).stat()
    return [value.st_dev, value.st_ino, value.st_mode, value.st_size,
            value.st_mtime_ns, value.st_ctime_ns]


def canonical(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def verified_digest(path, expected):
    path = Path(path)
    require(not path.is_symlink() and path.is_file(), 'Generation input is not an ordinary file')
    before = identity(path)
    with path.open('rb') as source:
        observed = hashlib.file_digest(source, 'sha256').hexdigest()
    require(identity(path) == before and observed == expected,
            'Generation input changed or has a different digest: ' + str(path))
    return observed


def verified_json(path, expected):
    path = Path(path)
    require(not path.is_symlink() and path.is_file(), 'Generation receipt is not an ordinary file')
    before = identity(path)
    with path.open('rb') as source:
        data = source.read(16 * 1024 * 1024 + 1)
    require(len(data) <= 16 * 1024 * 1024 and identity(path) == before
            and hashlib.sha256(data).hexdigest() == expected,
            'Generation receipt changed, exceeds its bound or has a different digest')
    return json.loads(data)


def validate_build_receipt(record):
    expected = {'status': 'current_source_signed_simulator_build_verified', 'device': DEVICE,
                'bundle_id': BUNDLE, 'generation': GENERATION, 'build_output': BUILD,
                'helper_sha256': BUILD_HELPER_SHA, 'monitor_sha256': MONITOR_SHA,
                'source_manifest_sha256': SOURCE_MANIFEST_SHA, 'build_monitor': BUILD_REPORT,
                'source_signature_verified': True, 'source_inputs_unchanged': True,
                'old_source_and_evidence_unchanged': True, 'installed': False, 'runtime_tests_executed': False}
    for key, value in expected.items():
        require(record.get(key) == value and type(record.get(key)) is type(value),
                'Actual build006 receipt differs: ' + key)
    require(not record.get('error') and record.get('finished_unix') is not None,
            'Actual build006 did not close successfully')
    require(record.get('build_command') == ['make', '-j3', 'PLAT=sim', 'BUILD=' + BUILD, 'app'],
            'Build006 command is not the reviewed isolated simulator target')
    require(canonical(record['source_manifest']) == SOURCE_MANIFEST_SHA
            and record['native_snapshot']['sha256'] == NATIVE_SHA,
            'The built source generation changed')
    bundle = record.get('signed_source_bundle') or {}
    require(bundle.get('app') == str(ROOT / APP) and bundle.get('executable_sha256') == BINARY_SHA
            and bundle.get('bundle_contents_sha256') == BUNDLE_SHA
            and canonical(bundle['entries']) == BUNDLE_SHA,
            'Signed build006 bundle provenance differs')
    measured = record.get('build_measurement') or {}
    cleanup = measured.get('owned_group_cleanup') or {}
    require(record.get('monitor_return_code') == 0 and measured.get('natural_exit_code') == 0
            and measured.get('ended_by_monitor') is False and not measured.get('collector_error')
            and not measured.get('violations') and not cleanup.get('error')
            and cleanup.get('sent_sigterm') is False and cleanup.get('sent_sigkill') is False,
            'Actual build006 monitor did not close naturally and safely')


def prior_binding(root):
    path = root / 'tests/wpt-ipad005/ipad_generation.py'
    verified_digest(path, PREVIOUS_READER_SHA)
    spec = importlib.util.spec_from_file_location('retained_ipad003_generation', path)
    previous = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(previous)
    return previous.verify_generation(root)


def verify_generation(root):
    root = Path(root).resolve(strict=True)
    require(root == ROOT, 'This generation belongs only to the authorized Mac repository')
    record = verified_json(root / BUILD_RECEIPT, BUILD_RECEIPT_SHA)
    validate_build_receipt(record)
    previous = prior_binding(root)
    require(previous == record['old_generation_binding'], 'Retained build003 provisioning evidence changed')
    measured = verified_json(root / BUILD_REPORT, record['build_measurement_sha256'])
    require(measured == record['build_measurement'], 'Retained actual build monitoring evidence differs')
    verified_digest(root / 'tools/monitor_process.py', MONITOR_SHA)
    import ipad_runner as entry
    source = record['signed_source_bundle']
    app = root / APP
    require(app.resolve(strict=True) == app and not app.is_symlink(), 'Signed build006 app path changed')
    entry.check_bundle_guards({str(app): source['guard'],
                               record['old_source_bundle']['app']: record['old_source_bundle']['guard']})
    for relative, expected in source['entries'].items():
        if expected['kind'] == 'file':
            path = app / relative
            require(path.resolve(strict=True).is_relative_to(app), 'Signed bundle member escaped the source app')
            verified_digest(path, expected['sha256'])
    for name, expected in record['old_evidence'].items():
        path = Path(name)
        require(path.resolve(strict=True).is_relative_to(root / 'output/wpt'),
                'Retained old evidence escaped the project')
        require(identity(path) == expected['identity'], 'Closed005 evidence identity changed')
        verified_digest(path, expected['sha256'])
    entry.check_bundle_guards({str(app): source['guard'],
                               record['old_source_bundle']['app']: record['old_source_bundle']['guard']})
    return {'device': DEVICE, 'bundle_id': BUNDLE, 'runtime': previous['runtime'],
            'runtime_build': previous['runtime_build'], 'device_type': previous['device_type'],
            'creation_report': previous['creation_report'], 'creation_report_sha256': previous['creation_report_sha256'],
            'build_report': BUILD_REPORT, 'build_report_sha256': record['build_measurement_sha256'],
            'build_receipt': BUILD_RECEIPT, 'build_receipt_sha256': BUILD_RECEIPT_SHA,
            'source_executable_sha256': BINARY_SHA, 'source_bundle_contents_sha256': BUNDLE_SHA,
            'source_manifest_sha256': SOURCE_MANIFEST_SHA, 'native_source_generation_sha256': NATIVE_SHA,
            'monitor_sha256': MONITOR_SHA, 'retained003_and005_verified': True,
            'source_generation': 'Isolated signed build006 of the recorded14814f31 source generation, including parsed-iframe bootstrap. Later native changes are separate generations.'}
