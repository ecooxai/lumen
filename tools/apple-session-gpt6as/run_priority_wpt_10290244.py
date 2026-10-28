#!/usr/bin/env python3
"""Run immutable, rotated categories from the frozen priority WPT selection.

This orchestrates the repository's unchanged full_runner. Every frozen URL is
assigned to exactly one category. Budgets pause categories; they do not shrink
the schedule or turn missing results into successes.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path.cwd()
BASE = ROOT / 'output/wpt-10290244'
ORDER = ('dom', 'events', 'url', 'headers', 'microtasks', 'storage', 'encoding')
PIN = '7f8d49b5a82606efc771f43d10aad5bce5ccbfeb'
SCHEDULE_SHA = 'adcbf9719f722368b1e53ac1bd61cf38e566d3bd7495d9d6127cad38dd6aeabe'
POLICY_SHA = 'f50041b545279ac4c499bc9901989c7b12abaf78d90cdd914dee2f35d98d4150'


def sha(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def utc():
    return datetime.now(timezone.utc).isoformat()


def read(path):
    return json.loads(Path(path).read_text())


def exclusive_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x') as target:
        json.dump(value, target, indent=2)
        target.write('\n')


def load_runner():
    path = ROOT / 'tests/wpt/full_runner.py'
    spec = importlib.util.spec_from_file_location('priority_wpt_unchanged_runner', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def partition(frozen):
    if frozen['wpt_revision'] != PIN or digest(frozen['policy']) != POLICY_SHA:
        raise ValueError('Priority policy identity changed')
    if digest(frozen['schedule']) != SCHEDULE_SHA or frozen['schedule_sha256'] != SCHEDULE_SHA:
        raise ValueError('Frozen priority schedule identity changed')
    categories = {name: [] for name in ORDER}
    for row in frozen['schedule']:
        matches = []
        for name in ORDER:
            rule = frozen['policy']['categories'][name]
            if row['type'] != 'testharness' or not row['source'].startswith(rule['prefix']):
                continue
            if rule.get('all_sources') or re.match(rule['basename_pattern'], Path(row['source']).name, re.I):
                matches.append(name)
        if len(matches) != 1:
            raise ValueError('Each frozen URL must belong to exactly one category: ' + row['test'])
        categories[matches[0]].append(row)
    combined = sorted((row for rows in categories.values() for row in rows), key=lambda row: (row['type'], row['test']))
    if combined != frozen['schedule'] or sum(map(len, categories.values())) != 1515:
        raise ValueError('Category partition lost or duplicated a frozen URL')
    for name, rows in categories.items():
        if len(rows) != frozen['category_counts'][name]['variants']:
            raise ValueError('Frozen category count changed: ' + name)
    return categories


def current_inputs(check_server=True):
    frozen_path = BASE / 'priority-selection-v1.json'
    frozen = read(frozen_path)
    categories = partition(frozen)
    inventory_path = Path(frozen['inventory_path'])
    if sha(inventory_path) != frozen['inventory_sha256']:
        raise ValueError('Inventory changed after selection freeze')
    inventory = read(inventory_path)
    upstream = Path(inventory['wpt_root'])
    for relative, expected in frozen['source_sha256'].items():
        if sha(upstream / relative) != expected:
            raise ValueError('An upstream selected source changed: ' + relative)
    readiness_path = BASE / 'fixture-readiness.json'
    readiness = read(readiness_path)
    if not readiness.get('fixture_ready') or readiness.get('error'):
        raise ValueError('Official WPT fixture is not ready')
    environment_path = BASE / 'browser-fixture-environment.json'
    environment = read(environment_path)
    if environment['fixtureManifestSHA256'] != sha(readiness_path):
        raise ValueError('Browser environment belongs to another official fixture')
    if environment['nativeEnvironment'].get('LUMEN_HOST_MAP_MODE') != 'exact':
        raise ValueError('Native DNS mapping must preserve nonexistent-domain tests')
    if check_server:
        identity = subprocess.check_output(['ps', '-p', str(readiness['server_pid']), '-o', 'lstart='], text=True).strip()
        if identity != readiness['server_start_identity']:
            raise ValueError('Official WPT server process changed')
    runner = load_runner()
    runner.source_fixture_signature(inventory)
    config_path = BASE / 'server.config.json'
    if sha(config_path) != readiness['server_config_sha256']:
        raise ValueError('WPT server configuration changed')
    for name, rows in categories.items():
        selected, _ = runner.select_schedule(inventory, 'testharness', [row['test'] for row in rows])
        if selected != rows:
            raise ValueError('Runner URL prefixes no longer reproduce the frozen category: ' + name)
    files = {
        'frozen_selection': frozen_path, 'inventory': inventory_path,
        'fixture_readiness': readiness_path, 'server_config': config_path,
        'browser_environment': environment_path, 'runner': ROOT / 'tests/wpt/full_runner.py',
        'reporter': ROOT / 'tests/wpt/testharnessreport.js', 'monitor': ROOT / 'tools/monitor_process.py',
        'orchestrator': Path(__file__).resolve(),
    }
    hashes = {name: {'path': str(path.resolve()), 'sha256': sha(path)} for name, path in files.items()}
    return frozen, categories, environment, hashes, runner


def prepare(args):
    frozen, categories, environment, hashes, _ = current_inputs()
    binaries = {name: {'path': str(path.resolve()), 'sha256': sha(path)}
                for name, path in (('lumen', args.lumen), ('chromium', args.chromium))}
    plan = {'schema': 1, 'created_utc': utc(), 'scope': 'Frozen priority subset; never full WPT or Chrome parity',
            'upstream_revision': PIN, 'schedule_sha256': SCHEDULE_SHA,
            'manifest_sha256': frozen['manifest_sha256'],
            'source_fixture': {'wpt_revision': PIN, 'manifest_sha256': frozen['manifest_sha256'],
                               'tracked_upstream_sources_clean': True},
            'selected_variants': 1515, 'category_order': list(ORDER),
            'category_counts': frozen['category_counts'], 'inputs': hashes, 'binaries': binaries,
            'origins': environment['origins'], 'timeout_s': 15, 'long_timeout_s': 65,
            'native_fixture': {'host_map': environment['nativeEnvironment']['LUMEN_HOST_MAP'],
                               'host_map_mode': 'exact',
                               'ssl_cert_sha256': sha(environment['nativeEnvironment']['SSL_CERT_FILE'])},
            'chromium_arguments': environment['chromiumArguments'],
            'strict_infrastructure': False, 'chromium_headful': True,
            'category_schedule_sha256': {name: digest(rows) for name, rows in categories.items()},
            'limits': frozen['policy']['limitations'] + [
                'Invocation time budgets can differ; the identical per-file timeouts and full category schedule cannot.',
                'Every unfinished category retains its complete frozen schedule and explicit unrun count.',
                'Raw assertion totals are encoding-heavy; report all seven categories separately with equal importance.',
                'Shared/service workers, HTTP/2 and indirect helpers require actual execution evidence; readiness alone does not prove them.'
            ]}
    exclusive_json(args.output / 'cohort.json', plan)
    print(json.dumps({'cohort': str(args.output / 'cohort.json'), 'selected_variants': 1515,
                      'categories': {name: len(rows) for name, rows in categories.items()}, 'binaries': binaries}, indent=2))


def validate_plan(path, check_server=True, check_binaries=True):
    plan = read(path)
    frozen, categories, environment, hashes, runner = current_inputs(check_server)
    if hashes != plan['inputs'] or plan['schedule_sha256'] != SCHEDULE_SHA:
        raise ValueError('Cohort fixture, runner, telemetry or frozen schedule changed; create a new cohort')
    if plan['category_schedule_sha256'] != {name: digest(rows) for name, rows in categories.items()}:
        raise ValueError('Cohort category schedules changed')
    if sha(environment['nativeEnvironment']['SSL_CERT_FILE']) != plan['native_fixture']['ssl_cert_sha256']:
        raise ValueError('Test certificate authority changed after cohort creation')
    if check_binaries:
        for name, binary in plan['binaries'].items():
            if sha(binary['path']) != binary['sha256']:
                raise ValueError('Cohort executable changed: ' + name)
    return plan, categories, environment, runner


def run(args):
    plan, categories, environment, runner = validate_plan(args.plan)
    directory = args.plan.parent / args.category / args.engine
    for name in tuple(os.environ):
        if name.startswith('LUMEN_'):
            del os.environ[name]
    os.environ.update(environment['nativeEnvironment'])
    options = argparse.Namespace(
        inventory=Path(plan['inputs']['inventory']['path']), engine=args.engine,
        binary=Path(plan['binaries'][args.engine]['path']), output=directory,
        types='testharness', prefix=[row['test'] for row in categories[args.category]],
        offset=0, limit=None, max_seconds=args.max_seconds,
        server_config=Path(plan['inputs']['server_config']['path']),
        fixture_manifest=Path(plan['inputs']['fixture_readiness']['path']),
        strict_infrastructure=plan['strict_infrastructure'], timeout=plan['timeout_s'],
        long_timeout=plan['long_timeout_s'], origin=plan['origins']['http'],
        secure_origin=plan['origins']['https'], h2_origin=plan['origins']['h2'],
        chromium_arg=environment['chromiumArguments'], chromium_headful=plan['chromium_headful'],
        resume=(directory / 'run.json').exists())
    began = time.monotonic()
    receipt = {'schema': 1, 'started_utc': utc(), 'category': args.category, 'engine': args.engine,
               'cohort_sha256': sha(args.plan), 'invocation_budget_s': args.max_seconds,
               'binary_sha256': plan['binaries'][args.engine]['sha256']}
    error = None
    try:
        runner.run_command(options)
    except BaseException as exc:
        error = exc
        receipt['error'] = type(exc).__name__ + ': ' + str(exc)
    finally:
        receipt['finished_utc'] = utc()
        receipt['wall_seconds'] = round(time.monotonic() - began, 3)
        if (directory / 'run.json').exists():
            metadata = read(directory / 'run.json')
            receipt['finished_schedule'] = metadata.get('finished_schedule', False)
            receipt['stops'] = {key: value for key, value in metadata.items() if key.startswith('stopped_for_')}
            receipt['summary'] = read(directory / 'summary.json') if (directory / 'summary.json').exists() else None
        exclusive_json(directory / ('invocation-' + str(time.time_ns()) + '.json'), receipt)
        print(json.dumps(receipt, indent=2))
    if error:
        raise error


def verify_run(directory, engine, plan, schedule, runner):
    path = directory / 'run.json'
    if not path.exists():
        if (directory / 'results.jsonl').exists():
            raise ValueError('Result stream lacks provenance: ' + str(directory))
        return None
    metadata = read(path)
    signature = metadata['signature']
    for key, expected in {
        'engine': engine, 'binary_sha256': plan['binaries'][engine]['sha256'],
        'wpt_revision': PIN, 'schedule_sha256': digest(schedule),
        'manifest_sha256': plan['manifest_sha256'], 'source_fixture': plan['source_fixture'],
        'inventory_sha256': plan['inputs']['inventory']['sha256'],
        'runner_sha256': plan['inputs']['runner']['sha256'],
        'reporter_sha256': plan['inputs']['reporter']['sha256'],
        'monitor_sha256': plan['inputs']['monitor']['sha256'],
        'server_config_sha256': plan['inputs']['server_config']['sha256'],
        'fixture_manifest_sha256': plan['inputs']['fixture_readiness']['sha256'],
        'timeout_s': plan['timeout_s'], 'long_timeout_s': plan['long_timeout_s'],
        'strict_infrastructure': plan['strict_infrastructure'],
        'origin': plan['origins']['http'], 'secure_origin': plan['origins']['https'],
        'h2_origin': plan['origins']['h2'], 'chromium_args': plan['chromium_arguments'],
        'native_fixture': plan['native_fixture'] if engine == 'lumen' else None,
        'chromium_headless': not plan['chromium_headful'] if engine == 'chromium' else None,
    }.items():
        if signature.get(key) != expected:
            raise ValueError('Cohort result provenance differs at ' + key + ': ' + str(directory))
    if runner.verified_schedule(directory, signature) != schedule:
        raise ValueError('Category result schedule changed')
    return metadata


def report(args):
    plan, categories, _, runner = validate_plan(args.plan, check_server=False, check_binaries=False)
    result = {'schema': 1, 'created_utc': utc(), 'cohort_sha256': sha(args.plan),
              'scope': plan['scope'], 'scheduled_variants': 1515, 'category_order': list(ORDER),
              'all_categories_complete': True, 'categories': {}, 'limits': plan['limits']}
    for name, schedule in categories.items():
        lumen = args.plan.parent / name / 'lumen'
        chromium = args.plan.parent / name / 'chromium'
        lm = verify_run(lumen, 'lumen', plan, schedule, runner)
        cm = verify_run(chromium, 'chromium', plan, schedule, runner)
        if lm and cm:
            runner.compare_command(argparse.Namespace(lumen=lumen, chromium=chromium,
                                                      output=args.plan.parent / name / 'comparison.json'))
        ls, cs, parity = runner.compare_streams(lumen / 'results.jsonl', chromium / 'results.jsonl', schedule)
        complete = ls['remaining_files'] == 0 and cs['remaining_files'] == 0
        healthy = complete and all(sum(value['file_outcomes'].get(status, 0) for status in ('OK', 'PASS')) == len(schedule) and
                                   value['zero_assertion_testharness_files'] == 0 for value in (ls, cs))
        result['all_categories_complete'] &= complete
        result['categories'][name] = {'scheduled_variants': len(schedule), 'lumen': ls, 'chromium': cs,
                                     'parity': parity, 'both_schedules_complete': complete,
                                     'both_harnesses_complete_without_zero_assertion_files': healthy,
                                     'states': {engine: {'started': bool(meta),
                                         'finished_schedule': bool(meta and meta.get('finished_schedule')),
                                         'stops': {key: value for key, value in (meta or {}).items() if key.startswith('stopped_for_')}}
                                         for engine, meta in (('lumen', lm), ('chromium', cm))}}
    runner.write_json(args.plan.parent / 'priority-comparison.json', result)
    print(json.dumps({'output': str(args.plan.parent / 'priority-comparison.json'),
                      'all_categories_complete': result['all_categories_complete'],
                      'categories': {name: {'scheduled': value['scheduled_variants'],
                           'lumen_recorded': value['lumen']['recorded_files'],
                           'chromium_recorded': value['chromium']['recorded_files'],
                           'retention': value['parity']['chromium_pass_retention'],
                           'complete': value['both_schedules_complete']} for name, value in result['categories'].items()}}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    prep = commands.add_parser('prepare')
    prep.add_argument('--lumen', type=Path, required=True)
    prep.add_argument('--chromium', type=Path, required=True)
    prep.add_argument('--output', type=Path, required=True)
    prep.set_defaults(func=prepare)
    execute = commands.add_parser('run')
    execute.add_argument('--plan', type=Path, required=True)
    execute.add_argument('--category', choices=ORDER, required=True)
    execute.add_argument('--engine', choices=('lumen', 'chromium'), required=True)
    execute.add_argument('--max-seconds', type=float, default=90)
    execute.set_defaults(func=run)
    summarize = commands.add_parser('report')
    summarize.add_argument('--plan', type=Path, required=True)
    summarize.set_defaults(func=report)
    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
