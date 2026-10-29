# Lumen Apple browser — task 10.30 01:10

Read `agent/task.md`, this file, and `agent/continue.md` before changing the project. `agent/task.md` preserves the complete current user request verbatim, including repeated directives.

## Active task and ownership

- Session start: 2026-10-29T17:11:00Z. Normal three-hour cutoff: 2026-10-29T20:11:00Z; extend only for an important bug, at most to21:11:00Z.
- Current branch: `gpt6-astra-pro_builtin-agents_lumen-10.30-0110-sharedworker`.
- Root is coordinator only. Three built-in agents inherit the same model/reasoning level: repository integration/source; independent WPT validation; Apple runtime/performance/package/progress delivery.
- Canonical Mac repository: `/Users/devin/repos/lumen` (`~/project/lumen` alias). **Child devinmac3d calls hang; use root transport immediately with agent-authored bounded scripts.** The current local full-history source copy is `/workspace/scratch/4aa97518bdcd/project/lumen`.
- Repository integration owns all branch/commit state and `AGENTS.md`, `agent/task.md`, `agent/continue.md`, `agent/devsummary.md`, `src/js/worker.cc`, and related worker preludes. WPT lead owns new oracle fixtures/drivers. Apple lead owns packaging/performance/dashboard files. Coordinate before crossing ownership.
- Preserve unrelated/untracked remote tools and verification data. Do not broad-clean, reset, or kill unrelated processes. Existing approved product is native004/dc7 until a new candidate passes unchanged regression and resource gates.
- Current target is genuine SharedWorker execution with one shared native isolate, distinct connection ports, origin/URL/name identity and safe per-owner teardown. Never alias DedicatedWorker as SharedWorker. ServiceWorker remains separately unsupported until actual lifecycle semantics exist.
- Use existing compatible prebuilt V8. Keep platform identity, exact compiled-source manifest and binary hashes explicit. Sample the owned process tree once per second during builds/tests; record p95/peak, intervals, gaps and cleanup. Investigate stages exceeding five minutes.
- Track real diagnose→fix→verify iterations with UTC start/end, stage wall time and evidence in `agent/devsummary.md`. Parallel effort is not elapsed wall time. Keep the fixed acceptance rubric and never equate WPT percentage with task quality; unknown mobile gates earn zero.
- A previous local translated-PID issue requires supported execution escalation for affected monitoring/evidence operations. Preserve fail-closed PID/start-identity guards. Respect every automatic review rejection; do not change proxy/security settings or reroute a rejected operation.

## Historical acceptance and constraints

The following retained evidence describes the prior accepted native004 artifact, not automatic acceptance of newly changed source. Read the current continuation for active candidate state.

## Current source and measured state

The reviewed product source is `dc7a137d79551df3f5d3dc5cc914d06f0b7be468`. Later Git commits contain documentation/evidence/tooling only; their source archive identity is distinct from the compiled source.

- Mac004 Lumen SHA256: `f906769431c7a25b28ba53a364ffb5027ca0dd04fa0f8accc1161407203d3b95`.
- Mac004 jsrun SHA256: `96693ac8d8f914f75563335fae20876a7c30922f77679525ae9697f9cb1e04cb`.
- Native004 build/regression gates: 19/19 in 44.191s, including actual Mac GUI handler-policy controls.
- Complete frozen priority WPT: **1,454/1,515 fully passing variants, zero unrun**; 1,166,858 PASS / 7 FAIL assertions; 1,455 OK / 30 TIMEOUT / 30 ERROR harness outcomes. All errors/timeouts remain counted. Independent source/raw audit confirms no lost prior passing assertion or variant. This is a selected priority inventory, not the full official WPT suite.
- Fixed overlapping 279 selection: 248/279 versus 246/279 original baseline. Original six-file subset separately passes 1,850/1,850 assertions on exact004.
- Exact official Google Chrome for Testing 156.0.8078.47/Linux reference: 1,462/1,515 fully passing, 1,166,990 PASS / 1,023 FAIL, all 1,515 harnesses OK. Native retains 1,165,970 Chrome-passing assertion identities; 1,020 remain missing/nonpassing. Do not infer parity from an aggregate percentage.

The full comparison proves identical 1,515 variant metadata, pinned source and 457 selected source/dependency files. Whole generated sparse manifests differ in unrelated entries; original rejected comparisons remain preserved. The exact official browser was independently redownloaded and byte-verified, including fresh-profile direct CLI controls without CDP/WPT injection. Unexpected URL behavior is preserved and belongs to this explicit version; do not describe it as latest or general Chrome behavior.

Current measured GUI scroll p95 is 17.021%, peak 18.855%, below the user's 30% threshold. Settled navigation RSS is 159.125→148.34375MiB with stable nodes/styles/workers and no GPU errors. **The independently audited resources,100-cycle/12,094-check stress and actual developmentMac.app package are accepted on exact004. Current score is61.426948 pending final public-source delivery, then66.426948 only after its full-byte checks pass.** Historical137 performance and package evidence never transfers to004.

