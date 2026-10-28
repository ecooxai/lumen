#!/usr/bin/env python3
"""Read every matched assertion in the closed 200-file pilot without exclusions.

Directory/source/message groupings prioritize investigation; they do not prove
a root cause. Original rows stay untouched. Full derived assertion/gap streams
retain raw messages and occurrence numbers, including duplicate test names.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path
import platform
import re
import sys
import time


ROOT = Path('/Users/devin/repos/lumen')
ASSESSOR_SHA = '22a1afa256de67b01d327b5be8e04a3bf5fb01215c2db570f7e4177adb18e36f'
CORE_SHA = 'a7bc96c3697fb099ad63c145011bc434a66d865a713a62e5198731bed9600ac7'
EXPECTED_COUNTS = {'chromium_passing_assertions': 1922, 'also_passed_by_lumen': 928,
                   'lumen_fail': 775, 'lumen_missing': 209, 'lumen_timeout': 5, 'lumen_notrun': 5}
MAX_DERIVED_BYTES = 256 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def checked_import(name, path, expected):
    with path.open('rb') as source:
        require(hashlib.file_digest(source, 'sha256').hexdigest() == expected, 'Reviewed sidecar changed')
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    sys.modules[name] = result
    spec.loader.exec_module(result)
    return result


def automation_reasons(metadata):
    reasons = []
    if metadata.get('testdriver') is True:
        reasons.append('testdriver_boolean')
    if metadata.get('testdriver_features'):
        reasons.append('testdriver_features')
    for item in metadata.get('script_metadata') or []:
        if isinstance(item, (list, tuple)) and len(item) == 2:
            name, value = item
            if isinstance(name, str) and name.strip() == 'script' and isinstance(value, str):
                value = value.strip()
                if value == '/resources/testdriver.js' or value.startswith('/resources/testdriver.js?'):
                    reasons.append('script_metadata_include')
                    break
    return reasons


def first_message_line(value):
    """A display/grouping pattern only; the derived JSONL keeps the whole message."""
    if value is None:
        return '(no message)'
    text = str(value).splitlines()[0] if str(value).splitlines() else ''
    return re.sub(r'\s+', ' ', text).strip() or '(empty message)'


def indexed_assertions(row):
    seen, result = Counter(), {}
    for sub in row.get('subtests', []):
        name = sub['name']
        occurrence = seen[name]
        seen[name] += 1
        result[(name, occurrence)] = sub
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path,
                        default=Path('output/wpt/gpt-6-astra-pro-devinmac3d-first200-gap-triage-v37'))
    parser.add_argument('--assessor', type=Path, default=Path('tests/wpt/assess_first_broad_shard_v37_200.py'))
    args = parser.parse_args()
    require(platform.system() == 'Darwin' and Path.cwd().resolve() == ROOT,
            'Read actual evidence only through the authorized Mac environment')
    assessor_path = (ROOT / args.assessor).resolve(strict=True)
    require(assessor_path.is_relative_to(ROOT), 'The pinned assessor must be project-local')
    assess = checked_import('pilot200_evidence_reader', assessor_path, ASSESSOR_SHA)
    core = checked_import('pilot200_comparison_core', ROOT / 'tests/wpt/full_runner.py', CORE_SHA)
    evidence = assess.Evidence()
    cohort = evidence.load(assess.COHORT / 'cohort.json')
    controller = evidence.load(assess.COHORT / 'controller.json')
    require(assess.canonical(cohort) == assess.COHORT_DIGEST == controller['cohort_sha256'], 'Closed cohort changed')
    require(controller.get('status') == 'paused' and controller.get('child_pid') is None
            and controller.get('stop_reason') == 'Invocation shard limit reached', 'Wait for the intentionally paused first200')
    unit = next(item for item in cohort['units'] if item['name'] == assess.UNIT)
    require(unit['phase'] == 'broad' and unit['type'] == 'testharness' and unit['offset'] == 0
            and unit['selected_files'] == 200, 'Exact first200 schedule changed')
    state = controller['units'][assess.UNIT]
    require(state.get('paired_complete') is True, 'The matched pair is not closed')
    directory = assess.COHORT / assess.UNIT
    comparison_path = directory / 'comparison.json'
    comparison = evidence.load(comparison_path)
    require(evidence.files[str(comparison_path)]['sha256'] == assess.COMPARISON_SHA == state['comparison_sha256'],
            'Actual matched comparison changed')
    require(comparison['parity']['counts'] == EXPECTED_COUNTS, 'The observed comparison denominator or outcomes changed')
    metas, schedules = {}, {}
    for engine in ('lumen', 'chromium'):
        metas[engine] = evidence.load(directory / engine / 'run.json')
        schedules[engine] = evidence.load(directory / engine / 'schedule.json')
        signature = metas[engine]['signature']
        require(signature['engine'] == engine and signature['runner_sha256'] == CORE_SHA
                and signature['inventory_sha256'] == assess.INVENTORY_SHA
                and assess.canonical(schedules[engine]) == signature['schedule_sha256'] == unit['schedule_sha256']
                and metas[engine].get('finished_schedule') is True and len(schedules[engine]) == 200,
                'Actual engine schedule/provenance differs')
    require(schedules['lumen'] == schedules['chromium'], 'Matched schedules differ')
    require(metas['lumen']['signature']['binary_sha256'] == 'd7fea94c14f7323446bcd6ca47f53bfba11b9bb86a4dcdf6acf41a309a997e5b',
            'Pilot native generation changed')
    for field in ('wpt_revision', 'manifest_sha256', 'source_fixture', 'server_config_sha256', 'fixture_manifest_sha256',
                  'selection', 'reporter_sha256', 'monitor_sha256', 'origin', 'secure_origin', 'h2_origin',
                  'timeout_s', 'long_timeout_s', 'strict_infrastructure'):
        require(metas['lumen']['signature'][field] == metas['chromium']['signature'][field],
                'Cross-engine fixture/selection differs: ' + field)
    output = (ROOT / args.output).resolve()
    require(output.is_relative_to(ROOT / 'output/wpt') and not output.exists(), 'Use a fresh derived-evidence directory')
    output.mkdir(parents=True, exist_ok=False)
    totals, raw, files_by_status = Counter(), {'lumen': Counter(), 'chromium': Counter()}, {'lumen': Counter(), 'chromium': Counter()}
    groups = {name: {} for name in ('areas', 'directories', 'sources', 'message_patterns', 'automation_declarations')}
    derived_bytes = derived_rows = 0
    parity = core.Parity()
    report = {'status': 'reading', 'scope': 'Exactly200 matched testharness variants; no exclusions or whole-suite score',
              'cohort': str(assess.COHORT), 'cohort_canonical_sha256': assess.COHORT_DIGEST,
              'comparison_sha256': assess.COMPARISON_SHA, 'source_generation_sha256': metas['lumen']['signature']['binary_sha256'],
              'reference_performance_release_gate_passed': False,
              'interpretation': {'ranking': 'Path and first-message-line groupings prioritize investigation; they do not establish a shared root cause.',
                  'duplicates': 'Assertions match by exact name and zero-based occurrence within each scheduled variant.',
                  'missing': 'A missing matching assertion remains a Chrome-PASS gap; no feature or execution failure is inferred from absence alone.',
                  'automation': 'This runner has no testdriver backend. Declared requirements are annotated without excluding any outcome; absence of a declaration does not prove no dynamic automation dependency.',
                  'raw': 'All raw browser rows remain authoritative and unchanged. Derived streams retain every assertion from either engine, raw messages, all200 file outcomes and every Chrome-PASS gap.'}}

    def add_group(kind, key, index, is_pass, status, example):
        table = groups[kind]
        require(len(table) < 20000 or key in table, 'Diagnostic grouping exceeds its key bound')
        if key not in table:
            table[key] = {'key': key, 'files': set(), 'chrome_pass_assertions': 0, 'retained': 0,
                          'gaps': Counter(), 'examples': []}
        group = table[key]
        group['files'].add(index)
        group['chrome_pass_assertions'] += 1
        if is_pass:
            group['retained'] += 1
        else:
            group['gaps'][status] += 1
            if len(group['examples']) < 5:
                group['examples'].append(example)

    def write_row(handle, value):
        nonlocal derived_bytes, derived_rows
        data = (json.dumps(value, ensure_ascii=True, separators=(',', ':')) + '\n').encode()
        derived_bytes += len(data)
        derived_rows += 1
        require(derived_bytes <= MAX_DERIVED_BYTES and derived_rows <= 500000,
                'Derived evidence exceeds its explicit output bound')
        handle.write(data)

    try:
        lumen_rows = iter(evidence.rows(directory / 'lumen/results.jsonl', 200, 16 * 1024 * 1024))
        chrome_rows = iter(evidence.rows(directory / 'chromium/results.jsonl', 200, 16 * 1024 * 1024))
        with (output / 'files.jsonl').open('xb') as file_out, (output / 'assertions.jsonl').open('xb') as assertion_out, (output / 'chrome-pass-gaps.jsonl').open('xb') as gap_out:
            for index, scheduled in enumerate(schedules['lumen']):
                lumen, chrome = next(lumen_rows, None), next(chrome_rows, None)
                key = (scheduled['type'], scheduled['test'])
                require(lumen is not None and chrome is not None
                        and (lumen['type'], lumen['test']) == key == (chrome['type'], chrome['test']),
                        'A complete contiguous raw result row is missing or changed')
                parity.add(key, lumen, chrome)
                source = scheduled['source']
                parts = source.strip('/').split('/')
                area = parts[0] if len(parts) > 1 else '(root)'
                source_directory = '/'.join(parts[:-1]) or '(root)'
                reasons = automation_reasons(scheduled.get('metadata') or {})
                automatic = 'declared_testdriver' if reasons else 'testdriver_not_declared'
                base = {'file_index': index, 'type': key[0], 'test': key[1], 'source': source,
                        'area': area, 'directory': source_directory, 'automation_declaration': automatic,
                        'automation_reasons': reasons}
                indexed = indexed_assertions(lumen)
                seen, matched, per_file = Counter(), set(), Counter()
                for engine, row in (('lumen', lumen), ('chromium', chrome)):
                    files_by_status[engine][row['status']] += 1
                    raw[engine].update(item['status'] for item in row.get('subtests', []))
                for sub in chrome.get('subtests', []):
                    name = sub['name']
                    occurrence = seen[name]
                    seen[name] += 1
                    assertion_key = (name, occurrence)
                    left = indexed.get(assertion_key)
                    if left is not None:
                        matched.add(assertion_key)
                    status = left['status'] if left is not None else 'MISSING'
                    gap = sub['status'] == 'PASS' and status != 'PASS'
                    row = {**base, 'subtest': name, 'occurrence': occurrence, 'chromium': sub, 'lumen': left,
                           'lumen_status': status, 'chrome_pass_gap': gap,
                           'file_harness': {'lumen': lumen['status'], 'chromium': chrome['status']}}
                    write_row(assertion_out, row)
                    if sub['status'] != 'PASS':
                        continue
                    totals['chromium_passing_assertions'] += 1
                    per_file['chromium_passing_assertions'] += 1
                    if status == 'PASS':
                        totals['also_passed_by_lumen'] += 1
                        per_file['also_passed_by_lumen'] += 1
                    else:
                        totals['lumen_' + status.lower()] += 1
                        per_file['lumen_' + status.lower()] += 1
                        row['lumen_harness_message'] = lumen.get('message')
                        write_row(gap_out, row)
                    example = {'file_index': index, 'test': key[1], 'subtest': name,
                               'occurrence': occurrence, 'lumen_status': status}
                    for kind, group_key in (('areas', area), ('directories', source_directory),
                                             ('sources', source), ('automation_declarations', automatic)):
                        add_group(kind, group_key, index, not gap, status, example)
                    if gap:
                        message = first_message_line(left.get('message') if left is not None else lumen.get('message'))
                        pattern = status + ' | ' + source_directory + ' | ' + message
                        # Full strings remain in JSONL. A hash avoids giant map keys.
                        pattern_key = hashlib.sha256(pattern.encode()).hexdigest()
                        add_group('message_patterns', pattern_key, index, False, status, example)
                        groups['message_patterns'][pattern_key]['display'] = pattern[:600]
                        groups['message_patterns'][pattern_key]['display_truncated'] = len(pattern) > 600
                for assertion_key, left in indexed.items():
                    if assertion_key not in matched:
                        write_row(assertion_out, {**base, 'subtest': assertion_key[0], 'occurrence': assertion_key[1],
                            'chromium': None, 'lumen': left, 'chrome_pass_gap': False,
                            'classification': 'Lumen assertion has no corresponding Chromium assertion; retained outside Chrome-PASS denominator'})
                write_row(file_out, {**base, 'counts': per_file,
                    'harness': {engine: {key: row.get(key) for key in
                        ('status', 'message', 'duration_ms', 'subtest_count', 'reporter_state', 'watchdog', 'cleanup', 'telemetry_errors')}
                        for engine, row in (('lumen', lumen), ('chromium', chrome))},
                    'assertions': {engine: dict(Counter(item['status'] for item in row.get('subtests', [])))
                        for engine, row in (('lumen', lumen), ('chromium', chrome))}})
            require(next(lumen_rows, None) is None and next(chrome_rows, None) is None, 'Rows exceed the exact200 schedule')
        require(dict(totals) == EXPECTED_COUNTS == parity.value()['counts']
                and parity.value() == comparison['parity'],
                'Full derived accounting differs from the frozen official comparison logic')
        ranked = {}
        for kind, table in groups.items():
            records = []
            for value in table.values():
                value['file_indices'] = sorted(value.pop('files'))
                value['file_count'] = len(value['file_indices'])
                value['gap_count'] = sum(value['gaps'].values())
                records.append(value)
            ranked[kind] = sorted(records, key=lambda value: (-value['gap_count'], -value['file_count'], value['key']))
        evidence.unchanged()
        report.update(status='complete', paired_files=200, counts=dict(totals), raw_assertion_outcomes=raw,
                      raw_file_outcomes=files_by_status, chromium_pass_retention=928 / 1922,
                      total_chrome_pass_gaps=sum(value for key, value in totals.items() if key.startswith('lumen_')),
                      ranked_groups=ranked, source_files=evidence.files,
                      actual_run_state=comparison['run_state'],
                      inherited_reference_incidents={name: (value.get('chromium') or {}).get('functional_reference_incident')
                          for name, value in controller['units'].items()},
                      actual_first200_reference_incident=state['chromium'].get('functional_reference_incident'),
                      derived_streams={name: {'bytes': (output / name).stat().st_size,
                                             'sha256': core.file_digest(output / name)}
                          for name in ('files.jsonl', 'assertions.jsonl', 'chrome-pass-gaps.jsonl')})
    except Exception as error:
        report.update(status='failed_incomplete_derived_evidence', error=type(error).__name__ + ': ' + str(error))
    report['finished_unix'] = time.time()
    with (output / 'summary.json').open('x') as summary:
        json.dump(report, summary, indent=2)
        summary.write('\n')
    def compact_ranking(kind):
        return [{key: value[key] for key in
                 ('key', 'display', 'file_count', 'chrome_pass_assertions', 'retained', 'gap_count', 'gaps')
                 if key in value}
                for value in report.get('ranked_groups', {}).get(kind, [])[:12]]

    print(json.dumps({'status': report['status'], 'report': str(output / 'summary.json'),
        'counts': report.get('counts'), 'gap_count': report.get('total_chrome_pass_gaps'),
        'top_sources': compact_ranking('sources'),
        'top_message_patterns': compact_ranking('message_patterns'),
        'error': report.get('error')}, sort_keys=True))
    return 0 if report['status'] == 'complete' else 1


if __name__ == '__main__':
    raise SystemExit(main())
