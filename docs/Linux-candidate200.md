# Lumen Linux candidate 200 — reviewed final source

## Identity and recovery

The tested Ruby source is `a4c26febf736ef6334f05421e1c0d69b24e5a411`. The native executable SHA256 is `9ad329ef2ba4282bc7574f34575ed6405f315f7e1a5088d956b776ccfa8d3927`. All 642 recorded source-set entries and the 710 exported Git-backed files were independently compared against that source. The public export follows the already public ancestor and the reviewed 186/196 export commits; it is not the original private working history.

The source tar includes a self-contained `PUBLIC-HISTORY.bundle`, `RESTORE-GIT.md` and `ARCHIVE-PROVENANCE.json`. The packaging check clones that bundle offline into a fresh directory and compares its commit, tree, tracked blob identities, paths and executable modes. Follow the included restore recipe. The export itself was not separately rebuilt and contains no portable Linux executable.

For build provenance, use the existing project README/CMake build definitions and `docs/build-dependencies200.json`, which records the actual dependency versions and settings reused on Ruby. Paths describe the validation environment and may need adaptation elsewhere. V8, WebGPU/SDL and native libraries are external dependencies; no dependency cache, browser profile or binary is included in the source archive. The runtime validation procedure depends on the existing operator-managed capture configuration and is intentionally not redistributed as private guard/authentication implementation.

## Final fixed compatibility comparison

The same frozen 640 non-URL HTTP-document variants at WPT revision `0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb` yielded:

| Browser | Complete selected cases | Reported passing assertions |
| --- | ---: | ---: |
| Original Lumen 176 | 357/640 | 8,073/10,540 |
| Final Lumen 200 | 380/640 | 8,324/10,540 |
| Chromium 150.0.7871.114 | 544/640 | 9,516/10,549 |

Final 200 gained 23 complete cases and 251 semantic passing assertion identities over 176, with zero lost, missing or added identities and no harness-status changes. Relative to 187, it gained 12 complete cases and 118 assertions. All 640 final native children exited zero, all profiles were removed, no owned roots/workers remained and source identities stayed stable. Reference failures and denominator differences remain visible. The original 187 receipt defect is preserved historically; later cleanup audits completed.

The exact public variants, categories and pinned source hashes are in `docs/wpt640-manifest.json`. The chosen scope is not all WPT, and localhost-origin constraints remain. The selected-sample score is 59.375/100, while Chromium's case-completion score in the same sample is 85/100. Neither is a whole-browser readiness score. The requested 98/100 goal was not achieved; whole-browser parity is not certified. URL conformance remains deferred.

## Focused repairs and retained gates

The final native 140-test gate and all 14 authored cohorts retained every accepted observation. Candidate 200's network scope passed 317/317 assertions; five neighboring files passed 11/11; cloning controls passed 42/42, conversion controls 235/235, and original header/body controls 120/120, without losses.

Editing 199 improved from 718 to 1,021/1,034 fields, coercion from 96 to 160/160 and Symbol controls from 50 to 64/64, with zero losses. The maxlength emoji assertion now passes, raising upstream forms to 647/649; the two remaining upstream cases concern validity selectors. Thirteen authored editing differences remain: one multiline textarea event-granularity field, four disabled-control focus fields, six unsupported email insertion fields and two number-input return fields. Authored form eligibility remains 8,127/8,127.

Other exact-final retained results include table spacing 960/960, CSSOM scanner 196/200, strings 27/27, canonical length 2,447/3,268 reference matches (all 821 preexisting differences retained), authored intrinsic geometry 3,352/3,528, upstream intrinsic assertions 76/148, angle assertions 89/184 and removeChild 28/28. These scopes overlap in places and must not be added together as a global pass rate.

The 197 intrinsic-width prototype was withheld after 16 authored geometry regressions; 198 restored every regression before acceptance. The 193 cache change failed a native zstd gate; the separate 194 repair restored 140/140 native tests and 258/258 zstd controls. Failed provisional candidates remain explicitly recorded rather than presented as clean successes.

## Final fixed runtime comparison

The unchanged correctness-gated workload ran 21 measured trials, seven each on 176, 200 and Chromium, in balanced serial order. Every trial passed 9,395 checks. There were no separately launched warmup trials; the public protocol metadata does not separately enumerate internal warmups. Three native 200 media captures preceded the quiet interval, and no cold-start comparison is claimed.

| Median phase time (ms) | Lumen 176 | Lumen 200 | Chromium |
| --- | ---: | ---: | ---: |
| DOM create/remove | 42.61 | 41.41 | 11.40 |
| Style/layout | 361.11 | 356.53 | 58.40 |
| Grid cache/rebuild | 148.21 | 142.21 | 49.00 |

Native ranges overlap; final times were lower in 4/7, 5/7 and 5/7 matched triplets respectively. No stable broad speedup is established. All values, ranges and pair effects are preserved in `docs/performance200-summary.json`. Chromium remains faster in these measured phases.

One-second browser-tree memory statistics use the median of each trial's sample median, then the median of those seven trial medians. They are not medians of maxima. Decimal-MB RSS/PSS values are 164.416/139.274 for 176, 165.218/139.974 for 200 and 935.907/382.451 for Chromium. Native trials have four memory samples and three CPU-interval samples each; Chromium has three and two. Sampling spans startup, work and cleanup. Summed RSS double-counts shared mappings; sampled PSS is proportional memory. These values do not describe steady idle or whole-system memory.

All 14 native actual-exit receipts were zero with profile removal; all seven Chromium owned-identity lists were empty and exact owned profile paths were absent. Source and all binary identities stayed unchanged. The validated backend is CPU/offscreen. Intel adapter enumeration does not establish actual Lumen hardware rendering, and accepted transform syntax does not establish transformed bounding rectangles or general painted-pixel correctness.

The separate 195 setter diagnostic showed a 39.4% median CSSOM-mutation reduction in all seven pairs, while the subsequent direct-attribute phase slowed in every pair. Fixed-order/shared-process effects limit its interpretation. Its broader workload effect remained inconclusive. That version-specific experiment and its frozen phase fixture are retained separately.

## Delivery limits

The native executable remains on Ruby; this source export and the cloud progress page are separate deliverables. The official Surge CLI was installed in task-owned storage, but no authenticated session was available. The AGENTS-required desktop restart was not performed because `/tmp/lumen-display0.pid` was absent and no verified existing launcher/profile recipe was available. No unrelated process was terminated, no guessed profile was launched and the user's display was not inspected or recorded. Native showcase frames came from the application's own guarded CPU/offscreen output for a synthetic local fixture.

Private task text, diaries, raw QA, profiles, inbox/capabilities, credentials and protected capture infrastructure are excluded from the public source history. Fresh final public aggregate/identity-change records are included alongside this document when the release is sealed.
