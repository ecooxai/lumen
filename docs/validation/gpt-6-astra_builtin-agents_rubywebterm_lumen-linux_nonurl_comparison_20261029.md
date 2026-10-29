# Lumen Linux: non-URL conformance and Chromium comparison

**Candidate 200 is an accepted development checkpoint. The 98/100 whole-browser goal has not been achieved or certified.** The exact-final focused regression gates and unchanged 640-variant comparison are complete. Against the preserved Lumen 176 baseline, the final build gains 23 fully passing cases and 251 semantic assertion passes with no lost passes. This report keeps authored controls, upstream WPT assertions, native tests and benchmark checks separate.

## Exact tested identities

| Component | Identity |
|---|---|
| Lumen final candidate | 200 |
| Build source commit | `a4c26febf736ef6334f05421e1c0d69b24e5a411` |
| Lumen executable SHA-256 | `9ad329ef2ba4282bc7574f34575ed6405f315f7e1a5088d956b776ccfa8d3927` |
| Chromium / driver | 150.0.7871.114 |
| Chromium ELF SHA-256 | `2d4adea820c95eee0c24dcc5eb729043df570838b8693fa3449df15040b0460f` |
| Chromium launcher SHA-256 | `d176751de81c10df1a96052f897155b2b055a8426cddd102f5a1ad102832d5d9` |
| WPT revision | `0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb` |
| Original fixed 640-manifest SHA-256 | `8b5d0d491ce8b14e6ee8107d1213c10b90127a0193207d06057216bd54f3243f` |
| Rendering in these conformance runs | CPU / offscreen |

The launched Chromium process was bound to the actual ELF, separately from its launcher. Hardware GPU rendering is not established by these measurements. The final Lumen source, executable and test selection are immutable throughout the comparison.

## Frozen broad comparison

The sample was selected before outcomes: deterministic hashing within 24 declared category buckets, then round-robin selection to 640 variants. There are 23 nonempty buckets. It covers HTTP testharness document variants, including generated `.any` and `.window` wrappers. Dedicated URL/navigation, secure-context, worker-only, visual/manual and declared long-timeout cases are excluded. Shared form validity cases using `type=url` remain; no URL parsing repair is claimed.

| Browser checkpoint | Fully passing cases | Passing reported assertions |
|---|---:|---:|
| Lumen 176 baseline | 357 / 640 (55.78%) | 8,073 / 10,540 (76.59%) |
| Lumen 187 anchor | 368 / 640 (57.50%) | 8,206 / 10,540 (77.86%) |
| Chromium reference | 544 / 640 (85.00%) | 9,516 / 10,549 (90.21%) |
| Lumen 200 final | 380 / 640 (59.38%) | 8,324 / 10,540 (78.98%) |

Between 176 and 187, the same sample gained 11 fully passing cases and 133 semantic assertion passes, with no lost passes, missing/added assertion identities, or harness-status changes. The final 200 run completed at 20:54:49 UTC. Against176 it gains 23 fully passing cases and 251 semantic assertion passes; against 187 it gains 12 cases and 118 semantic assertion passes. Both joins have zero lost semantic passes, zero missing/added identities, zero completion-only changes and zero harness-status changes. Historical rows remain intact.

A fully passing case requires harness `OK`, at least one reported assertion, and every assertion `PASS`. A zero-assertion case is visible but does not count as fully passing. Assertion identity is the case path and variant, assertion name, and duplicate occurrence index. Missing assertions remain in the union. Harness timeouts are separate from the fixed 15-second external deadline. Two browser workers are used.

The final 200/reference union contains 10,554 assertion identities:14 missing in Lumen and five missing in Chromium. Lumen 200 retains 7,749 of Chromium's9,516 passing assertions (81.43%);1,767 Chromium passes are not Lumen passes. There are575 Lumen-only passes. Full-case overlap is358 both,186 Chromium-only,22 Lumen-only, and 74 neither.

Final Lumen harness outcomes are616 `OK`,23 harness timeouts and one harness error, with zero external deadlines and one zero-assertion case. These counts are unchanged from both Lumen anchors. Chromium has623 `OK`,13 harness timeouts, three external deadlines and one error. These nonpasses remain in the reported denominators.

## Category-level remaining gaps

