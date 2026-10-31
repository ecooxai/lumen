#!/usr/bin/env python3
"""Read-only audit of the pinned, bounded document lifecycle contract.
No browser, source mutation, protected helper inspection, or result rewriting.
Exit 0: observable contract passes; 1: semantic failure; 2: evidence failure.
The durable fixture separately proves its recorded case-finish timing boundary.
"""
from pathlib import Path
import argparse, collections, datetime, hashlib, json, re, sys

ROOT = Path('/mnt/p3/project/browser/lumen-android/lumen-android')
QA = ROOT / 'agent/validation/linux-wpt-20261031-session9/qa-document-lifecycle'
CONTRACT_SHA = '7044502e5d3974e2ce7b67e30cf18907c17c28731f9b6f713c316940ac1f6c82'
FIXTURE_SHA = '58383de5678ccd3964da0e97f6cc04089a428dcab01cff1a711875f51480a332'
MANIFEST_SHA = '85a33adc567ac3bf609366059577d58d03438136ed039480f3d4a66dfd8abdb4'
V4_CONTROLLER_SHA = '02a3e4b177208d73cb069afd43516a7a505e3fa344bb6c2fb85f28dac318670d'
BASE_SOURCE = '554d59472954585bfd39c579ee122f9f21194847'
BASE_JSRUN = '00435d64f63d928ceac27691ab1f28f533e4002a80ee728e0787df5b9c4d35fe'
CHROME_SHA = '1bcad71ad1bff85955db13c6d8dd9f32374934d5a5cb079b526c46fb51f9bd97'
DRIVER_SHA = 'd7f1682dcf9e4087bae6a52ca03c9913390a79ab86e09796562db7ed6f534587'
ADAPTER_SHA = '07648d03d652201057f91bdd514f8a90ab62d9b0e4b5ba6e80a8bc32ca98bf27'

def require(condition, message):
    if not condition:
        raise ValueError(message)

def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'Duplicate JSON object key: ' + key)
        result[key] = value
    return result

def loads(text):
    def bad(value):
        raise ValueError('Non-finite JSON value: ' + value)
    return json.loads(text, object_pairs_hook=unique_object, parse_constant=bad)

def read(path):
    return loads(Path(path).read_text())

def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False)

def same(a, b):
    return encoded(a) == encoded(b)

def plain(row):
    return {'kind': row['kind'],
            'value': {key: value for key, value in row['value'].items() if key != 'sequence'}}

def parse_raw(raw, case_ids):
    cases, rows, summaries = [], [], []
    for line in raw.splitlines():
        for prefix, target in [('LIFECYCLE_CASE ', cases), ('LIFECYCLE_ROW ', rows),
                               ('MEASUREMENTS document-lifecycle ', summaries)]:
            if prefix in line:
                message = line[line.index(prefix):]
                require(len(message.encode('utf-8')) < 1900, 'Oversized/truncated console row')
                target.append(loads(message[len(prefix):]))
    require('LIFECYCLE_FATAL ' not in raw, 'Fatal lifecycle execution')
    require(re.findall(r'RESULTS document-lifecycle:(PASS|FAIL)', raw) == ['PASS'],
            'Exactly one complete transport marker required')
    require(same(summaries, [{'cases': 12, 'finished': 12, 'complete': True}]),
            'Twelve-case completion summary missing')
    require(len(cases) == 12 and [c.get('id') for c in cases] == case_ids,
            'Ordered twelve-case identity mismatch')
    for index, case in enumerate(cases):
        require(set(case) == {'index', 'id', 'finished', 'eventCount', 'actionCount', 'errorCount'},
                'Unexpected case fields')
        require(type(case['index']) is int and case['index'] == index and case['finished'] is True,
                'Incomplete or misindexed case')
        require(all(type(case[key]) is int and case[key] >= 0
                    for key in ['eventCount', 'actionCount', 'errorCount']), 'Invalid case counts')
    for row in rows:
        require(set(row) == {'caseIndex', 'caseId', 'kind', 'index', 'value'}, 'Unexpected row fields')
        index = row['caseIndex']
        require(type(index) is int and 0 <= index < 12 and row['caseId'] == case_ids[index],
                'Unknown or misindexed row identity')
        require(row['kind'] in ['events', 'actions', 'errors'] and type(row['index']) is int
                and row['index'] >= 0 and type(row['value']) is dict, 'Malformed observation row')
        require(type(row['value'].get('sequence')) is int and row['value']['sequence'] >= 0,
                'Missing chronological index')
    for index, case in enumerate(cases):
        selected = [row for row in rows if row['caseIndex'] == index]
        total = case['eventCount'] + case['actionCount'] + case['errorCount']
        require(len(selected) == total, 'Case declared count differs')
        require(sorted(row['value']['sequence'] for row in selected) == list(range(total)),
                'Duplicate/missing chronological position')
        for kind, count_key in [('events', 'eventCount'), ('actions', 'actionCount'),
                                ('errors', 'errorCount')]:
            require([row['index'] for row in selected if row['kind'] == kind]
                    == list(range(case[count_key])), 'Per-kind row identity mismatch')
    return {'cases': cases, 'rows': rows, 'summary': summaries}

