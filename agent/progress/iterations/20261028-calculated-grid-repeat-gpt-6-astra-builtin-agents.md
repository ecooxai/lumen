# Calculated grid repeat counts — candidate 141

Model: GPT-6 Astra. Built-in agents: progress_and_deployment, repository_lead, standards_comparison, root. Tools: Ruby MCP, sealed native capture guard, paired WPT, Chromium 150 CDP, CTest, one-second process-tree monitoring.

## Acceptance and time

Scoped quality: **96/100**, accepted after independent standards source review and root authorization. Whole Linux-browser/Chromium-conformance goal remains **19/100 provisional**. This score does not measure a WPT pass percentage.

The recorded iteration runs from 2026-10-28 10:31:16.686735 UTC (first unchanged 16-case cross-browser observation launch) through 10:42:39.537553 UTC (corrected full native gate), **11.3808 minutes**. Earlier review/design time is excluded from this bounded interval. Session start remains 09:12:12 UTC; these concurrent-agent intervals must not be summed as exclusive wall time.

Candidate 138 is not independently accepted. Its initial 84-control and 122-CTest success was insufficient: review discovered ten valid calculated repetition-count omissions. The earlier candidate-138 record preserves the initial conditional 96 assessment and explicitly supersedes it with an 88 assessment and a blocker. Candidate 141 corrects the CSSOM rejection introduced by 135 and the native raw-style/support-query rejection introduced by 138.

## What changed

The existing native grid-track parser now evaluates supported CSS numeric expressions in repetition-count positions through a bounded typed arithmetic helper. Arithmetic retains dimensional exponents until a final scalar is required. It rejects incompatible dimensions and malformed syntax, handles supported math functions and constants without evaluating arbitrary JavaScript, rounds integer results toward positive infinity at ties, and applies the positive-count range. The existing storage bound remains 1,000 tracks.

Relative lengths use the existing computed font/root/media context rather than treating different units as interchangeable. The CSSOM grid parser now reuses its existing integer-math grammar for repetition counts. Native automatic-repeat preflight, support queries, atomic invalid-declaration rejection and allocation ownership from candidate 138 are retained.

This is a bounded repeat-count repair. It does not establish complete CSS math, complete CSS grammar, or all Grid layout behavior. In particular, `clamp(none, …)` is unsupported; contexts beyond the explicit em/rem checks and existing length implementation remain unverified. Complex specified-value arithmetic serialization may differ from Chromium.

## Unchanged measurements

| Evidence | Earlier native | Candidate 141 | Chromium 150 |
|---|---:|---:|---:|
| Original automatic-repeat controls | 41/84 in 135; 84/84 in 138 | 84/84 | 84/84 |
| New calculated-count controls, unchanged authored expectations | 48/128 in 138 | 128/128 | 124/128 |
| Font/order/mutation controls | Not used as a prior-build gain claim | 8/8 | 8/8 |
| Same 37 upstream WPT URLs | 347/1,172 in 128; 1,059/1,172 in 138 | 1,059/1,172 | 1,130/1,172 |
| Fully passing upstream URLs | 3/37 in 128; 31/37 in 138 | 31/37 | 35/37 |
| Native CTest, physical `/tmp` | Separate earlier records | 125/125 | Not applicable |

The original 16 observations retain all valid and invalid expressions. Candidate 141 matches Chromium's acceptance and measured child geometry in all 16; ten valid calculated-count regressions are repaired. This statement excludes specified complex-expression serialization differences.

The upstream replay has exactly zero named gains and zero named losses versus 138. Versus 128 it retains 754 gains and **42 losses**, all valid tentative Grid3 intrinsic automatic-repeat syntax outside the implemented Grid1/2 profile. The denominator, tentative tests and original raw results are unchanged. No broad-suite or shared-WPT superiority follows from the authored-control counts.

The eight context controls check raw declaration order, CSSOM setter order, own computed font changes, stylesheet updates, inherited font changes and root rem changes using actual child positions. They establish those concrete contexts, not all possible relative units or layout dependencies.

## Percentage-context discrepancy