| Category | Cases | Lumen full cases | Chromium full cases | Lumen passing assertions | Chromium passing assertions | Chromium-pass gaps |
|---|---:|---:|---:|---:|---:|---:|
| css-values | 35 | 9 | 31 | 1173/1778 | 1725/1778 | 552 |
| css-sizing | 35 | 7 | 33 | 307/717 | 712/717 | 405 |
| css-grid | 35 | 8 | 28 | 462/721 | 686/721 | 224 |
| domparsing | 34 | 15 | 21 | 844/1281 | 551/1281 | 158 |
| css-variables | 34 | 8 | 30 | 138/299 | 267/299 | 147 |
| css-flexbox | 35 | 17 | 35 | 456/568 | 568/568 | 112 |
| cssom | 34 | 24 | 30 | 510/544 | 537/544 | 32 |
| fetch-basic | 31 | 19 | 29 | 350/385 | 374/385 | 30 |
| css-selectors | 34 | 20 | 24 | 332/375 | 347/375 | 16 |
| encoding | 34 | 30 | 30 | 1062/1066 | 1015/1078 | 16 |
| xhr | 34 | 25 | 31 | 90/105 | 102/105 | 15 |
| html-parsing | 34 | 20 | 25 | 105/127 | 114/124 | 13 |
| fetch-response | 31 | 25 | 26 | 276/290 | 273/290 | 13 |
| dom-events | 35 | 18 | 19 | 196/224 | 202/224 | 10 |
| dom-nodes | 35 | 27 | 33 | 270/280 | 277/280 | 7 |
| timers | 16 | 9 | 15 | 18/25 | 24/25 | 6 |
| dom-traversal | 15 | 12 | 14 | 51/56 | 55/56 | 4 |
| event-loops | 8 | 3 | 6 | 4/9 | 7/9 | 3 |
| dom-collections | 12 | 10 | 11 | 57/60 | 59/60 | 2 |
| forms-constraints | 27 | 24 | 26 | 785/790 | 787/790 | 2 |
| dom-ranges | 16 | 16 | 16 | 174/174 | 174/174 | 0 |
| fetch-headers | 11 | 11 | 11 | 217/217 | 217/217 | 0 |
| fetch-request | 25 | 23 | 20 | 447/449 | 443/449 | 0 |

A Chromium-pass gap is a reference `PASS` identity that is failing, incomplete or missing in Lumen. It is not the same as subtracting the two browsers' total pass counts. The declared DOM-attributes bucket selected no cases; it receives no coverage credit.

## Improvements and exact-final regression gates

The accepted changes address shrink-to-fit percentage edges, border-box floors, three-arm affine `clamp()` handling and grammar, table spacing, typed transform-angle validation, intrinsic width constraints, and a bounded RTL atomic-layout correction. CSSOM declaration scanning preserves quoted strings, escapes and comment boundaries.

Forms now separate validity flags from validation eligibility, include detached and externally associated radio peers, use bounded decimal step checks, and support selection-aware text/HTML insertion with UTF-16 length limits. Networking fixes cover Response cloning without changing public default-stream tee identity, Latin-1 header bytes, repeated XHR header combination, coercion order, public constructor iteration, and cache modes that avoid forbidden network activity.

| Frozen scope, measured on final 200 | Earlier checkpoint | Final matching/passing observations |
|---|---:|---:|
| Forms upstream24 | 522 at 183 | 647 / 649 |
| Forms authored eligibility | Retention | 8,127 / 8,127 |
| Numeric/radio authored | 541 at 186 | 572 / 588 |
| Numeric tolerance authored | 66 at 189 | 96 / 96 |
| Response.clone authored stable reference | Retention | 114 / 114 |
| Response.clone authored normative error propagation | Retention | 5 / 5 |
| Network upstream22 | 255 at 190 | 317 / 317 |
| Header neighboring upstream5 | 9 at 196 | 11 / 11 |
| Header/body authored bytes | Retention | 120 / 120 |
| Header conversion/state authored | 188 at 196 | 235 / 235 |
| Private cloning/public iterator authored | 27 at 196 | 42 / 42 |
| Editing insertion authored | 718 at 193 | 1,021 / 1,034 |
| Editing coercion authored | 96 at 194 | 160 / 160 |
| Editing Symbol conversion authored | 50 at 194 | 64 / 64 |
| CSSOM canonical-px authored retention | Retention | 2,447 / 3,268 |
| CSSOM scanner authored | 164 at 186 | 196 / 200 |
| CSS quoted-string authored | 24 at 183 | 27 / 27 |
| Angle parsing/computed upstream5 | 31 at 193 | 89 / 184 |
| Angle authored positives/negatives | 401 at 193 | 473 / 832 |
| Table spacing authored | 504 at 184 | 960 / 960 |
| Intrinsic flex keyword upstream2 | 40 at 194 | 76 / 148 |
| Intrinsic width/mutation authored | 2,724 at 194 | 3,352 / 3,528 |
| Nested intrinsic edge preservation authored | 252 at 196 | 270 / 672 |
| RTL atomic retention authored | 288 at 197 | 332 / 336 |
| Node.removeChild upstream1 | 27 at 184 | 28 / 28 |