def evidence(directory, controller_sha, candidate, case_ids, reference=False, current=False):
    directory = Path(directory).resolve()
    require(directory.is_relative_to(QA), 'Evidence outside the owned QA directory')
    receipt, launch = read(directory / 'receipt.json'), read(directory / 'launch.json')
    before, after = read(directory / 'pins-before.json'), read(directory / 'pins-after.json')
    require(same(before, after) and before, 'Before/after pins differ')
    mutable_default = ROOT / 'build/codex-native/jsrun'
    archived_skips = []
    for name, expected in before.items():
        path = Path(name)
        require(path.is_absolute() and path.resolve().is_relative_to(ROOT),
                'Pin outside project scope')
        if path == mutable_default:
            archived_skips.append(name)
            continue
        require(path.is_file() and sha(path) == expected, 'Immutable/public pin changed: ' + name)
    controller_paths = [name for name in before
                        if Path(name).parent == QA and Path(name).name.startswith('run-matrix')
                        and name.endswith('.py')]
    require(len(controller_paths) == 1 and before[controller_paths[0]] == controller_sha,
            'Reviewed ordinary controller identity differs')
    require(before.get(str(QA / 'document-lifecycle-matrix-v4.html')) == FIXTURE_SHA
            and before.get(str(QA / 'matrix-manifest-v4.json')) == MANIFEST_SHA,
            'Fixture/manifest run pins differ')
    build = ROOT / ('agent/validation/iteration-' + str(candidate))
    for name in ['build-state.json', 'source-manifest-before.json', 'source-manifest-after.json', 'build.log']:
        require(str(build / name) in before, 'Missing archived build provenance: ' + name)
    state, source = read(build / 'build-state.json'), read(build / 'source-manifest-before.json')
    require(same(source, read(build / 'source-manifest-after.json')), 'Sealed build manifests differ')
    require(state['build_exit_code'] == state['startup_exit_code'] == 0
            and state['source_unchanged_during_build'] is True, 'Unsuccessful/changed build')
    require(state['source_manifest_before'] == state['source_manifest_after'] == source['sha256'],
            'Sealed source aggregate mismatch')
    require(receipt['source_commit'] == launch['source_commit'] == state['head'],
            'Build/launch/receipt source mismatch')
    require(type(source['files']) is dict and len(source['files']) == receipt['recorded_input_count'],
            'Input-count mismatch')
    if current:
        require(all(sha(ROOT / name) == expected for name, expected in source['files'].items()),
                'Current candidate source differs from its sealed inputs')
    for flag in ['measurement_integrity_valid', 'cleanup_valid', 'transport_complete',
                 'pins_unchanged', 'source_unchanged', 'git_unchanged']:
        require(receipt.get(flag) is True, 'Invalid execution flag: ' + flag)
    require(receipt['actual_inner_returncode'] == 0 and receipt['monitor_returncodes'] == [0, 0]
            and receipt['monitor_forced_stop'] is False and receipt['timed_out'] is False
            and receipt['outer_cleanup_signals'] == [] and receipt['errors'] == [],
            'Execution, monitor, or cleanup failure')
    require(receipt['fixture_sha256'] == launch['fixture_sha256'] == FIXTURE_SHA
            and receipt['manifest_sha256'] == launch['manifest_sha256'] == MANIFEST_SHA,
            'Receipt fixture identity mismatch')
    require(receipt['workspace_candidate'] == launch['workspace_candidate'] == candidate,
            'Workspace candidate mismatch')
    require(launch['resource_interval_seconds'] == 1 and launch['maximum_browser_lanes'] == 1,
            'Unexpected resource/launch schedule')
    resource_files = {}
    for stem in ['resources', 'cgroup']:
        summary_path, csv_path = directory / (stem + '.summary.json'), directory / (stem + '.csv')
        summary = read(summary_path)
        require(summary['interval_seconds'] == 1 and type(summary['samples']) is int
                and summary['samples'] >= 1, 'Missing one-second monitor coverage')
        require(len(csv_path.read_text().splitlines()) >= 2, 'Empty monitor CSV')
        resource_files[stem] = {'summary_sha256': sha(summary_path), 'csv_sha256': sha(csv_path),
                                'samples': summary['samples'], 'interval_seconds': 1}
    if reference:
        cache = ROOT / 'build/references/chrome-for-testing-156.0.8078.80'
        adapter = ROOT / 'agent/validation/linux-wpt-20261030-session7/qa-script-scheduling/authored/run-cft156-fixture.py'
        for path, expected in [(cache / 'chrome-linux64/chrome', CHROME_SHA),
                               (cache / 'chromedriver-linux64/chromedriver', DRIVER_SHA),
                               (adapter, ADAPTER_SHA)]:
            require(before.get(str(path)) == expected, 'Pinned reference component differs')
        result = read(directory / 'browser/result.json')
        owned = result.get('owned_process_cleanup', {})
        require(result.get('status') == 'PASS'
                and result.get('marker') == 'RESULTS document-lifecycle:PASS'
                and result.get('profile_removed') is True and result.get('fixture_unchanged') is True
                and bool(owned.get('recorded_identities')) and owned.get('remaining_same_identity') == []
                and owned.get('signals') == [] and not result.get('driver_cleanup_error')
                and not result.get('error'), 'Reference owned cleanup/completion failed')
        raw = '\n'.join(result['console'])
        require(receipt['binary_sha256'] == launch['binary_sha256'] == CHROME_SHA,
                'Reference browser hash differs')
        require(receipt['exit_evidence']['adapter_actual_returncode'] == 0,
                'Reference adapter did not actually exit zero')
    else:
        binary = ROOT / ('build/codex-native/jsrun-astra-i' + str(candidate))
        require(before.get(str(binary)) == before.get(str(mutable_default))
                == receipt['binary_sha256'] == launch['binary_sha256'] == sha(binary),
                'Immutable/current-at-run native binary linkage differs')
        require('Linking CXX executable jsrun' in (build / 'build.log').read_text(),
                'Native test host link absent from recorded build')
        require(launch['native_environment'] == {'JSRUN_LAYOUT': '1',
                'JSRUN_UNTIL_ATTR': 'data-document-lifecycle-complete', 'LUMEN_CONSOLE': '0'},
                'Unexpected native host environment')
        require(receipt['exit_evidence']['native_jsrun_actual_returncode'] == 0,
                'Native test host did not actually exit zero')
        raw = (directory / 'controller.log').read_text()
    reconstructed = parse_raw(raw, case_ids)
    require(same(reconstructed, read(directory / 'observations.json')), 'Raw/result reconstruction differs')
    require(receipt['case_count'] == 12 and receipt['observation_row_count'] == len(reconstructed['rows'])
            and receipt['recorded_case_errors'] == sum(c['errorCount'] for c in reconstructed['cases']),
            'Receipt counts differ from raw rows')
    return reconstructed, {
        'directory': str(directory), 'source_commit': receipt['source_commit'],
        'binary_sha256': receipt['binary_sha256'], 'recorded_input_count': len(source['files']),
        'receipt_sha256': sha(directory / 'receipt.json'),
        'observations_sha256': sha(directory / 'observations.json'),
        'launch_sha256': sha(directory / 'launch.json'),
        'pins_before_sha256': sha(directory / 'pins-before.json'),
        'pins_after_sha256': sha(directory / 'pins-after.json'),
        'raw_console_sha256': hashlib.sha256(raw.encode('utf-8')).hexdigest(),
        'archived_mutable_paths_not_rehashed': archived_skips,
        'archived_build_pins_rehashed': True, 'current_source_rehashed': current,
        'exit_evidence': receipt['exit_evidence'], 'resource_evidence': resource_files,
        'measurement_integrity_valid': True
    }

