"""Consolidate task-owned handoff text without changing native code or scores."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
from zoneinfo import ZoneInfo

p = argparse.ArgumentParser()
p.add_argument('--expected-head', required=True)
args = p.parse_args()
root = Path.cwd()
branch = 'gpt6-astra-pro_release-integrator_lume-apple-wpt-10290244'

def need(value, message):
    if not value:
        raise RuntimeError(message)

def git(*arguments):
    return subprocess.run(['git', *arguments], check=True, capture_output=True,
                          text=True, timeout=30).stdout.strip()

def read(relative):
    return json.loads((root / relative).read_text())

def sha(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()

head = git('rev-parse', 'HEAD')
need(head == args.expected_head and re.fullmatch('[0-9a-f]{40}', head), 'Source checkpoint changed')
need(git('branch', '--show-current') == branch, 'Wrong task branch')
need(not git('diff', '--cached', '--name-only'), 'Another owner has staged changes')
need(sha(root / 'agent/task.md') == 'c84a64490d014bdc12fb4e20627d3ed898617d0f9849b816da8c72c7d9fe3f8a', 'Original task text changed')
score = read('agent/scorecard-10290244.json')
gates = {row['id']: row for row in score['gates']}
native = score['current_baseline_binary_sha256']
wpt_path = gates['wpt_original']['evidence'][0]
wpt = read(wpt_path)
need(wpt['candidate']['run_info']['browser_version'] == 'sha256:' + native, 'Accepted WPT artifact differs')
need(wpt.get('comparable') and not wpt.get('blockers'), 'Accepted original WPT evidence is incomplete')
summary_paths = [name for name in gates['repro_source']['evidence'] if name.endswith('/summary.json')]
need(len(summary_paths) == 1, 'Current source build summary must be explicit')
summary = read(summary_paths[0])
artifact = summary['artifacts']['lumen']
need(summary['passed'] and artifact['sha256'] == native and sha(root / artifact['snapshot']) == native,
     'Immutable native artifact or build summary differs')
compiled_path = str(Path(summary_paths[0]).with_name('compiled-source-sha256.json'))
compiled = read(compiled_path)
for name, digest in compiled.items():
    need(sha(root / name) == digest, 'Unaccepted source changes remain: ' + name)

now = datetime.now(timezone.utc)
stamp = now.astimezone(ZoneInfo('Asia/Shanghai')).strftime('%Y%m%d-%H%M%S')
archive = root / 'agent/archive'
archive.mkdir(parents=True, exist_ok=True)
old_agents = (root / 'AGENTS.md').read_text()
marker = '# Agent notes for Lumen'
need(marker in old_agents, 'Stable project guidance boundary is missing; inspect before rewriting')
stable = old_agents[old_agents.index(marker):]
# These are the task-owned append-only sections from our guarded checkpoints.
# Archive the complete original first and retain every preceding project rule.
stable = re.split(r'\n## (?:Latest|Previous) verified task checkpoint\s*[—-]', stable, maxsplit=1)[0].strip()
archived = []
for name in ('AGENTS', 'continue'):
    relative = 'AGENTS.md' if name == 'AGENTS' else 'agent/continue.md'
    target = archive / f'{name}-{stamp}-10290244-checkpoint.md'
    with target.open('xb') as stream:
        stream.write((root / relative).read_bytes())
    archived.append(str(target.relative_to(root)))

frozen = read('output/wpt-10290244/priority-selection-v1.json')
need(frozen['selected_variants'] == 1515 and frozen['selected_source_files'] == 235,
     'Frozen priority inventory changed')
status_path = root / 'output/session-10290244/progress-state/status.json'
status = json.loads(status_path.read_text()) if status_path.is_file() else {}
expanded = status.get('expandedWpt', {})
reference_path = root / 'output/wpt-10290244/reference-review.json'
reference = json.loads(reference_path.read_text()) if reference_path.is_file() else {}
resource_evidence_path = root / 'agent/evidence/10290244-010-current-native-resources.json'
reference_diagnostic_note = ''
if resource_evidence_path.is_file():
    resource_evidence = json.loads(resource_evidence_path.read_text())
    if resource_evidence.get('nativeSHA256') == native:
        diagnostic = resource_evidence.get('referenceDiagnostics', {}).get('officialGoogleDmg', {})
        if diagnostic:
            reference_diagnostic_note = (f"The official stable Google DMG download/image-integrity check passed, but mounted-app deep strict signature validation rejected Finder metadata. Its owned read-only mount was detached and the browser was not executed; no metadata or trust changes were made. Receipt `{diagnostic['receipt']}` SHA-256 `{diagnostic['receiptSHA256']}`; preserved diagnostic is in `{resource_evidence_path.relative_to(root)}`. This does not establish a replacement reference.")
last_iteration = score['history'][-1]['iteration']
elapsed = (now - datetime.fromisoformat(score['session_start_utc'].replace('Z', '+00:00'))).total_seconds()
subset = wpt['candidate']
pass_count = subset['subtests']['passed']
total_count = subset['subtests']['union_inventory']
file_count = subset['files']['reported']
fully_count = subset['files']['passed']
category_lines = []
for name, count in frozen['category_counts'].items():
    row = expanded.get('categories', {}).get(name, {})
    category_lines.append(f"| {name} | {count['variants']} | {row.get('lumenRecorded', 0)} | {row.get('lumenFullyPassing', 0)} |")
gates_text = '\n'.join(f"| {row['id']} | {row['status']} | {row['earned']}/{row['weight']} |" for row in score['gates'])
progress = 'https://4d3ee194969f5f51.czd5qzludboe.opentunnel.xyz'
reference_note = ('The installed Chrome control remains quarantined after independent native URL/anchor sanity failures. Preserve its raw failures and do not award comparative retention from that reference.'
                  if reference.get('status') == 'quarantined'
                  else 'Read the current reference-review receipt before making any Chrome-parity claim; artifact identity and native sanity controls are required.')
native_scope = 'Current accepted native artifact' if expanded.get('lumenBinarySHA256') == native else 'Historical or unreported native cohort; do not transfer these results to the accepted executable'
runtime_followup = root / 'agent/gpt6-astra-pro_builtin-agents_devinmac3d_runtime_followup_10290244.md'
runtime_followup_note = ''
if runtime_followup.is_file():
    need(sha(runtime_followup) == 'b25de8aa933721b6b43b86461f32e06dffba1d745b795c1276bb39347d16fd6d',
         'Reviewed runtime follow-up note differs')
    runtime_followup_note = (f"Detailed next implementation plan: `{runtime_followup.relative_to(root)}`. It identifies the MessagePort transfer-list loss behind two detached-buffer failures, existing native serializer/backing-store ownership to reuse, exact red-first tests, stream interface/state gaps, platform-matched Apple dependencies and verified-process cleanup. This is a reviewed design note, not implemented or passing behavior.")
matrix_note = ''
matrix_path = root / 'agent/gpt6-astra-pro_builtin-agents_devinmac3d_wpt_matrix_10290244.md'
if matrix_path.is_file():
    matrix_relative = str(matrix_path.relative_to(root))
    need(not git('diff', '--', matrix_relative), 'Final matrix has uncommitted edits')
    git('ls-files', '--error-unmatch', matrix_relative)
    matrix_note = (f"The complete current-artifact category matrix and denominator policy are committed at `{matrix_relative}` with a JSON companion. Read its separate raw and adjusted Headers rows: the pinned helper restoration replaces exactly two rows in a supplemental view, leaves the original ledger untouched, and does not add variants to the52-variant inventory. The matrix preserves errors, timeouts, unsupported contexts and all unrun encoding variants.")

header = f'''# Lume task 10290244 — current evidence checkpoint

Read `agent/task.md`, `agent/continue.md`, and `agent/scorecard-10290244.json` first.
Canonical project: `/Users/devin/repos/lumen` (requested `~/project/lumen`).
Task branch: `{branch}`. Source checkpoint before this handoff: `{head}`.
Recorded: {now.isoformat()}. Session start {score['session_start_utc']}; authorized deadline {score['deadline_utc']}.

## Ownership and operating rules

- The root coordinator brokers Mac I/O; child devinmac3d calls repeatedly hang. Three built-in workers inherit the same model and reasoning level. Do not replace this native repository with an older companion checkout.
- One integrator stages and commits exact reviewed allowlists. Engine/runtime owners coordinate shared Makefile, native bindings, workers and preludes. Serialize heavy builds, browser runs and controlled CPU measurements.
- Use existing one-second resource monitors for every build/test. Preserve stage timings, collection gaps, peaks, process cleanup and executable hashes. Investigate abnormal stages above five minutes. Reuse matching prebuilt V8.
- `agent/evidence/`, the fixed scorecard, raw output receipts and compiled-source hashes distinguish accepted, historical and unverified evidence. Custom regression counts are separate from upstream WPT assertions. A six-file pass is not full WPT conformance.
- Keep all 1515 frozen priority variants visible, including unsupported workers and unrun cases. Preserve original upstream assertions and reference anomalies. A changed executable needs fresh performance acceptance.
- Same-device IPA acceptance requires the identical signed iOS device package actually running on each requested platform. Native Mac builds, simulator binaries, SDK link probes and package creation alone do not establish this.
- Never stage or publish `output/` wholesale: it contains private tunnel/message state and browser profiles. Use the reviewed committed-source packer; omit credentials, private access fragments, `.git/config`, dependency environments and unrelated files.
- Record iteration start/end, elapsed wall time and timed build/test evidence in `agent/devsummary.md`. Separate worker/model compute duration is unavailable; do not invent or sum parallel wall times as elapsed time.

{stable}

## Latest accepted evidence and next work

Iteration `{last_iteration}`; score {score['score']}/100 against the fixed 98/100 whole-task goal. This is an acceptance checklist, not a WPT percentage.
Native `{artifact['snapshot']}`, SHA-256 `{native}`.
Original pinned six-file subset: {pass_count}/{total_count} assertions, {fully_count}/{file_count} files fully passing.
{reference_note}

Read the refreshed continuation for current gates and reproduction paths. Continue important standards failures, high scrolling CPU and real Apple package execution; do not treat a build or this handoff as completion. The complete previous handoff/instructions are archived under `agent/archive/` with timestamp `{stamp}`.
'''

continuation = f'''# Lume Apple WPT 10290244 — continuation

Updated {now.isoformat()} after {elapsed:.3f}s elapsed wall time. Session {score['session_start_utc']} to {score['deadline_utc']}; goal98/100, maximum1000 iterations or the authorized three-hour budget. This checkpoint does not claim the goal was reached.

## Source and artifact identity

- Repository `/Users/devin/repos/lumen`, requested alias `~/project/lumen`.
- Branch `{branch}`; prior source HEAD `{head}`. This documentation commit follows that source checkpoint.
- Accepted iteration `{last_iteration}`; native SHA-256 `{native}` at `{artifact['snapshot']}`.
- Current native build summary `{summary_paths[0]}`; complete compilation source hashes `{compiled_path}`.
- Exact original request is `agent/task.md` (SHA-256 c84a64490d014bdc12fb4e20627d3ed898617d0f9849b816da8c72c7d9fe3f8a).
- Read `agent/devsummary.md` and `agent/evidence/` for regression-first changes, immutable artifacts and per-stage timing. Git is the source of truth; do not duplicate repository code into personal file storage.

## Verified standards coverage

The unchanged original six-file pinned upstream subset reports **{pass_count}/{total_count} assertions** and **{fully_count}/{file_count} files fully passing** on the accepted executable. Exact comparison: `{wpt_path}`. This is not a full Web Platform Tests pass rate.

The broader predeclared subset is1515 variants across235 source files at revision7f8d49b5a82606efc771f43d10aad5bce5ccbfeb. Schedule SHA-256 adcbf9719f722368b1e53ac1bd61cf38e566d3bd7495d9d6127cad38dd6aeabe. Equal category weighting prevents encoding's1339 variants from concealing failures elsewhere. Public category snapshot below is {native_scope.lower()}.

{matrix_note}

| Category | Frozen variants | Recorded native variants | Fully passing native variants |
|---|---:|---:|---:|
{chr(10).join(category_lines)}

{reference_note} The downloaded official CfT archive's hash matching and its failed macOS signature check are distinct facts; no trust bypass is authorized. A new independently verified Chrome artifact and fresh sanity/paired reports are required to replace quarantined comparisons.

{reference_diagnostic_note}

## Current acceptance gates

Score **{score['score']}/100**. Only current-artifact receipts earn resource gates; old wheel idle/soak and failed scrolling measurements remain historical after changing the executable.

| Gate | Status | Earned / available |
|---|---|---:|
{gates_text}

Same-device IPA execution is incomplete unless the corresponding gates above explicitly pass with matching package hashes and actual launch receipts. Existing source/prebuilt recovery and SDK/toolchain probes do not substitute for execution.

## Reproduction and release tools

- `tools/monitor_process.py` supplies one-second process sampling and optional steady interval CPU gates; native workload drivers live in `tools/apple-session-gpt6as/`.
- `tools/apple-session-gpt6as/run_priority_wpt_10290244.py` prepares immutable cohorts, executes resumable frozen categories and reports all unrun/unsupported variants. Keep exact native/reference binaries, official fixture provenance and schedule identity.
- `tools/apple-session-gpt6as/package_source_10290244.py` packages committed source plus a portable Git bundle, with an explicit secret/source audit.
- `tools/apple-session-gpt6as/update_priority_progress_10290244.py` refreshes live data from the accepted scorecard and selected cohort; it must run after archive refresh.
- `tools/apple-session-gpt6as/verify_progress_public_10290244.mjs` verifies TLS, two-second WebSocket metrics, public source bytes and the authenticated message/reply path.
- `tools/apple-session-gpt6as/publish_progress_surge_10290244.py` uses only an existing Surge login/token, records a bounded deployment receipt, and never creates credentials or changes trust.
- Live task progress: {progress}. The private access fragment is available only in the user chat/private runtime state. Never put it in Git, handoff files, public config or source archives.
- The FileHarbor reservation API returned403. Do not retry alternate routes to bypass access controls. The authorized fallback is a standalone durable delivery receipt; source remains Git-backed and downloadable from the verified archive.

## Prioritized next steps

{runtime_followup_note}

1. Preserve the exact accepted source and raw measurements. Inspect Git status before any new task branch; coordinate shared native files and avoid competing commits.
2. Finish important frozen category failures with meaningful red/green regressions and unchanged upstream assertions. Keep absent/shared/service-worker support explicit. New codec or Headers coverage must be tied to the compiled artifact that ran it.
3. Re-run preset idle, interaction and navigation-soak workloads on the final executable. Investigate scrolling above10%p95; report CPU/RSS samples and gaps, not just successful process exit.
4. Resolve real Apple device/simulator runtime and signed same-IPA prerequisites using existing authorized tooling/artifacts. Do not invent signing identities or infer execution from SDK link success.
5. Push the current branch, refresh source archive and public status, verify the live route/message inbox, attempt the requested Surge publication using existing authorization, and preserve the final source/receipt identities.

Prior append-only continuation and AGENTS are archived with Asia/Shanghai timestamp `{stamp}`. Current model workers are equally capable; root is project manager and the remote I/O broker. Any future session should first re-check live process ownership and evidence freshness.
'''

(root / 'AGENTS.md').write_text(header)
(root / 'agent/continue.md').write_text(continuation)
with (root / 'agent/devsummary.md').open('a') as output:
    output.write(f'\n\n## Integration handoff checkpoint {now.isoformat()}\n\nConsolidated outdated task-owned instructions and archived both previous documents as {archived}. Accepted iteration {last_iteration}, source base `{head}`, native `{native}`, score {score["score"]}/100. Source hashes and immutable artifact were rechecked; no native code, score or raw WPT result changed. Session elapsed {elapsed:.3f}s. Separate model compute time remains unavailable. Refresh/push source publication after this documentation commit.\n')
paths = ['AGENTS.md', 'agent/continue.md', 'agent/devsummary.md', *archived]
need(not git('diff', '--cached', '--name-only'), 'Concurrent staged changes appeared')
git('add', '--', *paths)
need(set(git('diff', '--cached', '--name-only').splitlines()) == set(paths), 'Documentation staging escaped the allowlist')
git('diff', '--cached', '--check')
git('commit', '-m', 'Consolidate current Lume evidence and reproducible task handoff')
print(json.dumps({'head': git('rev-parse', 'HEAD'), 'sourceBaseCommit': head,
                  'acceptedIteration': last_iteration, 'nativeSHA256': native,
                  'scoreUnchanged': score['score'], 'sessionSeconds': round(elapsed, 3),
                  'paths': paths}, indent=2))
