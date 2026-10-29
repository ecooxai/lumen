#!/usr/bin/env python3
"""Stream exact frozen1515 reference/native assertions with bounded memory.

The retained qualified Chromium reference is immutable. Only one variant from
each browser is decoded at a time; no new browser execution is performed.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import time


REFERENCE_SHA = '3480ef53ee582e96fa7ef0a2fc5eb662f136cfa7268fad635712653013bedbb6'
FROZEN = 'adcbf9719f722368b1e53ac1bd61cf38e566d3bd7495d9d6127cad38dd6aeabe'
PIN = '7f8d49b5a82606efc771f43d10aad5bce5ccbfeb'
BASELINE_SHA = 'f906769431c7a25b28ba53a364ffb5027ca0dd04fa0f8accc1161407203d3b95'


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text())


def save(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    with temporary.open('w') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)
    if read(path) != value:
        raise RuntimeError('Atomic audit receipt readback differs')


class JSONStream:
    def __init__(self, stream):
        self.stream, self.buffer, self.offset, self.eof = stream, '', 0, False
        self.decoder = json.JSONDecoder()

    def more(self):
        self.buffer = self.buffer[self.offset:]
        self.offset = 0
        block = self.stream.read(256 * 1024)
        self.eof = not block
        self.buffer += block

    def peek(self):
        while True:
            while self.offset < len(self.buffer) and self.buffer[self.offset].isspace():
                self.offset += 1
            if self.offset < len(self.buffer):
                return self.buffer[self.offset]
            if self.eof:
                return ''
            self.more()

    def expect(self, character):
        if self.peek() != character:
            raise ValueError('Unexpected reference JSON delimiter; expected ' + character)
        self.offset += 1

    def value(self):
        self.peek()
        while True:
            try:
                value, end = self.decoder.raw_decode(self.buffer, self.offset)
                self.offset = end
                return value
            except json.JSONDecodeError:
                if self.eof:
                    raise
                self.more()


def reference_rows(path, metadata):
    """Decode only individual rows; retain the comparatively small metadata."""
    with Path(path).open(encoding='utf-8') as source:
        stream = JSONStream(source)
        stream.expect('{')
        keys = set()
        while stream.peek() != '}':
            key = stream.value()
            if not isinstance(key, str) or key in keys:
                raise ValueError('Duplicate or invalid reference top-level key')
            keys.add(key)
            stream.expect(':')
            if key == 'rows':
                stream.expect('[')
                while stream.peek() != ']':
                    row = stream.value()
                    if not isinstance(row, dict):
                        raise ValueError('Reference variant is not an object')
                    yield row
                    if stream.peek() != ']':
                        stream.expect(',')
                stream.expect(']')
            else:
                metadata[key] = stream.value()
            if stream.peek() != '}':
                stream.expect(',')
        stream.expect('}')
        if stream.peek() or 'rows' not in keys:
            raise ValueError('Unexpected trailing data or missing reference rows')


def index_cohort(plan_path, expected_binary, audit):
    plan_path = Path(plan_path)
    plan = read(plan_path)
    if (plan['binary']['sha256'] != expected_binary or sha(plan['binary']['path']) != expected_binary
            or plan['wptRevision'] != PIN or plan['fullFrozenScheduleSHA256'] != FROZEN
            or digest(plan['frozenSchedule']) != FROZEN or plan['fullFrozenVariantCount'] != 1515):
        raise ValueError('Frozen native cohort identity differs')
    allowed = set(audit.indexed(plan['frozenSchedule']))
    flattened = [row for group in plan['groups'].values() for row in group]
    if sorted(flattened, key=audit.key) != sorted(plan['frozenSchedule'], key=audit.key):
        raise ValueError('Native group partition changes the full denominator')
    offsets, signatures, evidence = {}, {}, {}
    hashes = {str(plan_path): sha(plan_path)}
    for name, schedule in plan['groups'].items():
        metadata_path, schedule_path, ledger = audit.group_paths(plan_path.parent, name)
        metadata = read(metadata_path)
        signature = metadata['signature']
        if (signature['engine'] != 'lumen' or signature['binary_sha256'] != expected_binary
                or signature['schedule_sha256'] != plan['groupScheduleSHA256'][name]
                or digest(schedule) != plan['groupScheduleSHA256'][name]
                or read(schedule_path) != schedule):
            raise ValueError('Native group execution signature differs: ' + name)
        wanted = set(audit.indexed(schedule))
        observed = set()
        with ledger.open('rb') as stream:
            while True:
                offset = stream.tell()
                line = stream.readline()
                if not line:
                    break
                if not line.strip():
                    continue
                row = json.loads(line)
                identity = audit.key(row)
                if identity not in wanted or identity in offsets:
                    raise ValueError('Foreign or duplicate native variant')
                offsets[identity] = (ledger, offset, name)
                observed.add(identity)
        if observed != wanted:
            raise ValueError('Native group has unrun variants: ' + name)
        audit.validate_recorded_count(metadata, list(observed), schedule, name)
        signatures[name] = signature
        evidence[name] = {'recorded': len(observed), 'resultsSHA256': sha(ledger),
            'runSHA256': sha(metadata_path), 'scheduleSHA256': sha(schedule_path)}
        for path in (metadata_path, schedule_path, ledger):
            hashes[str(path)] = sha(path)
    if set(offsets) != allowed or len(offsets) != 1515:
        raise ValueError('A complete unchanged1515 result set is required')
    return {'plan': plan, 'offsets': offsets, 'signatures': signatures,
        'evidence': evidence, 'hashes': hashes, 'planSHA256': sha(plan_path)}


def row_at(index, identity):
    path, offset, _ = index['offsets'][identity]
    with path.open('rb') as stream:
        stream.seek(offset)
        return json.loads(stream.readline())


class Counts:
    def __init__(self):
        self.recorded = self.passing = self.qualified_pass = self.measurement_errors = 0
        self.harness, self.assertions = Counter(), Counter()

    def add(self, row, audit):
        self.recorded += 1
        self.passing += audit.fully_passes(row)
        self.harness[row['status']] += 1
        self.assertions.update(check['status'] for check in row.get('subtests', []))
        self.qualified_pass += sum(check['status'] == 'PASS' for check in row.get('subtests', [])) if audit.qualified(row) else 0
        self.measurement_errors += bool(audit.evidence_error(row))

    def summary(self, scheduled):
        return {'scheduled': scheduled, 'recorded': self.recorded, 'fullyPassing': self.passing,
            'unrun': scheduled-self.recorded,
            'passPercentOfRecorded': 100*self.passing/self.recorded if self.recorded else None,
            'harnessStatuses': dict(self.harness), 'assertions': dict(self.assertions),
            'qualifiedPassingAssertions': self.qualified_pass, 'measurementErrorVariants': self.measurement_errors}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-summary', type=Path, required=True)
    parser.add_argument('--candidate-plan', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path.cwd().resolve()
    out = args.output.resolve()
    out.relative_to(root / 'out')
    out.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    utc = lambda: datetime.now(timezone.utc).isoformat()
    receipt = {'startedUTC': utc(), 'accepted': False, 'scope':
        'Complete frozen1515 priority WPT, actual native Mac versus retained qualified Linux Chrome156.0.8078.47; different platform/V8, not full official WPT or general browser parity.',
        'fullOfficialWptPassPercent': None, 'streamingVariantReadback': True, 'auditorSHA256': sha(__file__)}
    save(out / 'receipt.json', receipt)
    try:
        specification = importlib.util.spec_from_file_location('unchanged_assertion_audit', root / 'tools/apple-session-gpt6as/audit_wpt_candidate.py')
        audit = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(audit)
        build = read(args.build_summary)
        build_summary_sha = sha(args.build_summary)
        if not build.get('passed'):
            raise ValueError('The candidate build is not accepted')
        artifact = build['artifacts']['lumen']
        source_path = args.build_summary.resolve().parent / 'compiled-source-sha256.json'
        if sha(source_path) != build['sourceManifestSHA256']:
            raise ValueError('Compiled source manifest changed')
        sources = read(source_path)
        if any(sha(root / name) != value for name, value in sources.items()):
            raise ValueError('Production source differs from the final accepted build')
        current = index_cohort(args.candidate_plan, artifact['sha256'], audit)
        before = index_cohort(root / 'out/10292019r-mac/wpt-final-004/cohort/cohort.json', BASELINE_SHA, audit)
        baseline_receipt_path = root / 'out/10292019r-mac/independent-final-004/receipt.json'
        baseline_receipt = read(baseline_receipt_path)
        if (not baseline_receipt['acceptedEvidence'] or not baseline_receipt['noRegression']
                or not baseline_receipt['fullFrozenInventoryExecuted']
                or baseline_receipt['binary'] != before['plan']['binary']
                or baseline_receipt['evidence'] != before['evidence']):
            raise ValueError('Native baseline no longer matches its retained independent raw audit')
        for field in ('frozenSchedule', 'groups', 'groupScheduleSHA256'):
            if before['plan'][field] != current['plan'][field]:
                raise ValueError('Native baseline and candidate schedule differ: ' + field)
        for group in current['signatures']:
            a, b = before['signatures'][group], current['signatures'][group]
            for field in (*audit.SIGNATURE_FIELDS, 'execution_adapter', 'collector_sha256'):
                if a.get(field) != b.get(field):
                    raise ValueError('Native paired execution protocol differs: ' + group + ':' + field)
        reference_path = root / 'out/10292019r-chrome1515-reference/reference.json'
        if sha(reference_path) != REFERENCE_SHA:
            raise ValueError('Immutable qualified Chrome1515 reference changed')
        proof_dir = root / 'out/10292019r-mac/fixture-comparison1515-004'
        proof, expected = read(proof_dir / 'receipt.json'), read(proof_dir / 'expected-reference.json')
        if (not proof['accepted'] or not proof['selectedScheduleEqual'] or proof['sourceMismatches']
                or proof['errors'] or proof['sourceCount'] != 457 or proof['selectedSourceCount'] != 235
                or proof['dependencyCount'] != 222 or sha(proof_dir / 'expected-reference.json') != proof['expectedReferenceSHA256']):
            raise ValueError('Retained canonical457-file source proof is not qualified')
        upstream = root / 'output/wpt-10290244/upstream'
        if any((upstream/name).is_symlink() or sha(upstream/name) != value for name, value in expected['sourceSHA256'].items()):
            raise ValueError('Actual selected upstream source/dependency bytes changed')
        reference_metadata = {}
        counts = {name: Counts() for name in ('baseline', 'candidate', 'chrome')}
        categories = {}
        changes = {'nativeBaseline': {'lostFullyPassingVariants': [], 'newUnqualifiedHarnesses': [],
            'lostPassingAssertions': [], 'gainedPassingAssertions': []},
            'qualifiedChrome': {'lostFullyPassingVariants': [], 'newUnqualifiedHarnesses': [],
                'lostPassingAssertions': [], 'gainedPassingAssertions': []}}
        seen = set()
        for chrome in reference_rows(reference_path, reference_metadata):
            identity = audit.key(chrome)
            if identity in seen or identity not in current['offsets']:
                raise ValueError('Foreign or duplicate Chrome reference variant')
            seen.add(identity)
            native, baseline = row_at(current, identity), row_at(before, identity)
            counts['candidate'].add(native, audit)
            counts['baseline'].add(baseline, audit)
            counts['chrome'].add(chrome, audit)
            native_delta = audit.audit_rows([baseline], [native])
            chrome_delta = audit.audit_rows([chrome], [native])
            for name, delta in (('nativeBaseline', native_delta), ('qualifiedChrome', chrome_delta)):
                for field in changes[name]:
                    changes[name][field].extend(delta[field])
            group = current['offsets'][identity][2]
            category = 'encoding' if group.startswith('encoding-') else group
            if category not in categories:
                categories[category] = {'native': Counts(), 'chrome': Counts(), 'missingReferencePASS': 0,
                    'unionAssertionIdentities': 0, 'candidateQualifiedPassInUnion': 0, 'harnessesHealthy': True}
            item = categories[category]
            item['native'].add(native, audit)
            item['chrome'].add(chrome, audit)
            item['missingReferencePASS'] += len(chrome_delta['lostPassingAssertions'])
            am, bm = audit.assertions(native), audit.assertions(chrome)
            names = set(am) | set(bm)
            item['unionAssertionIdentities'] += len(names)
            item['candidateQualifiedPassInUnion'] += sum(audit.assertion_status(native, name, am) == 'PASS' for name in names)
            item['harnessesHealthy'] &= audit.qualified(native) and audit.qualified(chrome) and bool(am) and bool(bm)
        if len(seen) != 1515 or seen != set(current['offsets']):
            raise ValueError('Full qualified Chrome reference denominator differs')
        reference = reference_metadata
        if (not reference['acceptedReference'] or reference['fullFrozenVariantCount'] != 1515
                or reference['all1515ImmutableCaseCount'] != 1515 or reference['frozenScheduleSHA256'] != FROZEN
                or reference['groups'] != current['plan']['groups']
                or reference['groupScheduleSHA256'] != current['plan']['groupScheduleSHA256']
                or reference['sourceProofSHA256'] != proof['expectedReferenceSHA256']
                or reference['sourceProof'] != expected):
            raise ValueError('Qualified reference metadata, canonical selected source or schedule differs')
        rp = reference['protocol']
        for group, signature in current['signatures'].items():
            for field in ('wpt_revision', 'reporter_sha256', 'monitor_sha256', 'origin', 'secure_origin',
                          'h2_origin', 'timeout_s', 'long_timeout_s'):
                if signature.get(field) != rp.get(field):
                    raise ValueError('Chrome/native paired protocol differs: ' + group + ':' + field)
            if signature['runner_sha256'] != rp['collector_sha256']:
                raise ValueError('The shared collector differs')
            for field in ('wpt_revision', 'tracked_upstream_sources_clean'):
                if signature['source_fixture'].get(field) != rp['source_fixture'].get(field):
                    raise ValueError('Pinned source cleanliness differs')
        summaries = {name: count.summary(1515) for name, count in counts.items()}
        for key, value in reference['summary'].items():
            if summaries['chrome'].get(key) != value:
                raise ValueError('Recomputed Chrome raw summary differs: ' + key)
        if summaries['baseline']['fullyPassing'] != 1454 or summaries['baseline']['assertions'] != {'PASS': 1166858, 'FAIL': 7}:
            raise ValueError('Recomputed native004 baseline differs')
        if summaries['baseline'] != baseline_receipt['summary']:
            raise ValueError('Recomputed native004 raw summary differs from its retained independent audit')
        if any(sha(path) != value for index in (before, current) for path, value in index['hashes'].items()):
            raise ValueError('Native evidence changed during independent readback')
        if sha(reference_path) != REFERENCE_SHA or sha(root/artifact['path']) != artifact['sha256']:
            raise ValueError('Qualified reference or native artifact changed during audit')
        if (sha(args.build_summary) != build_summary_sha or sha(source_path) != build['sourceManifestSHA256']
                or any(sha(root/name) != value for name, value in sources.items())):
            raise ValueError('Compiled source or build attestation changed during audit')
        if any((upstream/name).is_symlink() or sha(upstream/name) != value for name, value in expected['sourceSHA256'].items()):
            raise ValueError('Canonical selected source/dependency bytes changed during audit')
        category_output = {}
        for name, item in categories.items():
            total = item['native'].recorded
            native, chrome = item['native'].summary(total), item['chrome'].summary(total)
            missing = item['missingReferencePASS']
            union = item['unionAssertionIdentities']
            category_output[name] = {'scheduled': total, 'native': native, 'chrome': chrome,
                'chromePassingAssertionsRetained': chrome['qualifiedPassingAssertions']-missing,
                'chromePassingAssertionsTotal': chrome['qualifiedPassingAssertions'],
                'chromePassingAssertionsMissing': missing,
                'harnessesHealthy': item['harnessesHealthy'], 'unionAssertionIdentities': union,
                'candidateQualifiedPassInUnion': item['candidateQualifiedPassInUnion'],
                'proportionalPassRatio': item['candidateQualifiedPassInUnion']/union if union else None,
                'fixedCategoryWeight': 1/7, 'scoreAssigned': None}
        for name, delta in changes.items():
            save(out/(name+'-assertion-differences.json'), delta)
        delta = changes['nativeBaseline']
        no_regression = not (delta['lostFullyPassingVariants'] or delta['newUnqualifiedHarnesses'] or delta['lostPassingAssertions'])
        missing = len(changes['qualifiedChrome']['lostPassingAssertions'])
        missing_context = Counter('ServiceWorker' if '.serviceworker.' in row['test'] else
            'SharedWorker' if '.sharedworker.' in row['test'] else 'other' for row in changes['qualifiedChrome']['lostPassingAssertions'])
        measurement_valid = summaries['candidate']['measurementErrorVariants'] == 0
        receipt.update(accepted=no_regression and measurement_valid, noRegression=no_regression,
            measurementEvidenceValid=measurement_valid, sourceCommit=build['reviewedProductSource'],
            sourceBuildCommit=build['sourceBaseCommit'], sourceManifestSHA256=build['sourceManifestSHA256'],
            buildSummarySHA256=build_summary_sha, baselineIndependentReceiptSHA256=sha(baseline_receipt_path),
            compiledSourceRecheckedAfter=True, canonicalSelectedSourceRecheckedAfter=True,
            binary=artifact, native=summaries['candidate'],
            baseline=summaries['baseline'], chrome=summaries['chrome'], categories=category_output,
            all1515Recorded=True, frozenScheduleSHA256=FROZEN, wptRevision=PIN,
            nativeCohortSHA256=current['planSHA256'], baselineCohortSHA256=before['planSHA256'],
            nativeEvidence=current['evidence'], baselineEvidence=before['evidence'],
            chromeReferenceSHA256=REFERENCE_SHA, referenceBinary=reference['binary'], referenceVersion=reference['version'],
            canonicalSelectedSourceFiles=457, canonicalSourceProofSHA256=proof['expectedReferenceSHA256'],
            lostBaselinePassingAssertions=len(delta['lostPassingAssertions']),
            gainedBaselinePassingAssertions=len(delta['gainedPassingAssertions']),
            gainedFullyPassingVariants=summaries['candidate']['fullyPassing']-summaries['baseline']['fullyPassing'],
            chromePassingAssertionsTotal=summaries['chrome']['qualifiedPassingAssertions'],
            chromePassingAssertionsRetained=summaries['chrome']['qualifiedPassingAssertions']-missing,
            chromePassingAssertionsMissing=missing, missingReferencePASSByContext=dict(missing_context),
            selected98PercentGoalReached=100*summaries['candidate']['fullyPassing']/1515 >= 98)
    except Exception as failure:
        receipt['error'] = type(failure).__name__ + ': ' + str(failure)
    receipt.update(finishedUTC=utc(), durationSeconds=round(time.monotonic()-started, 6))
    save(out/'receipt.json', receipt)
    print(json.dumps({key: receipt.get(key) for key in ('accepted', 'noRegression', 'native',
        'chromePassingAssertionsRetained', 'chromePassingAssertionsTotal', 'chromePassingAssertionsMissing',
        'missingReferencePASSByContext', 'gainedFullyPassingVariants', 'selected98PercentGoalReached',
        'durationSeconds', 'error')}), flush=True)
    return 0 if receipt['accepted'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