def compare(observations, contract):
    reports = []
    for case_index, expected in enumerate(contract['cases']):
        rows = sorted([r for r in observations['rows'] if r['caseIndex'] == case_index],
                      key=lambda r: r['value']['sequence'])
        canonical, moved, optional, errors = [], [], [], []
        micro_checks = expected['initial_load_microtask_checks']
        optionals = expected['optional_exact_post_removal_records']
        for row in rows:
            record = plain(row)
            if row['kind'] == 'errors':
                errors.append(record['value'])
            elif micro_checks and row['kind'] == 'actions' and row['value'].get('action') == 'load-microtask' and row['value'].get('generation') == 'initial':
                moved.append(row)
            elif any(same(record, allowed) for allowed in optionals):
                optional.append(row)
            else:
                canonical.append(record)
        failures = []
        if not same(canonical, expected['canonical_records']):
            failures.append('Exact canonical event/action sequence differs')
        if not same(errors, expected['preserved_v4_wait_assumptions']):
            failures.append('Known observation-wait rows differ or new case errors appeared')
        if len(micro_checks) > 1:
            failures.append('Unreviewed multi-microtask allowance')
        if micro_checks:
            check = micro_checks[0]
            if len(moved) != check['exact_count']:
                failures.append('Initial-load microtask count differs')
            for row in moved:
                value = plain(row)['value']
                if not any(same(value, allowed) for allowed in check['allowed_values']):
                    failures.append('Initial-load microtask contains an unapproved value')
                matching = [r['value']['sequence'] for r in rows if r['kind'] == 'events'
                            and all(r['value'].get(k) == v for k, v in check['must_follow_event'].items())]
                if not matching or row['value']['sequence'] <= max(matching):
                    failures.append('Initial-load microtask precedes the last matching load callback')
        optional_indices = []
        before_remove = [r['value']['sequence'] for r in rows
                         if r['kind'] == 'actions' and r['value'].get('action') == 'before-remove']
        for row in optional:
            index = next(i for i, allowed in enumerate(optionals) if same(plain(row), allowed))
            optional_indices.append(index)
            if len(before_remove) != 1 or row['value']['sequence'] <= before_remove[0]:
                failures.append('Optional removal-author row precedes removal')
        if optional_indices != sorted(set(optional_indices)):
            failures.append('Optional removal-author rows duplicate or reverse observed order')
        reports.append({
            'id': expected['id'], 'passed': not failures, 'failures': failures,
            'expected_canonical_records': len(expected['canonical_records']),
            'actual_canonical_records': len(canonical),
            'exact_canonical_sequence': same(canonical, expected['canonical_records']),
            'observed_initial_load_microtasks': [r['value'] for r in moved],
            'observed_optional_removal_author_rows': [r['value'] for r in optional],
            'known_observation_wait_rows': errors,
            'canonical_expected': expected['canonical_records'] if failures else None,
            'canonical_actual': canonical if failures else None
        })
    return {'passed': all(case['passed'] for case in reports),
            'case_count': len(reports), 'canonical_records_required': sum(len(c['canonical_records']) for c in contract['cases']),
            'cases': reports}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', type=int, required=True)
    parser.add_argument('--run', type=Path, required=True)
    parser.add_argument('--controller-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--check-current-source', action='store_true')
    args = parser.parse_args()
    require(args.candidate >= 232, 'Candidate must follow the sealed231 baseline')
    require(re.fullmatch('[0-9a-f]{64}', args.controller_sha256) is not None, 'Invalid controller hash')
    require(not args.output.exists(), 'Fresh audit output required')
    report = {'auditor_sha256': sha(__file__),
              'reviewed_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'candidate': args.candidate, 'contract_sha256': CONTRACT_SHA,
              'not_WPT': True, 'full_trace_equivalence_claimed': False,
              'durable_finish_boundary_gate_required': True,
              'v4_timing_observability_limit': 'v4 has final finished:true but no per-case finish sequence; the separate durable fixture must prove zero records after its explicit finish boundary.',
              'outer_terminal_gate': 'The coordinator must separately retain actual completion of the outer scheduled command.',
              'resource_limitations': 'One-second execution-tree and shared cgroup coverage; reference tree includes ChromeDriver. This audit makes no performance claim.'}
    code = 2
    try:
        require(sha(QA / 'acceptance-contract-v1.json') == CONTRACT_SHA, 'Approved contract changed')
        contract = read(QA / 'acceptance-contract-v1.json')
        case_ids = [case['id'] for case in contract['cases']]
        require(len(case_ids) == len(set(case_ids)) == contract['case_count'] == 12, 'Contract case identities differ')
        require(sha(ROOT / contract['reference_observations']) == contract['reference_sha256']
                and sha(ROOT / contract['baseline_observations']) == contract['baseline_sha256'],
                'Archived observations changed')
        reference, reference_proof = evidence(QA / 'chromium156-v4', V4_CONTROLLER_SHA, 231, case_ids, reference=True)
        baseline, baseline_proof = evidence(QA / 'jsrun231-v4', V4_CONTROLLER_SHA, 231, case_ids)
        require(reference_proof['source_commit'] == baseline_proof['source_commit'] == BASE_SOURCE
                and baseline_proof['binary_sha256'] == BASE_JSRUN, 'Archived source/binary identity differs')
        ref_result = compare(reference, contract)
        require(ref_result['passed'] and ref_result['canonical_records_required'] == 114,
                'Pinned reference does not satisfy the exact approved contract')
        candidate, candidate_proof = evidence(args.run, args.controller_sha256, args.candidate,
                                               case_ids, current=args.check_current_source)
        candidate_result = compare(candidate, contract)
        report.update({'evidence_integrity_valid': True, 'reference': reference_proof,
                       'baseline': baseline_proof, 'candidate_evidence': candidate_proof,
                       'reference_contract': ref_result, 'baseline_contract': compare(baseline, contract),
                       'candidate_contract': candidate_result,
                       'observable_bounded_contract_passed': candidate_result['passed']})
        code = 0 if candidate_result['passed'] else 1
    except Exception as error:
        report.update({'evidence_integrity_valid': False,
                       'observable_bounded_contract_passed': False, 'error': type(error).__name__ + ': ' + str(error)})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    print(json.dumps({'output': str(args.output), 'sha256': sha(args.output),
                      'evidence_integrity_valid': report['evidence_integrity_valid'],
                      'observable_bounded_contract_passed': report['observable_bounded_contract_passed'],
                      'exit_code': code}))
    return code

if __name__ == '__main__':
    sys.exit(main())