Four Chromium failures in the 128-assertion authored fixture refer to one unchanged negative case: `repeat(calc(50% / 10px), 10px)`. The independent standards review reads Values 4's calculation-context rules as giving a non-math `repeat()` count argument its own integer context, with no Grid-defined length-percentage hint. Under that reading percentage divided by length cannot produce the required scalar. A percentage divided by a percentage can cancel without borrowing a length basis.

The negative expectation is retained as a documented **draft interpretation / implementation discrepancy** with Chromium 150. It is not described as a universal browser bug, and these four interpretation-dependent authored assertions are excluded from any claim of shared-WPT superiority. The original fixture and Chromium outcomes are preserved.

## Full native gate and inherited temporary-directory limitation

The first unchanged 125-test run, 10:40:44.374300–10:41:21.468615 UTC, produced **124/125**. Its sole failure was `android_dependency_plan`: the existing dependency planner calls `Path.resolve()` on the NDK/work paths, while the existing test compares its canonical output with the unresolved `TemporaryDirectory` spelling. A project-backed short symlink had been supplied as `TMPDIR` for browser profile compatibility, exposing this assumption.

Both `tests/test_android_deps.py` and `tools/android/build_deps.py` are byte-identical to initial baseline `e6b7c5f`. The respective SHA256 hashes are `e05dddaf41b674c9e5ee7028eace199e8d96526ff29ca592a83fed6df1eb22a4` and `8e1ef7af3717eacdb81568a0f92bbc7089ae1bc29a2ead6a277778498a5e33f0`. This is an inherited tooling limitation, not an engine assertion change. No fixture expectation was modified or omitted.

The complete same 125 tests were rerun with physical `TMPDIR=/tmp`, removing inherited `LUMEN_WPT_CTRL` and `LUMEN_PRE_FILE`, and passed **125/125** in 33.89 seconds of CTest time (34.10-second monitored interval). The original failed run, its driver/environment and corrected run remain separately recorded. A canonical-path fixture cleanup, if made later, belongs to a separate change.

The corrected run has 34 one-second process-tree samples: median CPU 113%, peak 223%, peak RSS 431.17 MiB and peak PSS 262.52 MiB. Shared compilation/test CPU remains capped at 480%. Runs overlap separately monitored browser work; timings are not isolated performance benchmarks, and summed RSS can double-count shared pages.

## Provenance and retained evidence

Frozen binary SHA256: `c5c1707e327ef6354ef7b04a59cd0bb63679ea9d4f088c366043c9f6c3f97c53`.

Source-manifest SHA256: `c0316d6cdda73ef4ca24fbd7dbf6dcf8ffbb7f27c2aec0df9a22eef03685e1ed`.

The repository lead preserved all 559 exact source files at `agent/progress/runtime/private-prepared/source-snapshots/iteration-141`. `agent/progress/runtime/count141-ready.json` records the owned source hashes. `agent/progress/runtime/session2-iteration4-repeat-count.json` records times, metrics, limitations and evidence SHA256 values. `count141-environment-audit.json` records the inherited path comparison.

Evidence root: `agent/validation/linux-wpt-20261028/session2/css-grid-parser`.

- `repeat-count{128,135,138,141}.json` and `chromium-repeat-count-observation.json`: original 16 observations.
- `candidate141-native-repeat.json`, `candidate141-count-regression.json`, `candidate141-count-context.json`: authored outcomes; matching Chromium files remain alongside them.
- `candidate141-comparison.json`: exact unchanged 37-URL identities, every gain/loss and original result hashes.
- `native141/`: original failed full gate; `native141-physical-tmp/`: corrected full gate and one-second resources.

External native pages use the sealed capture guard and isolated profiles. New probes explicitly remove controller defaults and do not access the compromised broad-run controller. This iteration's independent scoped evidence is separate from the diagnostic-only primary-136 run. Source, raw results and archives remain private; the live dashboard publishes aggregate status only.

## Primary standards

- https://drafts.csswg.org/css-grid-1/#repeat-syntax
- https://drafts.csswg.org/css-grid-2/#repeat-syntax
- https://drafts.csswg.org/css-values-4/#calc-type-checking
- https://drafts.csswg.org/css-values-4/#calc-range
- https://drafts.csswg.org/css-grid-3/
