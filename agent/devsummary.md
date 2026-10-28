

# Session 10290244 — Lume Apple WPT

Start: 2026-10-28T18:47:35Z (2026-10-29 02:47:35 Asia/Shanghai).
Deadline: 2026-10-28T21:47:35Z. Branch: `gpt6-astra-pro_release-integrator_lume-apple-wpt-10290244`.
Model: GPT-6 Astra Pro, built-in subagents with inherited same reasoning.
Workers: wpt_engine, apple_runtime, release_integrator; root manages and brokers remote I/O.

Intake preserved inherited source in `a8bd64552f9379cca33cd736a187924ec18eb356` and archived prior handoff.
No new compatibility percentage or overall quality score is inferred from intake.
Iteration scores and times below must come from completed evidence-backed work.

| Iteration | Start UTC | End UTC | Wall seconds | Worker effort seconds | Tests/build timing | Score and basis | Result |
|---|---|---|---:|---:|---|---|---|
| 10290244-001 | 2026-10-28T18:47:35Z | 2026-10-28T19:00:32.602027+00:00 | 777.602 | Not separately instrumented; three same-model workers active in parallel | Native frozen WPT 9.224 s; Chrome reference 4.144 s; compare 0.037 s; monitor regression stage 1.053 s (20 tests) | 9.97/100 task gates; only current frozen subset earns points so far | Preserved source/task, established current 1844/1850 baseline, hardened interval CPU sampling acceptance. |

Iteration 10290244-001 evidence: `output/session-20261029/native-qa-kit-v1/source/output/wpt-comparison-session10290244-baseline-1.json`; `output/session-10290244/monitor-gate-green.json`; monitor raw CSV and test log under the same prefix. The monitor suite is 20/20, not WPT. A one-sample short unit run does not establish browser resource usage. The six-file upstream subset is 1844/1850 (99.675676%) versus Chrome 1850/1850; all six harnesses completed, five files fully pass. Full official WPT remains unmeasured. Session elapsed to this checkpoint: 777.602 seconds.


## Iteration 10290244-002 — enforced idle acceptance

Start: 2026-10-28T19:00:32.602027+00:00. End: 2026-10-28T19:10:56.571308+00:00. Iteration wall time: 623.969 s. Total session elapsed: 1401.571 s.
Workers: apple_runtime implemented and verified; release_integrator independently reviewed/committed; root brokered remote commands. Separate model compute/worker-active seconds are unavailable and are not equated with elapsed time.

The post-WASM idle runner now rejects over-limit p95 CPU, missing/nonfinite measurements, stale passing labels, short windows, and sampling errors/gaps. Mac regression tests: 6/6; monitored test stage 1.044 s. This is a tool acceptance correction, not an invented change in browser CPU.

Existing current-baseline raw samples were adjudicated using the preset full interval starting after 15 seconds of settling: 65 samples over 64.009 s, interval CPU mean 0.061%, p95 0.996%, peak 1.0%; RSS 156.484→144.266 MiB. No collection errors/gaps. Evidence: `output/session-10290244/apple-wasm-idle-baseline/fixed-settle-review.json`; original raw and receipt hashes are retained in that review.

Score: 14.97/100 task acceptance for baseline binary `7a4bfec10412f3e2c0eee92a01bf8c9bec496eb122114ab4844e8e4a4c783b85`. WPT baseline remains 1844/1850 (99.675676%) across six files; no full WPT or same-IPA claim. A changed candidate requires new artifact-matched performance evidence before retaining these points.


## Iteration 10290244-003 — native DOM clone and ownership repair

Start: 2026-10-28T19:10:56.571308+00:00. End: 2026-10-28T19:18:56.812906+00:00. Iteration wall: 480.242 s. Session elapsed: 1881.813 s. Workers: wpt_engine implementation; apple_runtime independent source review; release_integrator artifact/acceptance review; root remote I/O coordination. Separate model compute durations remain unavailable.

Fixed native Document allocation/clone ownership; qualified-name, URL/content-type/encoding/quirks metadata; cross-realm Attr identity, values, clone/import/attachment; shallow template cloning; and XHR XML document URL/baseURI. Regression-first clone metadata changed from 1/36 to36/36; XML/cross-realm fixture from25/33 to33/33. All262 targeted regression checks passed. These are separate from upstream WPT.

The unchanged six-file pinned WPT subset now passes1850/1850 (100%) in Lume and Chrome, all6files fully passing, no comparison blockers. This is not a full WPT result. Native binary SHA-256 `5ec2abd8d1503c17524e65ec6ed89ed5ba4a75253e326a0d8715f7a49b60fd75`; jsrun `ac22b2347e0d786f220575b140e75a775ea3d38fccdea4f2be460cd4a77e2966`. Incremental native build 3.075 s using prebuilt V8; longest test stage 9.083 s. Every stage retained one-second resource samples; very short stages do not establish idle behavior.

Source, fixtures, complete compiled-source hashes, targeted-stage timing, WPT comparison and red-log hashes are in `agent/evidence/10290244-003-clone-metadata.json`; raw reports remain under the recorded output paths. Score: 35.00/100 task gates. Prior 7a4b idle observations are explicitly historical; the new5ec2 artifact gets no idle/interaction/soak points before its own tests.
