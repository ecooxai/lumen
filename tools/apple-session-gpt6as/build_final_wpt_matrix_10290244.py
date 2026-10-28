#!/usr/bin/env python3
"""Build the final source-bound WPT matrix without running or modifying tests."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
from datetime import datetime, timezone

ROOT = Path.cwd()
BASE = ROOT / 'output/wpt-10290244'
PLAN = BASE / 'priority-1b9d-v1/cohort.json'
PLAN_SHA = '6720f796928e5a83d64c76a4e751e0da672f14f124c04792a97319485170d65d'
BINARY_SHA = '1b9d43ed19cd616b0029e34fde97050b86c9acbdbda16a0dca5892922df704e9'
SOURCE_COMMIT = '436916ceb3b32630ed251bb666b02fa39a3cd0c8'
PIN = '7f8d49b5a82606efc771f43d10aad5bce5ccbfeb'
SUPPLEMENT = BASE / 'priority-1b9d-v1-headers-supplemental-1'
SUPPLEMENT_SHA = '6bd1483c10336f701501333530520443eca80658f879b4e6066222a2f980a7d6'
NAME = 'gpt6-astra-pro_builtin-agents_devinmac3d_wpt_matrix_10290244'
OUT = ROOT / 'agent'
WRAPPER = ROOT / 'tools/apple-session-gpt6as/run_priority_wpt_10290244.py'
WRAPPER_SHA = 'b2395c8e7b00e4081924025d765cdb5386de19cd6eddb0f4178c4a8c6d6e2987'
# Frozen, observed end-of-session counts: scheduled, recorded, fully passing,
# PASS subtests, FAIL subtests, harness ERROR, harness TIMEOUT.
EXPECTED = {
    'dom': (14, 14, 13, 995, 1, 0, 0),
    'events': (16, 16, 16, 78, 0, 0, 0),
    'url': (77, 77, 76, 12316, 7, 0, 0),
    'encoding': (1339, 31, 16, 7264, 57, 2, 5),
    'headers': (52, 52, 24, 499, 2, 13, 13),
    'microtasks': (10, 10, 6, 15, 0, 2, 2),
    'storage': (7, 7, 7, 1152, 0, 0, 0),
}
FILES = {
    'tools/apple-session-gpt6as/remote_priority_batch_10290244.py': '1114c72ce92e310d107f8152bdde9273b5a640224d9db3338ac96a214be5b104',
    'tools/apple-session-gpt6as/remote_headers_supplemental_10290244.py': '4a39a2422fc72bb73e57005cff04f11f260828508253f35cb351d7cfd6792fd0',
    'tools/apple-session-gpt6as/remote_final_resource_batch_10290244.py': 'fe1753eef6a4223bf6ff79b30c3a2d4ac01914a00b34aa24831eab106680f006',
    'tools/apple-session-gpt6as/update_priority_progress_10290244.py': 'e9f406b46cdc70b39059537a6e747e1bc03b820872df59285ff9fe2ceb9a0f79',
    'tools/apple-session-gpt6as/build_delivery_receipt_10290244.py': '8c941b0fd3535da4216022177b8f856c87f5d0cbd70bbbfc5050b98aa57e3dc3',
    'agent/gpt6-astra-pro_builtin-agents_devinmac3d_runtime_followup_10290244.md': 'b25de8aa933721b6b43b86461f32e06dffba1d745b795c1276bb39347d16fd6d',
}


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def need(condition, message):
    if not condition:
        raise RuntimeError(message)


def read(path):
    return json.loads(Path(path).read_text())


def row_hash(row):
    return hashlib.sha256(json.dumps(row, sort_keys=True).encode()).hexdigest()


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_suffix(path.suffix + '.tmp')
    pending.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')
    pending.replace(path)


def evidence(path):
    return {'path': str(path.relative_to(ROOT)), 'sha256': sha(path)}


def compact(summary):
    return {key: summary[key] for key in (
        'scheduled_files', 'recorded_files', 'remaining_files',
        'files_passing_all_assertions', 'file_outcomes', 'subtest_outcomes',
        'zero_assertion_testharness_files', 'executed_subtests_excluding_notrun')}


def check_summary(summary, expected, label):
    actual = (
        summary['scheduled_files'], summary['recorded_files'],
        summary['files_passing_all_assertions'],
        summary['subtest_outcomes'].get('PASS', 0),
        summary['subtest_outcomes'].get('FAIL', 0),
        summary['file_outcomes'].get('ERROR', 0),
        summary['file_outcomes'].get('TIMEOUT', 0),
    )
    need(actual == expected, label + ': observed counters differ: ' + repr(actual))
    need(set(summary['subtest_outcomes']) <= {'PASS', 'FAIL'}, label + ': additional subtest statuses need explicit review')
    need(set(summary['file_outcomes']) <= {'OK', 'ERROR', 'TIMEOUT'}, label + ': additional harness statuses need explicit review')
    need(summary['remaining_files'] == expected[0] - expected[1], label + ': unrun count differs')


def totals(rows):
    result = {key: sum(row[key] for row in rows) for key in (
        'scheduled_files', 'recorded_files', 'remaining_files',
        'files_passing_all_assertions', 'zero_assertion_testharness_files',
        'executed_subtests_excluding_notrun')}
    for field in ('file_outcomes', 'subtest_outcomes'):
        keys = set().union(*(row[field] for row in rows))
        result[field] = {key: sum(row[field].get(key, 0) for row in rows) for key in sorted(keys)}
    return result


def main():
    need(str(ROOT.resolve()) == '/Users/devin/repos/lumen', 'Unexpected repository')
    need(sha(PLAN) == PLAN_SHA, 'Frozen cohort differs')
    need(sha(WRAPPER) == WRAPPER_SHA, 'Reviewed priority wrapper differs')
    subprocess.run(['git', 'merge-base', '--is-ancestor', SOURCE_COMMIT, 'HEAD'], check=True)
    subprocess.run(['git', 'diff', '--exit-code', SOURCE_COMMIT, '--', 'src', 'Makefile'], check=True, stdout=subprocess.PIPE)
    for path, expected in FILES.items():
        need(sha(ROOT / path) == expected, 'Reviewed reproduction file differs: ' + path)
    spec = importlib.util.spec_from_file_location('final_wpt_priority_wrapper', WRAPPER)
    wrapper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(wrapper)
    plan, schedules, _, runner = wrapper.validate_plan(PLAN, check_server=False, check_binaries=False)
    need(plan['binaries']['lumen']['sha256'] == BINARY_SHA == sha(plan['binaries']['lumen']['path']), 'Native executable differs')
    need(plan['selected_variants'] == 1515, 'Frozen schedule denominator differs')
    frozen = read(BASE / 'priority-selection-v1.json')
    need(frozen['selected_source_files'] == 235, 'Frozen source-file count differs')
    records = {}
    categories = {}
    for name, expected in EXPECTED.items():
        directory = PLAN.parent / name / 'lumen'
        wrapper.verify_run(directory, 'lumen', plan, schedules[name], runner)
        rows = runner.read_jsonl(directory / 'results.jsonl')
        summary = runner.summarize(rows, schedules[name])
        check_summary(summary, expected, name)
        records[name] = rows
        categories[name] = {'summary': compact(summary), 'results': evidence(directory / 'results.jsonl'), 'run': evidence(directory / 'run.json')}
    supplement_path = SUPPLEMENT / 'receipt.json'
    need(sha(supplement_path) == SUPPLEMENT_SHA, 'Reviewed supplemental receipt differs')
    supplement = read(supplement_path)
    need(supplement['complete'] and supplement['allSupplementalAssertionsPass'], 'Supplemental result incomplete')
    need(supplement['binarySHA256'] == BINARY_SHA and supplement['parentPlanSHA256'] == PLAN_SHA, 'Supplemental artifact/cohort differs')
    need(supplement['upstreamRevision'] == PIN, 'Supplemental upstream differs')
    original_path = ROOT / supplement['originalResults']
    need(sha(original_path) == supplement['originalResultsSHA256'], 'Original Headers ledger changed')
    raw = records['headers']
    repeated = runner.read_jsonl(SUPPLEMENT / 'lumen/results.jsonl')
    adjusted = runner.read_jsonl(SUPPLEMENT / 'adjusted-headers-results.jsonl')
    need(len(repeated) == 2 and [row['test'] for row in repeated] == supplement['urls'], 'Supplemental variant list differs')
    by_url = {row['test']: row for row in repeated}
    need(adjusted == [by_url.get(row['test'], row) for row in raw], 'Adjusted rows are not exact replacements')
    original_by_url = {row['test']: row for row in raw}
    need(len(supplement['replacementLedger']) == 2, 'Replacement ledger size differs')
    for ledger in supplement['replacementLedger']:
        url = ledger['test']
        need(row_hash(original_by_url[url]) == ledger['originalRowSHA256'], 'Original replacement row differs')
        need(row_hash(by_url[url]) == ledger['supplementalRowSHA256'], 'Supplemental replacement row differs')
    selected = [row for row in schedules['headers'] if row['test'] in by_url]
    raw_summary = runner.summarize(raw, schedules['headers'])
    repeated_summary = runner.summarize(repeated, selected)
    adjusted_summary = runner.summarize(adjusted, schedules['headers'])
    need(raw_summary == supplement['rawOriginalSummary'], 'Raw supplemental summary differs')
    need(repeated_summary == supplement['supplementalSummary'], 'Repeated summary differs')
    need(adjusted_summary == supplement['adjustedSummary'], 'Adjusted supplemental summary differs')
    check_summary(adjusted_summary, (52, 52, 26, 539, 0, 13, 13), 'Headers adjusted')
    helper = BASE / 'upstream' / supplement['helper']['path']
    need(sha(helper) == supplement['helper']['sha256'] == supplement['helper']['httpSHA256'], 'Restored helper differs')
    source_helper = subprocess.check_output(['git', '-C', str(BASE / 'upstream'), 'show', PIN + ':' + supplement['helper']['path']])
    need(source_helper == helper.read_bytes(), 'Restored helper is not pinned upstream content')
    batch_path = ROOT / 'output/session-10290244/final-priority-batch-1/receipt.json'
    batch = read(batch_path)
    need(batch['planSHA256'] == PLAN_SHA and batch['binarySHA256'] == BINARY_SHA, 'Batch provenance differs')
    need(not batch.get('stoppedForRunnerError') and batch.get('finishedUTC'), 'Batch did not finish naturally')
    need(all(not row.get('terminatedForBudget') and row.get('exitCode') == 0 for row in batch['stages'] if row.get('started')), 'A batch stage ended abnormally')
    # The launcher receipt, rather than an inferred duration, establishes the
    # observed natural exit. The detailed batch receipt retains every stage.
    job_path = ROOT / 'output/session-10290244/final-priority-batch-1-job/result.json'
    job = read(job_path)
    need(job.get('exit_code') == 0 and not job.get('timed_out'), 'Batch launcher did not exit cleanly')
    raw_total = totals([row['summary'] for row in categories.values()])
    adjusted_rows = [compact(adjusted_summary) if name == 'headers' else row['summary'] for name, row in categories.items()]
    adjusted_total = totals(adjusted_rows)
    need(raw_total['recorded_files'] == adjusted_total['recorded_files'] == 207, 'Recorded inventory differs')
    need(raw_total['remaining_files'] == adjusted_total['remaining_files'] == 1308, 'Unrun inventory differs')
    value = {
        'schema': 'lume-final-wpt-matrix-10290244-v1',
        'createdUTC': datetime.now(timezone.utc).isoformat(),
        'createdBy': {'model': 'GPT-6 Astra Pro', 'agents': 'Built-in same-model agents', 'remoteTool': 'devinmac3d', 'role': 'wpt_engine'},
        'sourceCommit': SOURCE_COMMIT, 'binarySHA256': BINARY_SHA,
        'upstreamRevision': PIN, 'cohort': evidence(PLAN),
        'scope': 'Predeclared priority subset; not the complete Web Platform Tests suite.',
        'frozenVariants': 1515, 'frozenSourceFiles': 235,
        'recordedVariants': 207, 'unrunVariants': 1308,
        'categories': categories, 'rawAggregateBookkeeping': raw_total,
        'headersSupplemental': {'receipt': evidence(supplement_path), 'original': categories['headers']['results'], 'adjustedResults': evidence(SUPPLEMENT / 'adjusted-headers-results.jsonl'), 'replacementLedger': supplement['replacementLedger'], 'summary': compact(adjusted_summary)},
        'adjustedAggregateBookkeeping': adjusted_total,
        'aggregateWarning': 'Assertion-weighted bookkeeping is not an overall browser-quality score or a full-WPT pass rate. Raw and supplemental views overlap and must never be added.',
        'batch': {'receipt': evidence(batch_path), 'job': evidence(job_path), 'recordedWallSeconds': batch['wallSeconds'], 'overFiveMinuteReason': 'Sequential upstream runs with unchanged 15s/65s case timeouts; unsupported service/shared contexts and explicitly unrun encoding cases are retained. The 304.226s launcher result is an expected bounded test batch, not a build regression.'},
        'encodingFailureGroups': [
            {'count': 23, 'area': 'Legacy form/query encoding', 'tests': ['big5-encoder.html', 'iso-2022-jp-encoder.html'], 'assessment': 'Output is UTF-8 where the fixtures expect legacy bytes or numeric character references. Exact form/document encoding propagation still needs tracing; this is not a TextEncoder UTF-8 API defect.'},
            {'count': 24, 'area': 'Stream interface and IDL exposure', 'tests': ['idlharness.any.html', 'idlharness.any.worker.html'], 'assessment': 'Existing TextDecoderStream inheritance/receiver checks and dedicated-worker exposure remain incomplete.'},
            {'count': 8, 'area': 'Legacy streaming decoder state', 'tests': ['iso-2022-jp-decoder.any.html', 'iso-2022-jp-decoder.any.worker.html'], 'assessment': 'Explicitly unsupported streaming/reset paths; requires persistent bounded legacy decoder state.'},
            {'count': 2, 'area': 'MessagePort ArrayBuffer transfer', 'tests': ['encodeInto.any.html', 'encodeInto.any.worker.html'], 'assessment': 'The fixture transfers its output buffer through MessagePort; current local-channel paths discard buffer transfer lists. Genuine-detachment codec controls pass. Confirm sender byteLength in a RED transfer regression before implementing shared transfer machinery.'},
        ],
        'referenceStatus': 'Installed Chrome URL control is quarantined after independent sanity failures; no aggregate parity or outperformance claim.',
        'reproductionFiles': [{'path': path, 'sha256': expected} for path, expected in FILES.items()],
        'priorityWrapperLimitation': 'The measured priority batch wrapper cleans up owned children in normal/error paths, but has no external SIGTERM handler. Preserve this exact measured revision; an interrupted future invocation needs explicit owned-process cleanup verification.',
    }
    receipt_copy = OUT / 'evidence/10290244-headers-fixture-supplemental.json'
    receipt_copy.parent.mkdir(parents=True, exist_ok=True)
    if receipt_copy.exists():
        need(sha(receipt_copy) == SUPPLEMENT_SHA, 'Committed supplemental copy differs')
    else:
        receipt_copy.write_bytes(supplement_path.read_bytes())
    json_path = OUT / (NAME + '.json')
    save(json_path, value)
    md_path = OUT / (NAME + '.md')
    md_path.write_text(markdown(value))
    outputs = [json_path, md_path, receipt_copy]
    provenance = {'schema': 'lume-final-wpt-provenance-10290244-v1', 'binarySHA256': BINARY_SHA, 'sourceCommit': SOURCE_COMMIT,
                  'generator': evidence(Path(__file__).resolve()), 'outputs': [evidence(path) for path in outputs],
                  'reviewedFiles': value['reproductionFiles']}
    manifest = OUT / 'evidence/10290244-final-wpt-provenance.json'
    save(manifest, provenance)
    print(json.dumps({'manifest': evidence(manifest), 'outputs': provenance['outputs'], 'recordedVariants': 207, 'unrunVariants': 1308, 'sourceUnchanged': True}, indent=2))


def markdown(value):
    lines = [
        '# Final scoped WPT matrix for task 10290244', '',
        '**Current native artifact:** SHA256 `' + BINARY_SHA + '`, compiled source checkpoint `' + SOURCE_COMMIT + '`.', '',
        'The preserved six-file regression selection passes **1,850/1,850 assertions**. In the separately frozen priority selection, **207 of 1,515 variants were recorded and 1,308 remain unrun**. After the verified two-row Headers fixture replacement, **160 of 207 recorded variants fully pass**. The denominator remains 1,515; these results do not establish full WPT or Chrome parity.', '',
        'The priority inventory contains 235 upstream source files at commit `' + PIN + '`. All generated window, dedicated-worker, shared-worker and service-worker variants remain in the frozen schedule. Categories were selected before their outcomes were observed.', '',
        '## Current artifact results', '',
        '| Category | Frozen variants | Recorded | Fully passing | Harness OK / ERROR / TIMEOUT | Subtests PASS / FAIL | Unrun |',
        '|---|---:|---:|---:|---|---|---:|',
    ]
    for name, row in value['categories'].items():
        s = row['summary']
        label = 'Headers — original raw' if name == 'headers' else {'dom': 'DOM', 'url': 'URL'}.get(name, name.capitalize())
        lines.append(table_row(label, s))
        if name == 'headers':
            lines.append(table_row('Headers — verified replacement view', value['headersSupplemental']['summary']))
    lines += [
        '', 'There are no additional recorded subtest TIMEOUT/NOTRUN or harness CRASH statuses in these guarded counters. Harness errors/timeouts include zero-assertion files and are never counted as passing. Raw totals are 158 fully passing files, 22,319 passed assertions and 67 failed assertions; the separate adjusted view is 160 fully passing files, 22,359 passed assertions and 65 failed assertions. Those assertion totals are weighted by each test file\'s assertion count and are bookkeeping, not an overall browser-quality score.', '',
        '## Headers fixture correction', '',
        'The original 52-variant run reported 499 PASS and 2 FAIL, with 24 fully passing files, 13 ERROR and 13 TIMEOUT. Both failures were the `Loading data…` setup in `headers-no-cors.any.html` and its dedicated-worker variant. A sparse-checkout helper, `fetch/api/cors/resources/not-cors-safelisted.json`, was absent, so the official server returned an HTTP 404 JSON error object instead of the expected array.', '',
        'The helper was restored from the same pinned upstream revision. Its Git bytes, local SHA256, HTTP 200 response bytes and 11-entry JSON-array shape were verified. Exactly those two variants were rerun on the unchanged native artifact: 54/54 assertions passed. The original two rows contained 14 passed assertions and 2 setup failures; replacing those rows gives 539/539 observed assertions across the same 52 scheduled variants, with 26 fully passing files. The 13 ERROR and 13 TIMEOUT remain visible. The repeated 54 assertions must not be added to the raw 501-assertion denominator.', '',
        'The byte-exact receipt is committed at `agent/evidence/10290244-headers-fixture-supplemental.json`, SHA256 `' + SUPPLEMENT_SHA + '`. It preserves the original ledger hash and each old/new row hash. Original results remain at `output/wpt-10290244/priority-1b9d-v1/headers/lumen/results.jsonl`; the separate replacement ledger is `output/wpt-10290244/priority-1b9d-v1-headers-supplemental-1/adjusted-headers-results.jsonl`.', '',
        '## Remaining encoding failures and next priorities', '',
        '| Count | Area | Interpretation and next step |', '|---:|---|---|',
        '|23|Legacy Big5/ISO-2022-JP form/query encoding|The fixtures receive UTF-8 where legacy bytes or numeric character references are expected. Trace document/form encoding propagation; existing acceptCharset reflection is present. This is separate from the UTF-8 TextEncoder API.|',
        '|24|Stream interfaces and IDL|Repair TextDecoderStream inheritance, private receiver checks and worker exposure with a shared stream implementation.|',
        '|8|Legacy streaming/reset state|Implement bounded persistent ISO-2022-JP decoder state. Do not remove the explicit unsupported guard or buffer entire streams without a bound.|',
        '|2|MessagePort ArrayBuffer transfer|The fixture transfers a destination buffer, but current channel paths discard buffer transfer lists. Add sender-detachment/recipient-identity RED tests and repair transfer machinery; genuine-detachment codec controls already pass.|', '',
        'These four groups account for all 57 recorded encoding assertion failures. Encoding coverage remains 31/1,339 variants, with 1,308 unrun; no broader encoding pass claim is supported. The runtime follow-up document contains implementation paths and regression requirements: `agent/gpt6-astra-pro_builtin-agents_devinmac3d_runtime_followup_10290244.md`.', '',
        '## Denominators and reference limitations', '',
        '- Report fully passing files against every attempted file, with unrun files adjacent, or against the complete frozen inventory. Never divide only by harness-OK files.',
        '- Keep 60 frozen shared/service-worker variants visible: 26 Headers, 4 microtasks and 30 encoding. Outcomes apply only to cases actually executed; unrun variants remain unrun.',
        '- Do not add the original 1,850-assertion selection to this matrix because the selections overlap. Custom native checks are also separate.',
        '- Installed Chrome failed independent about:blank URL sanity controls. Its historical URL counts remain quarantined; no aggregate Chrome parity or outperformance claim is made.', '',
        '## Separate native verification', '',
        'The same artifact passed 1,122/1,122 custom native/window/worker checks, plus the separate existing native encoding target at 89/89. The combined build took 11.104s. These checks are not upstream WPT assertions. The original selected1,850-assertion regression remains 6/6 fully passing files.', '',
        'Current-artifact CPU/RAM and Apple deployment evidence is adjudicated separately in `agent/evidence/10290244-010-current-native-resources.json` and `agent/scorecard-10290244.json`; this matrix awards no performance, IPA or quality-score credit.', '',
        '## Reproduction and duration', '',
        'Run the unchanged priority wrapper with `run --plan output/wpt-10290244/priority-1b9d-v1/cohort.json --category <category> --engine lumen --max-seconds <budget>`. It uses the official fixture, immutable native SHA, fixed 15s/65s per-file timeouts and 1Hz process telemetry. Resume retains every unrun frozen variant. A clean new cohort/output directory is required for a separate execution history.', '',
        'The serialized final batch completed naturally in 304.226s. This exceeds five minutes because it includes sequential upstream cases and expected unsupported-context timeouts under the unchanged limits; it is not an abnormal build delay. Category timings and termination flags remain in `output/session-10290244/final-priority-batch-1/receipt.json`.', '',
        'The measured wrappers are `remote_priority_batch_10290244.py`, `remote_headers_supplemental_10290244.py` and `remote_final_resource_batch_10290244.py` under `tools/apple-session-gpt6as/`. The matrix JSON records their exact hashes. The priority batch wrapper has no external SIGTERM handler; its observed run ended naturally. Preserve that measured revision, and verify owned-child cleanup explicitly if a future invocation is externally interrupted.', '',
        'Generated by GPT-6 Astra Pro, built-in same-model agents and devinmac3d. The adjacent JSON and `agent/evidence/10290244-final-wpt-provenance.json` bind every recorded matrix row to its source results and reproduction files.', '',
    ]
    return '\n'.join(lines)


def table_row(label, summary):
    f = summary['file_outcomes']
    s = summary['subtest_outcomes']
    return '| ' + ' | '.join(map(str, (label, summary['scheduled_files'], summary['recorded_files'], summary['files_passing_all_assertions'],
        f"{f.get('OK', 0)} / {f.get('ERROR', 0)} / {f.get('TIMEOUT', 0)}", f"{s.get('PASS', 0)} / {s.get('FAIL', 0)}", summary['remaining_files']))) + ' |'


if __name__ == '__main__':
    main()
