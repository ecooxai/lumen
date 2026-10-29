"""Build public, artifact-specific WPT facts from verified local result ledgers."""
from collections import Counter
from datetime import datetime
from pathlib import Path
import hashlib
import json

FROZEN_COUNT = 1515
FROZEN_SHA = 'adcbf9719f722368b1e53ac1bd61cf38e566d3bd7495d9d6127cad38dd6aeabe'

def _read(path):
    return json.loads(path.read_text())

def _sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def _passing(row):
    return row.get('status') in ('OK', 'PASS') and bool(row.get('subtests')) and all(item['status'] == 'PASS' for item in row['subtests'])

def _rows(directory, expected, schedule, binary, schedule_sha):
    metadata = _read(directory / 'run.json')
    signature = metadata['signature']
    if signature['binary_sha256'] != binary or signature['schedule_sha256'] != schedule_sha or _read(directory / 'schedule.json') != schedule:
        raise ValueError('Run identity or scheduled variants differ')
    # A writer may have appended a later group result since priority-summary.json
    # was published. Only its declared stable prefix belongs to this snapshot.
    lines = (directory / 'results.jsonl').read_text().splitlines()
    if len(lines) < expected:
        raise ValueError('Summary references missing result rows')
    rows = [json.loads(line) for line in lines[:expected]]
    names = [row['test'] for row in rows]
    if len(set(names)) != len(names) or not set(names).issubset({row['test'] for row in schedule}):
        raise ValueError('Duplicate or unscheduled result rows')
    for row in rows:
        if (row.get('metrics') or {}).get('errors') or row.get('telemetry_errors') or (row.get('cleanup') or {}).get('error') or (row.get('cleanup') or {}).get('profile_error'):
            raise ValueError('Measurement or cleanup evidence is incomplete')
    return rows

def _cohort(root, path, accepted):
    summary = _read(path)
    plan_path = path.parent / 'cohort.json'
    plan = _read(plan_path)
    binary = plan['binary']['sha256']
    if summary['cohortSHA256'] != _sha(plan_path) or summary['binarySHA256'] != binary or plan['fullFrozenVariantCount'] != FROZEN_COUNT or plan['fullFrozenScheduleSHA256'] != FROZEN_SHA:
        raise ValueError('Cohort or frozen denominator identity differs')
    if sum(len(rows) for rows in plan['groups'].values()) != FROZEN_COUNT:
        raise ValueError('Cohort omitted frozen variants')
    datetime.fromisoformat(summary['updatedUTC'].replace('Z', '+00:00'))
    rows = []
    groups = {}
    for name, schedule in plan['groups'].items():
        value = summary['groups'][name]
        count = value['run']
        if type(count) is not int or not 0 <= count <= len(schedule) or value['scheduled'] != len(schedule) or value['unrun'] != len(schedule) - count:
            raise ValueError('Group denominator or progress differs')
        observed = _rows(path.parent / name, count, schedule, binary, plan['groupScheduleSHA256'][name]) if count else []
        fully = sum(bool(_passing(row)) for row in observed)
        if fully != value['fullyPassing']:
            raise ValueError('Group pass count differs from its raw ledger')
        groups[name] = {'scheduled': len(schedule), 'recorded': count, 'fullyPassing': fully, 'unrun': len(schedule) - count}
        rows.extend(observed)
    count = len(rows)
    fully = sum(bool(_passing(row)) for row in rows)
    assertions = dict(Counter(item['status'] for row in rows for item in row.get('subtests', [])))
    harnesses = dict(Counter(row['status'] for row in rows))
    if summary['scheduled'] != FROZEN_COUNT or summary['run'] != count or summary['fullyPassing'] != fully or summary['unrun'] != FROZEN_COUNT - count or summary['assertions'] != assertions or summary['harnessStatuses'] != harnesses:
        raise ValueError('Summary totals differ from its raw ledger')
    return {'cohort': path.parent.name, 'binarySha256': binary, 'acceptedArtifact': binary == accepted, 'updatedUTC': summary['updatedUTC'], 'scheduled': FROZEN_COUNT, 'recorded': count, 'fullyPassing': fully, 'unrun': FROZEN_COUNT - count, 'passingPercentOfRun': round(100 * fully / count, 4) if count else None, 'passingCoveragePercent': round(100 * fully / FROZEN_COUNT, 4), 'assertions': assertions, 'harnessStatuses': harnesses, 'groups': groups, 'source': str(path.relative_to(root)), 'sourceSHA256': _sha(path), 'cohortSHA256': summary['cohortSHA256'], 'measurementQualified': True, 'scope': 'One native artifact only; missing variants stay scheduled. Full official WPT rate is unknown.'}

