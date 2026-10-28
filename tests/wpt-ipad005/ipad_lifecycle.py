"""Bind the new simulator lifecycle policy to retained, observed evidence.

This verifies files only. It performs no simulator action and never changes the
earlier failed SIGTERM cleanup result or its retained profile.
"""
import hashlib
import json
from pathlib import Path

ROOT = Path('/Users/devin/repos/lumen')
DEVICE = 'A77F68E4-972B-472B-80B8-F65E71D361E5'
BUNDLE = 'com.ecooxai.lumen'
POLICY = 'exact-ipad-simctl-platform-termination-v1'
PRIOR = ROOT / 'output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-004'
RECOVERY = PRIOR / 'owned-app-recovery-001/recovery.json'
RECOVERY_CANONICAL_SHA = 'f8c7139b63e450efb579494c4b6d614847c65078fc094be55814fd6c9a73d195'
ORIGINAL_ROW_SHA = 'd9e551cdc33fba92361b742176d43e2c17951d69df91d90105baae2f9a1871cd'
ORIGINAL_FILES = {
    'runtime-smoke-001/http/run.json': '28f56b3e68010957531b0e23fcd9c149883b2abc50cb1bd6f5a535700f6dd358',
    'runtime-smoke-001/http/results.jsonl': 'bcb2f060815f1ebaa8e9d91d3f4f722eb21732535898edce7cbb3bca05d68c6e',
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def canonical(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def read_evidence(path):
    path = Path(path)
    require(not path.is_symlink() and path.resolve(strict=True).is_relative_to(PRIOR),
            'Lifecycle evidence escaped the retained iPad004 generation')
    before = path.stat()
    identity = (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns)
    with path.open('rb') as source:
        data = source.read(4 * 1024 * 1024 + 1)
    after = path.stat()
    require(identity == (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns)
            and len(data) <= 4 * 1024 * 1024, 'Lifecycle evidence changed or exceeds its read bound')
    return data, hashlib.sha256(data).hexdigest()


def validate_recovery(record):
    require(record.get('status') == 'recovered_owned_app_gone_original_cleanup_remains_failed'
            and record.get('device') == DEVICE and record.get('bundle_id') == BUNDLE
            and record.get('original_test_cleanup_passed') is False
            and record.get('performance_release_gate_passed') is False
            and record.get('original_row_sha256') == ORIGINAL_ROW_SHA,
            'The observed recovery or original failed qualification changed')
    original = record.get('original_cleanup') or {}
    require(original.get('stopped') is False and original.get('sent_sigterm') is True
            and original.get('error') == 'Owned iPad app did not terminate within the bounded cleanup window',
            'Original failed Unix-signal cleanup is not retained')
    after = record.get('after') or {}
    require(after.get('identity', 'missing') is None and after.get('matching_installed_app_pids') == []
            and not record.get('error') and not record.get('collector_failure'),
            'Recovery did not establish that the owned app is gone')
    steps = record.get('steps') or []
    require(len(steps) == 1 and steps[0].get('command') == ['xcrun', 'simctl', 'terminate', DEVICE, BUNDLE],
            'Recovery did not use the exact selected simulator and bundle')
    measured = steps[0].get('measurement') or {}
    cleanup = measured.get('owned_group_cleanup') or {}
    require(steps[0].get('monitor_exit_code') == 0 and measured.get('natural_exit_code') == 0
            and measured.get('ended_by_monitor') is False and not measured.get('collector_error')
            and not measured.get('violations') and not cleanup.get('error')
            and cleanup.get('sent_sigterm') is False and cleanup.get('sent_sigkill') is False,
            'The actual platform termination request was not safe and successful')


def verify_lifecycle(root):
    require(Path(root).resolve(strict=True) == ROOT, 'Lifecycle proof belongs to the authorized Mac repository')
    raw, recovery_sha = read_evidence(RECOVERY)
    record = json.loads(raw)
    require(canonical(record) == RECOVERY_CANONICAL_SHA, 'The exact observed recovery receipt changed')
    validate_recovery(record)
    evidence = {str(RECOVERY): recovery_sha}
    original = {}
    for relative, expected in ORIGINAL_FILES.items():
        path = PRIOR / relative
        data, observed = read_evidence(path)
        require(observed == expected, 'The original failed iPad test evidence changed: ' + relative)
        evidence[str(path)] = observed
        original[relative] = data
    run = json.loads(original['runtime-smoke-001/http/run.json'])
    lines = original['runtime-smoke-001/http/results.jsonl'].splitlines()
    require(run.get('invocation_open') is False and run.get('finished_schedule') is False
            and run.get('completed_result_rows') == 1 and len(lines) == 1
            and canonical(json.loads(lines[0])) == ORIGINAL_ROW_SHA,
            'The retained failed invocation is not the exact closed one-case run')
    return {'policy': POLICY, 'evidence_sha256': evidence,
            'original_test_cleanup_passed': False, 'prior_performance_release_gate_passed': False,
            'app_graceful_shutdown_verified': False, 'natural_app_exit_status_available': False,
            'qualification': 'New tests use explicit simulator platform termination. Safe platform cleanup does not prove graceful app shutdown or rewrite the earlier failed SIGTERM result.'}
