#!/usr/bin/env python3
"""Read-only accounting of declared testdriver coverage and raw WPT outcomes.

The custom browser runner does not implement WebDriver/testdriver actions.
This report does not change results, remove failed attempts, or infer that a
browser lacks a feature from a test that requires unavailable automation.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import platform

REPO = Path('/Users/devin/repos/lumen')
INVENTORY_SHA = '9ffd9444520fd3158b7566f37600b0f10b56bbded1ccbd9b1b1ed8166a836053'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def file_digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def canonical_digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def identity(path):
    value = path.stat()
    return [value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_ctime_ns]


def declaration_reasons(metadata):
    reasons = []
    if metadata.get('testdriver') is True:
        reasons.append('testdriver_boolean')
    if metadata.get('testdriver_features'):
        reasons.append('testdriver_features')
    # Generated .any/.window/.worker wrappers may retain the script metadata
    # without setting the HTML-only `testdriver` boolean. Use the same include
    # spelling recognized by the pinned tools/manifest/sourcefile.py.
    for item in metadata.get('script_metadata') or []:
        if not isinstance(item, (list, tuple)) or len(item) != 2:
            continue
        name, value = item
        if isinstance(name, str) and name.strip() == 'script' and isinstance(value, str):
            value = value.strip()
            if value == '/resources/testdriver.js' or value.startswith('/resources/testdriver.js?'):
                reasons.append('script_metadata_include')
                break
    return reasons


def bucket(metadata):
    return 'declared_testdriver' if declaration_reasons(metadata) else 'testdriver_not_declared'


def totals():
    return {'files': 0, 'fully_passing_files': 0, 'harness': Counter(), 'assertions': Counter()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, default=REPO / 'output/wpt/inventory.json')
    parser.add_argument('--run', type=Path, action='append', default=[])
    parser.add_argument('--allow-incomplete', action='store_true', help='Diagnostic completed-row prefix only; never a completed-suite claim')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(platform.system() == 'Darwin', 'Read actual WPT evidence only on the authorized Mac through devinmac3d')
    inventory_path = args.inventory.resolve(strict=True)
    inventory_identity = identity(inventory_path)
    require(inventory_path.is_relative_to(REPO / 'output/wpt') and file_digest(inventory_path) == INVENTORY_SHA,
            'Pinned inventory differs')
    inventory = json.loads(inventory_path.read_text())
    counts, metadata_shapes, declarations, reason_counts = {}, {}, {}, {}
    for row in inventory['tests']:
        kind, metadata = row['type'], row.get('metadata') or {}
        category = bucket(metadata)
        counts.setdefault(kind, Counter())[category] += 1
        reason_counts.setdefault(kind, Counter()).update(declaration_reasons(metadata))
        for key, value in metadata.items():
            if 'driver' in key.lower():
                shape = json.dumps(value, sort_keys=True, separators=(',', ':'))
                metadata_shapes.setdefault(key, Counter())[shape] += 1
        declarations[(kind, row['test'])] = category
    output = {'inventory_sha256': INVENTORY_SHA, 'wpt_revision': inventory['wpt_revision'],
              'manifest_sha256': inventory['manifest_sha256'],
              'testdriver_backend_available': False,
              'inventory_by_type': counts, 'declaration_reasons_by_type': reason_counts,
              'testdriver_metadata_values': metadata_shapes,
              'interpretation': {
                  'declared_testdriver': 'The pinned manifest declares testdriver, its features or an explicit script metadata include. Complete automation is unavailable in this runner.',
                  'declaration_counts': 'A file can have multiple declaration reasons; only inventory_by_type contains disjoint file counts.',
                  'testdriver_not_declared': 'No declaration was found; this is not proof that dynamically loaded helpers need no automation.',
                  'raw_results': 'All PASS, FAIL, TIMEOUT, NOTRUN, CRASH, ERROR and infrastructure outcomes are retained in these counts.',
                  'limitation': 'A failure or timeout in an automation-dependent reference test is not evidence that Chromium lacks the web feature.',
                  'scope': 'This is a diagnostic qualification sidecar, not an official wptrunner pass-rate report and not a denominator adjustment.'},
              'primary_documentation': 'https://web-platform-tests.org/writing-tests/testdriver.html', 'runs': []}
    del inventory
    for supplied in args.run:
        directory = supplied.resolve(strict=True)
        require(directory.is_relative_to(REPO / 'output/wpt'), 'Run evidence must stay in project output')
        paths = {name: directory / name for name in ('run.json', 'schedule.json', 'results.jsonl')}
        before = {name: identity(path) for name, path in paths.items()}
        meta = json.loads(paths['run.json'].read_text())
        signature = meta['signature']
        require(signature.get('inventory_sha256') == INVENTORY_SHA, 'Run inventory differs')
        schedule = json.loads(paths['schedule.json'].read_text())
        require(canonical_digest(schedule) == signature['schedule_sha256'], 'Signed schedule changed')
        require(args.allow_incomplete or meta.get('finished_schedule') is True, 'Run is incomplete; retain explicit diagnostic qualification')
        groups = {name: totals() for name in ('declared_testdriver', 'testdriver_not_declared')}
        rows = 0
        for raw in paths['results.jsonl'].open('rb'):
            require(raw.endswith(b'\n'), 'Interrupted result tail; wait for a stable completed-row snapshot')
            if not raw.strip():
                continue
            row = json.loads(raw)
            require(rows < len(schedule) and (row['type'], row['test']) == (schedule[rows]['type'], schedule[rows]['test']),
                    'Result rows are not a contiguous scheduled prefix')
            key = (row['type'], row['test'])
            require(key in declarations, 'Result is outside the pinned inventory')
            group = groups[declarations[key]]
            group['files'] += 1
            group['harness'][row['status']] += 1
            assertions = row.get('subtests', [])
            group['assertions'].update(item['status'] for item in assertions)
            has_required_assertions = bool(assertions) or row['type'] not in ('testharness', 'test262')
            if has_required_assertions and row['status'] in ('OK', 'PASS') and all(item['status'] == 'PASS' for item in assertions):
                group['fully_passing_files'] += 1
            rows += 1
        require(not meta.get('finished_schedule') or rows == len(schedule), 'Completed run is missing result rows')
        evidence = {name: {'sha256': file_digest(path), 'identity': before[name]} for name, path in paths.items()}
        require(before == {name: identity(path) for name, path in paths.items()}, 'Run changed while being audited; retry after its current stage')
        output['runs'].append({'path': str(directory), 'engine': signature['engine'], 'binary_sha256': signature['binary_sha256'],
            'runner_sha256': signature['runner_sha256'], 'schedule_sha256': signature['schedule_sha256'],
            'files_scheduled': len(schedule), 'complete_rows_observed': rows, 'finished_schedule': meta.get('finished_schedule') is True,
            'automation_limitation_recorded_by_runner': meta.get('automation_limitation'),
            'stop_flags': {key: value for key, value in meta.items() if key.startswith('stopped_for_') and value},
            'groups': groups, 'source_files': evidence})
    require(identity(inventory_path) == inventory_identity, 'Inventory changed during audit')
    target = args.output.resolve()
    require(target.is_relative_to(REPO / 'output/wpt') and not target.exists(), 'Write a fresh project-local sidecar')
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open('x') as destination:
        json.dump(output, destination, indent=2)
        destination.write('\n')
    print(json.dumps({'report': str(target), 'inventory_by_type': counts,
                      'observed_runs': len(output['runs']), 'testdriver_backend_available': False}))


if __name__ == '__main__':
    main()
