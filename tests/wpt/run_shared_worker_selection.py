#!/usr/bin/env python3
"""Run all30 SharedWorker variants from the unchanged frozen1515 WPT inventory.

This is a focused diagnostic selection, never a replacement for the1515 cohort
or a full-WPT percentage. Existing runner/reporter/fixture bytes stay unchanged.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import time
import traceback


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def save(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    with temporary.open('w') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)
    if json.loads(path.read_text()) != value:
        raise RuntimeError('Evidence readback mismatch: ' + str(path))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, default=Path('output/session-102900614/wpt-fixture-1'))
    parser.add_argument('--max-seconds', type=float, default=180)
    args = parser.parse_args()
    root = Path.cwd().resolve()
    binary = args.binary.resolve()
    if sha(binary) != args.sha256:
        raise RuntimeError('Executable differs from the explicitly selected artifact')
    helper = root / 'tools/apple-session-gpt6as/run_priority_wpt_10290244.py'
    specification = importlib.util.spec_from_file_location('shared_worker_pinned_inputs', helper)
    inputs = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(inputs)
    inputs.BASE = args.fixture.resolve()
    frozen, categories, environment, hashes, runner = inputs.current_inputs()
    schedule = [row for row in frozen['schedule'] if '.sharedworker.' in row['test']]
    if len(schedule) != 30 or len(frozen['schedule']) != 1515:
        raise RuntimeError('Frozen SharedWorker selection or full inventory changed')
    inventory = json.loads(Path(hashes['inventory']['path']).read_text())
    selected, _ = runner.select_schedule(inventory, 'testharness', [row['test'] for row in schedule])
    if selected != schedule:
        raise RuntimeError('Exact prefixes did not reproduce the original30 SharedWorker variants')
    out = args.output.resolve()
    if not out.is_relative_to(root):
        raise RuntimeError('Evidence output must remain inside this repository')
    out.mkdir(parents=True, exist_ok=False)
    utc = lambda: datetime.now(timezone.utc).isoformat()
    started = time.monotonic()
    plan = {'schema': 1, 'scope': 'All30 SharedWorker variants from frozen1515; not full-WPT or a new1515 pass rate',
        'officialWPT': True, 'wptRevision': frozen['wpt_revision'],
        'frozen1515ScheduleSHA256': frozen['schedule_sha256'], 'fullFrozenVariantCount': 1515,
        'selectedVariantCount': 30, 'unexecutedFrozenVariants': 1485, 'schedule': schedule,
        'selectedScheduleSHA256': inputs.digest(schedule), 'binary': str(binary),
        'binarySHA256': args.sha256, 'inputs': hashes, 'origins': environment['origins'],
        'timeoutSeconds': 15, 'longTimeoutSeconds': 65, 'startedUTC': utc(),
        'orchestratorSHA256': sha(__file__), 'complete': False}
    save(out / 'selection.json', plan)
    for key in tuple(os.environ):
        if key.startswith('LUMEN_'):
            del os.environ[key]
    os.environ.update(environment['nativeEnvironment'])
    run_dir = out / 'lumen'
    options = argparse.Namespace(inventory=Path(hashes['inventory']['path']), engine='lumen',
        binary=binary, output=run_dir, types='testharness', prefix=[row['test'] for row in schedule],
        offset=0, limit=None, max_seconds=args.max_seconds, server_config=Path(hashes['server_config']['path']),
        fixture_manifest=Path(hashes['fixture_readiness']['path']), strict_infrastructure=False,
        timeout=15, long_timeout=65, origin=environment['origins']['http'],
        secure_origin=environment['origins']['https'], h2_origin=environment['origins']['h2'],
        chromium_arg=environment['chromiumArguments'], chromium_headful=True, resume=False)
    error = None
    try:
        runner.run_command(options)
    except BaseException as failure:
        error = failure
        plan['error'] = type(failure).__name__ + ': ' + str(failure)
        plan['traceback'] = traceback.format_exc()
    rows = []
    ledger = run_dir / 'results.jsonl'
    if ledger.exists():
        metadata = json.loads((run_dir / 'run.json').read_text())
        signature = metadata['signature']
        if signature['binary_sha256'] != args.sha256 or runner.verified_schedule(run_dir, signature) != schedule:
            raise RuntimeError('Result provenance or schedule mismatch')
        rows = [json.loads(line) for line in ledger.read_text().splitlines()]
        names = [row['test'] for row in rows]
        if len(set(names)) != len(names) or any(name not in {row['test'] for row in schedule} for name in names):
            raise RuntimeError('Duplicate or foreign result in frozen SharedWorker selection')
        plan['resultsSHA256'] = sha(ledger)
        plan['runMetadataSHA256'] = sha(run_dir / 'run.json')
    def issue(row):
        cleanup = row.get('cleanup') or {}
        return runner.measurement_failure(row) or cleanup.get('error') or cleanup.get('profile_error')
    def passing(row):
        return row.get('status') in ('OK', 'PASS') and bool(row.get('subtests')) and all(
            assertion['status'] == 'PASS' for assertion in row['subtests']) and not issue(row)
    plan.update(finishedUTC=utc(), durationSeconds=round(time.monotonic() - started, 6),
        executed=len(rows), fullyPassing=sum(passing(row) for row in rows), unrun=30 - len(rows),
        harnessStatuses=dict(Counter(row['status'] for row in rows)),
        assertions=dict(Counter(assertion['status'] for row in rows for assertion in row.get('subtests', []))),
        failures=[{'test': row['test'], 'status': row['status'], 'message': row.get('message'),
            'measurementError': issue(row), 'assertions': [assertion for assertion in row.get('subtests', [])
            if assertion['status'] != 'PASS']} for row in rows if not passing(row)],
        complete=len(rows) == 30 and error is None, binarySHA256After=sha(binary))
    if plan['binarySHA256After'] != args.sha256:
        raise RuntimeError('Executable changed during measurement')
    for entry in hashes.values():
        if sha(entry['path']) != entry['sha256']:
            raise RuntimeError('Pinned measurement input changed: ' + entry['path'])
    save(out / 'selection.json', plan)
    print(json.dumps({key: value for key, value in plan.items() if key not in ('schedule', 'inputs', 'failures', 'traceback')}), flush=True)
    return 0 if plan['complete'] and plan['fullyPassing'] == 30 else 1


if __name__ == '__main__':
    raise SystemExit(main())
