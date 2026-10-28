# Current task: Lume Apple WPT 10290244

Read `agent/task.md` and `agent/continue.md` before work.
Canonical source: `/Users/devin/repos/lumen` (requested `~/project/lumen`).
Current branch: `gpt6-astra-pro_release-integrator_lume-apple-wpt-10290244`.
Inherited tracked source checkpoint: `a8bd64552f9379cca33cd736a187924ec18eb356`.
Session: 2026-10-29 02:47:35 Asia/Shanghai to 05:47:35 (three hours).

## Current ownership and acceptance

- `release_integrator` owns Git staging/commits, task/handoff/timing records, and progress publication.
- `wpt_engine` owns standards and engine fixes with regression-first tests.
- `apple_runtime` owns Apple packaging/build/runtime and resource sampling.
- Root manages and brokers remote I/O because child MCP calls hang; three built-in workers inherit the same model and reasoning level.
- Coordinate shared source edits; serialize heavy builds. Preserve existing untracked evidence and tooling.
- Sample CPU/RAM every second for every test/build. Record wall times; investigate abnormal runs over five minutes. Reuse matching prebuilt V8 when supported.
- State WPT subtest numerator/denominator, file harness completion, exact suite and revision. Focused WPT and custom regressions never establish full-suite Chrome parity.
- The goal is 98/100 for the whole Apple task. Keep unverified gates visible; a build or package alone does not satisfy the goal. Same-IPA acceptance requires a real compatible package plus actual platform execution/provenance.
- Output evidence includes tunnel credentials: never stage or publicly archive `output/` wholesale. Publish only an explicit reviewed source/evidence allowlist, excluding credentials, `.DS_Store`, dependency environments, and `.git/config`.
- Record completed iterations and elapsed session/worker/test/build time in `agent/devsummary.md`; commit important progress and checkpoint at least every ten iterations.

Prior session guidance is archived in `agent/archive/AGENTS-20261029-024735+0800.md`; stable project instructions follow.

# Agent notes for Lumen

See `CONTINUE.md` for project state, build layout and next steps.

## CPU monitoring during every test

Whenever you run Lumen for a test (GUI, ChatGPT, scrolling, typing, video,
benchmarks), sample its CPU every second for the whole run and check it:

```sh
P=$(pgrep -n lumen)
while kill -0 $P 2>/dev/null; do ps -o %cpu=,rss= -p $P; sleep 1; done
```

- Idle pages (nothing animating, no input) should sit near 0-3% CPU.
- Ordinary interaction (typing, scrolling, hover, clicking) should stay
  low; typing in the ChatGPT composer must stay under 10%.
- Sustained high CPU is only acceptable while doing real heavy work (page
  load, video decode, a long streamed reply). Anything else is a bug:
  profile it (`sample <pid> 5 -file out.txt` on macOS, `perf` on Linux),
  find the hot path and fix it before moving on.
- Report the measured CPU numbers with the test results.


## Previous verified task checkpoint — 10290244-004

Current native snapshot is `build-apple-load-20261029/lumen-render-overdraw-f941cfcdedd9a59b` (f941cfcdedd9a59b…). Original fixed WPT subset: 1850/1850 in Lume and Chrome; renderer exact-pixel regressions 92/92. Idle passes independently; scroll p95 23.864% still exceeds 10%. Keep this failed gate explicit. Broader coverage is a frozen 1515-variant,235-source,seven-category priority suite, not all WPT. Same-device IPA execution and a full navigation soak remain unverified. Read the latest `agent/continue.md` entries and scorecard before further source changes; every changed binary needs matching fresh performance evidence.


## Previous verified task checkpoint — 10290244-005

Current native snapshot `build-apple-load-20261029/lumen-clone-allocation-de338a9f08913dc8` (de338a9f08913dc8…) passes 292 targeted allocation/compatibility checks and 1850/1850 assertions in the original six-file WPT subset. Lazy-wrapper allocation regression is fixed with exact counters, but current-binary CPU/RSS/soak still needs fresh evidence; previous f941 idle/scroll observations are historical. Expanded official-server WPT keeps 1515 frozen variants in seven categories and reports missing cases. Same-IPA Mac/iPad/iPhone execution remains unverified. Read the latest continuation and scorecard before modifying shared Makefile or native runtime files.


## Task10290244 progress service

Public HTTPS base: https://4d3ee194969f5f51.czd5qzludboe.opentunnel.xyz. It is verified with actual WebSocket snapshots and private message/reply round trips. Preserve explicit current/historical executable labels. Keep the private access fragment, message receipt keys, ignored state, tunnel profiles and user profiles out of Git/source archives. The progress helper reads exact scorecard evidence; successful UI/build work is not browser conformance.


## Previous verified task checkpoint —10290244-006

Native `build-apple-load-20261029/lumen-wheel-fast-path-7f0cafc374cf2cf1` (7f0cafc374cf2cf1…) has303 focused checks and1850/1850 original-subset assertions in both engines. Current-artifact idle and20-navigation memory soak pass. Scrolling remains21.973%p95 against10%target. Broader1515variant coverage is incomplete; sameIPA execution remains unverified. Read the newest continuation/evidence and preserve current/historical artifact distinctions.


## Previous verified task checkpoint —10290244-007

Accepted source `4517145578ef3374cba6e699b2fe183fc25e48a0` and immutable native `eac62c75a9958f9cdc26cc85d4aa54e17c4031f9c9e43915263673375626fb55`:714 targeted checks;1850/1850 original six-file WPT assertions. Expanded1515-variant coverage is incomplete. Absolute DOM995/996 and events78/78 are promising but installed Chrome control URL sanity failed, so no broad comparative acceptance. Task score40/100 includes verified live delivery workflow; new native performance and same-device IPA gates remain unverified. Preserve the next candidate's uncommitted files and keep root as the only remote I/O broker. Refresh archive/status after each accepted source checkpoint.


## Previous verified task checkpoint —10290244-008

Native `2c27f3bb2c305b2fc3b79d1e257c9913380432014a5a85832306afa13441dc32` passes815 targeted checks and1850/1850 original six-file WPT assertions. Frame microtask realm and Storage.key conversion fixes are committed with exact source hashes. Score40/100; expanded1515-variant work is incomplete, old reference URL behavior remains quarantined, and current-artifact performance/IPA needs proof. New header/encoding work must remain isolated until another measured checkpoint.


## Latest verified task checkpoint —10290244-009

Native `1b9d43ed19cd616b0029e34fde97050b86c9acbdbda16a0dca5892922df704e9` passes1122 targeted native checks and1850/1850 original-subset WPT assertions. Headers conversion/guard/iteration and HTTP ByteString transport fixes are committed with exact source hashes. Score40/100; current-artifact performance, full frozen priority coverage and same-device IPA execution remain incomplete. Preserve reference quarantine and coordinate further shared-source edits before mutation.