Every previously accepted observation in the final authored retention bundle is unchanged; all declared identity joins have zero lost passes and no missing/added fields. The fresh native gate passes **140/140**, with the three dedicated URL unit tests excluded. The cache tests preserve existing privacy, authorization and variation checks while adding real server hit counters, stale/miss/no-network cases, redirect metadata, and old-format cache compatibility.

The four original intrinsic/RTL gates are kept distinct. The 672-field nested-edge corpus retains its strict tolerance and known percentage-serialization/quantization differences; it is not evidence that every differing field is a used-pixel layout error.

## Rejected intermediate changes and retained limitations

Candidate 193 was rejected after an unsolicited HTTP 304 response lost its streaming head callback. Candidate 194 restricts suppression to actual cache revalidation and restores the native 140/140 gate, including 258 zstd sync/stream/sink checks. Candidate 197 was rejected for 16 new intrinsic/RTL observation failures. Candidate 198 restores those passes and adds the independently frozen RTL retention controls. Neither rejected candidate is described as accepted.

Editing still has 13 reference differences: one multiline textarea event-granularity field, four disabled-control focus fields, six email-input editing fields, and two number-input return-value fields. Two upstream dynamic validity-selector assertions remain failing. The numeric/radio corpus retains 16 interoperability differences.

One numeric mismatch needs particular care: `negative-min-base / step-up / value` starts at −0.35 with min −0.05 and step 0.1, then invokes `stepUp()` exactly once. Chromium returns +0.05; Lumen returns −0.05. The current [HTML stepUp algorithm](https://html.spec.whatwg.org/multipage/input.html#dom-input-stepup) supports Lumen's minimum-clamping result. The frozen Chromium mismatch remains in the interoperability score; no special case or expectation rewrite was introduced.

Other known gaps include unsupported intrinsic height/stretch behavior, mixed-bidi coordinates, some valid transform/math serialization and geometry behavior, and broad unrun platform categories. `EventSource` is undefined in the checked runtime; SSE is unsupported.

## Reference and lifecycle qualifications

- Of the 574 Lumen-only assertion passes at checkpoint 187, 451 are DOM parsing cases dominated by newer `afterHTML`/`appendHTML` APIs absent in this Chromium 150 reference. These are captured API-availability differences, not proof of global superiority.
- The authored form reference has two reversed-time validation-message nonpasses. They remain visible alongside Lumen's 8,127/8,127 result.
- Response cloning has **114 stable Chromium-comparable fields** and **five separately declared normative error-propagation fields**. Chromium 150 stalls on the isolated uncloneable function-chunk case; the five normative checks are not counted as five reference-comparable passes.
- The 22-file network reference is 315/317. Its two retained failures concern User-Agent filtering and trailing header whitespace during combining. Lumen 200 passes 317/317 in this fixed cohort.
- The five-file angle reference is 174/184. Nineteen assertion names on each side are asymmetric because test names embed runtime serialization; these identities are retained rather than normalized away.
- The original 187 controller left 638/640 actual native exit receipts and two inactive task-owned profiles despite no surviving browser processes. Metadata was preserved before those exact profiles were removed. No synthetic exit receipts were created. The supported cleanup-joining wrapper is used for later runs.

Every final focused native browser capture has an actual exit-code-zero receipt, removed profile, joined cleanup workers, and an unchanged source seal. The final 640 run has 640 actual exit-code-zero receipts,640 removed profiles, no remaining owned native roots or cleanup workers, and two unchanged source receipts. Both shard controllers exited0. This proves capture/cleanup integrity; it does not turn failed assertions into passing tests.

## Performance

The final fixed benchmark uses seven balanced measured trials each of immutable Lumen 176, Lumen 200 and Chromium, with 9,395 correctness checks per trial. All21 trials pass. No separately launched warmup trials were added; the public helper does not declare its internal warmup count. Every timing sample and outlier is retained.

| Measured phase | Lumen 176 median ms (range) | Lumen 200 median ms (range) | Chromium median ms (range) |
|---|---:|---:|---:|
| DOM create/remove | 42.61 (40.02–44.21) | 41.41 (39.73–56.11) | 11.40 (9.80–18.50) |
| Style/layout | 361.11 (349.73–395.32) | 356.53 (343.25–388.83) | 58.40 (53.30–72.60) |
| Grid/cache rebuild | 148.21 (134.52–154.88) | 142.21 (132.74–151.98) | 49.00 (44.80–62.70) |

The before/after Lumen ranges overlap and pair directions are mixed. **No stable broad speedup is established. Chromium remains faster in these measured phases.** These are workload-specific timings, not a claim about all browsing.

| Sampled browser tree memory | Lumen 176 | Lumen 200 | Chromium |
|---|---:|---:|---:|
| Summed RSS, MB | 164.42 | 165.22 | 935.91 |
| Sampled PSS, MB | 139.27 | 139.97 | 382.45 |

Memory uses decimal MB and the **median of seven per-trial sample medians**. Sampling covers startup, workload and cleanup, with four memory samples per native trial and three per Chromium trial. It is not steady idle, whole-system RAM, or a median of per-trial peaks. Summed RSS double-counts shared mappings; PSS is sampled proportional memory. CPU was sampled every second but the short whole-trial samples do not establish phase-specific CPU efficiency. All14 native benchmark exits are0; all 21 profiles are removed and all owned identities are gone.

An earlier182→183 fixed10,000-node style workload measured the clamp representation's memory tradeoff: `ComputedStyle` grew from 1,368 to 1,640 bytes (+272), settled RSS from 187.970 to 190.374 MB, and sampled PSS from 162.889 to 165.482 MB. Settled CPU remained 1% in both. This is a specific workload cost, not a claim about every page.


An earlier controlled 194→195 experiment isolated the canonical-pixel CSSOM setter shortcut. All 3,268 semantic observations were unchanged. The setter-phase median fell from 58.536 to 35.459 ms (39.4%), with all seven pairs lower, but the following direct-attribute phase slowed in every pair. The separate full workload had overlapping ranges and an inconclusive net effect. Fixed phase order and shared-process effects limit attribution; no overall speedup is claimed from that experiment.

Resource measurements distinguish CPU time, wall time, RSS and sampled PSS. Summed RSS can double-count shared mappings, especially for Chromium's process tree. CPU rendering and the same task workload are explicitly identified.

## Next implementation priorities

The largest remaining selected gaps are CSS values, sizing, grid, variables and flex behavior. The following whole upstream files provide concrete starting points; they are preserved failures, not newly selected tests.

| Whole public WPT file | Chromium-pass gaps |
|---|---:|
| `/css/css-sizing/stretch/block-height-008.html` | 160 |
| `/css/css-values/calc-size/animation/interpolate-size-min-height-interpolation.html` | 120 |
| `/css/css-values/attr-all-types.html` | 116 |
| `/css/css-grid/abspos/orthogonal-positioned-grid-descendants-008.html` | 100 |
| `/css/css-sizing/contain-intrinsic-size/contain-intrinsic-size-logical-003.html` | 48 |
| `/css/css-values/calc-size/animation/interpolate-size-max-width-interpolation.html` | 48 |
| `/css/css-sizing/stretch/stretch-max-inline-size-001.html` | 40 |
| `/css/css-values/sin-cos-tan-serialize.html` | 36 |

These include unsupported stretch/intrinsic-height behavior, typed attributes, orthogonal positioned-grid layout, calc-size interpolation and non-finite math serialization. Animation and new sizing families require broader design work than another isolated parser check. Dedicated URL conformance remains deferred.

## Reproduction and evidence

The accompanying public manifest lists all 640 exact relative WPT paths/variants and pinned source hashes. Its SHA-256 is `c7a3d26a0b69a013f012905edc1054d8ce04fcf4581d452db765bafe2679c1e1`. The unchanged final comparator summary has SHA-256 `42cb9682263f9ac73b090cb0f5253d4e161743f221cbcf16bfa7d586b66e75a2`; its whitelisted changed-identity companion contains every semantic and full-case gain/loss against 176 and 187. The JSON report contains the full category and identity-count definitions. Upstream raw results and guarded lifecycle logs remain preserved in the original project; only reviewed public selection and aggregate fields are released here.

The project uses `tools/codex_compile.py` around its supported `agent/validation/run_guarded_cohort.py --runner paired_wpt` interface. Approved capture setup is documented by `agent/validation/README.capture-refusal.md`; protected implementation files are not modified or exported by this report. The final native command is `ctest --test-dir build/codex-native --output-on-failure -E '^(url|url_host_nul|data_url)$' -j2` after a fresh all-target build.

**A selected passing percentage is not an overall browser-readiness score. The final handoff must retain the unresolved failures and excluded coverage; it does not certify Chromium parity or a 98/100 browser.**