## Security history that must remain visible

Earlier source b816/native003 completed the same WPT inventory but failed a new parser/meta handler oracle: 7/9 versus reference 9/9. Inline body handlers executed after restrictive head meta CSP. This was a real release hold, outside the selected WPT inventory; retain all RED evidence. Earlier137 could not navigate the data-form child, so its absence-of-handler results do not certify safety.

The dc7 correction defers eager frame body/frameset handler activation to the parser position, after preceding policies/scripts. It retains owner-realm native CSP checks and legitimate earlier callbacks. Unchanged9 regression and an independent36-case suite pass on corrected source; actual Mac GUI coverage is recorded. Keep scoped regression success separate from a general security audit. Evidence: `agent/evidence/10.292019r-security-hold-013/`, `10.292019r-parser-handler-correction-014/`, and `10.292019r-parser-handler-focused-controls.json`.

## Retained prior-session acceptance rules

- Branch: `gpt6-astra-pro_builtin-agents_lumen-10.292019r-worker-xhr`. Engineer owns Git/source integration; performance agent owns Apple/resource/lifetime oracles; delivery agent owns independent audit, publication and handoff. Preserve unrelated and untracked work; never broad-clean directories or kill unrelated processes.
- User target is 98/100 under the existing task rubric. The WPT percentage is not that score. Unknown mobile gates earn zero. Read `agent/scorecard-rubric-20261029-cpu30.md` and inherited proportional/category rules in `agent/scorecard-rubric-10290244.md`; unsupported/incomplete categories remain unearned. Never round a lower score up to95/98.
- Prior task10.292019r ran12:20:07–16:20:07Z. Its deadline is historical. Use the current task window above; builds/checkpoints are not stopping conditions.
- Preserve every scheduled variant, failure, timeout, error and unrun result. SharedWorker/ServiceWorker cannot be faked with DedicatedWorker. Highest remaining compatibility gap is those real execution contexts, followed by wider official WPT coverage. Seven selected IDNA assertion failures also occur in the exact reference. A read-only probe of the exact linked ICU78.3 library reports UnicodeData17.0.0.0 and0x80 UIDNA_ERROR_DISALLOWED for each; the pinned test data is18.0.0-labeled and its version page remains draft. Preserve the compatibility failures; do not weaken URL validation to hide them.
- Compare exact binary/source/dependency identity, pinned fixture, URLs, timeouts, reporter and monitor. Hash every accepted raw ledger. `tools/apple-session-gpt6as/audit_wpt_candidate.py` rejects disappearing assertions/results and invalid cleanup. Linux headless success cannot establish Apple runtime behavior.
- Prefer existing compatible prebuilt V8: native Mac15.6.75.16 arm64; Linux headless tests used15.7.112.12 with RTC/WebTransport disabled. Preserve that platform distinction.
- One-second process-tree CPU/RAM sampling is required for tests. Report p95/peak, interval count, gaps and cleanup; short cases cannot establish steady-state performance. Ordinary scrolling must stay below30% of one logical core; typing retains its separate10% rubric limit. Host status telemetry is a different measurement.
- Preserve actual signed-IPA and simulator distinctions. Accessible signing identities/profiles/mobile dependency archives were absent; no shared device IPA or actual iPad Lumen launch has been established. Do not bypass signing or repair vendor Xcode files.

## Delivery, persistence and publication boundaries

Live progress: https://dd1cacbcfa84c17d.q4d3dgcpeeoq.opentunnel.xyz . Static mirror: https://lume-102900614-gpt6-astra-pro.surge.sh . Verify current reachability rather than assuming saved URLs remain live. Existing namespace102900614 and task transport20261029-cpu30 preserve private inbox continuity; user-facing task is10.292019r. Never publish access capabilities, inbox text, signed transfer descriptors, credentials or profile files. Use explicit public path allowlists when searching or packaging.

The user authorized durable source/evidence checkpoints. Reviewed source archives include committed source and portable Git history, with all reachable blobs scanned; runtime output/private configuration is excluded. Current source/package/raw Library IDs and public archive identity are recorded in continuation and manifests. Keep published documentation commit, compiled dc7 source and executable SHA separate.

Git push remains blocked by automatic approval review. A later read-only Mac probe found configuredDestinationMatches=false before any permission query; its exact cause is unknown. Do not change remotes/credentials or try another connector to bypass the gate. Source/Git-history backups and reviewed public source publication are separate authorized operations.

## Context for continuing work

Read the final continuation first for current native resource/package, score and artifact acceptance. The package is an ad-hoc developmentMac.app with external Homebrew dylibs, not self-contained/notarized and not a sharedIPA. Native004/dc7 remains the accepted baseline. This newly authorized session may develop candidates; renewed candidate-specific tests/resources are required before promotion. No fabricated95/98 completion.