def _reference(root, base, qualified):
    directory = base / 'chrome-qualified-streams-1'
    if not qualified or not (directory / 'paired-identity.json').is_file():
        return None
    identity = _read(directory / 'paired-identity.json')
    if identity['referenceBinarySHA256'] != qualified['binarySHA256'] or identity['referenceQualificationSHA256'] != qualified['evidenceSHA256'] or identity['frozenScheduledVariants'] != FROZEN_COUNT:
        raise ValueError('Qualified reference identity differs')
    plan_path = root / identity['nativeCohort']
    plan = _read(plan_path)
    if _sha(plan_path) != identity['nativeCohortSHA256'] or plan['binary']['sha256'] != identity['nativeBinarySHA256'] or plan['fullFrozenScheduleSHA256'] != FROZEN_SHA:
        raise ValueError('Reference input cohort changed')
    group = identity['group']
    schedule = plan['groups'][group]
    invocations = sorted(directory.glob('paired-invocation-*.json'))
    if not invocations:
        raise ValueError('Qualified reference execution receipt missing')
    receipt = _read(invocations[-1])
    if not receipt.get('measurementQualified') or not receipt.get('passed') or receipt.get('resourceErrorCases') or receipt.get('recorded') != len(schedule):
        raise ValueError('Qualified reference execution incomplete')
    rows = _rows(directory, len(schedule), schedule, qualified['binarySHA256'], identity['groupScheduleSHA256'])
    fully = sum(bool(_passing(row)) for row in rows)
    if fully != receipt['rawFullyPassing']:
        raise ValueError('Reference raw pass count differs')
    return {'binarySha256': qualified['binarySHA256'], 'group': group, 'recorded': len(rows), 'scheduled': len(schedule), 'fullyPassing': fully, 'assertions': dict(Counter(item['status'] for row in rows for item in row.get('subtests', []))), 'harnessStatuses': dict(Counter(row['status'] for row in rows)), 'source': str(directory.relative_to(root)), 'measurementQualified': True, 'scope': 'Qualified signed Chrome on this exact group; actual reference failures remain failures. This is not a full-WPT result.'}

def build_candidate_catalog(root, task, accepted, qualified=None):
    root = Path(root)
    base = root / ('output/session-' + task)
    cohorts = []
    errors = []
    for path in sorted(base.glob('wpt-*-native-*/priority-summary.json')):
        try:
            cohorts.append(_cohort(root, path, accepted))
        except (OSError, ValueError, KeyError, TypeError) as error:
            errors.append({'cohort': path.parent.name, 'error': str(error)[:180]})
    cohorts.sort(key=lambda value: value['updatedUTC'], reverse=True)
    reference = None
    try:
        reference = _reference(root, base, qualified)
    except (OSError, ValueError, KeyError, TypeError) as error:
        errors.append({'cohort': 'qualified-chrome-streams', 'error': str(error)[:180]})
    return {'schema': 1, 'frozenScheduledVariants': FROZEN_COUNT, 'fullOfficialWptPassRate': None, 'acceptedNativeBinarySha256': accepted, 'latestMeasuredCohort': next((value for value in cohorts if value['recorded']), None), 'cohorts': cohorts, 'qualifiedChrome': reference, 'validationErrors': errors, 'policy': 'Never combine variants or assertions across native artifact hashes. Raw errors, timeouts and unrun variants retain their status.'}
