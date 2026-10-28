# Iteration 148: property-aware CSS Typed OM values

## Outcome and scope

The exact primary-sample regression for `grid-area: a / a` is repaired. The unchanged nine-URL Typed OM cohort improves from **471/542 to 479/542 passing assertions**, with **eight existing-identity gains, no losses, all 542 identities retained, and all nine harnesses OK**. Two URLs fully pass, versus one before the repair; the same installed Chromium reference passes 507/542 assertions and four URLs. The full native suite passes **131/131**. The final authored fixture passes **62/62**, versus native 146 at 32/62 and Chromium at 60/62.

Proposed scoped quality is **96/100**, pending final PM acceptance. The whole browser goal remains **19/100**, a subjective readiness judgment distinct from test percentages. Candidate 148 includes other unaccepted changes; the separate supplemental-grid and module-error-ordering regressions still block combined release.

## Why the change is necessary

Frozen builds isolate the lost upstream assertion to candidate 135's correct CSSOM shorthand canonicalization. Build 128 passes four of seven `grid-area.html` assertions, while 135, 141, 142 and 146 pass three. Canonicalizing `a / a` to `a` exposes an inherited Typed OM classifier that mistakes the resulting shorthand token for `CSSKeywordValue`; the direct `a` case already failed in 128. The repair preserves CSSOM canonicalization and makes Typed OM classification property-aware.

Grid placement shorthands now reify as direct `CSSStyleValue` objects even when their canonical text contains one token. Numeric and structured grid-line longhands likewise remain direct values; genuine single custom-ident and `auto` line values keep their keyword class. Existing custom-property, CSS-wide and pending-variable handling remains ordered before the new property rules.

All newly or previously reified direct values carry a private associated-property record in a `WeakMap`. The shared set/append path rejects reuse for a different property before serialization, while same-property roundtrips remain valid. This preserves ownership without exposing writable association state or retaining otherwise unreachable values. Independent reviews by repository_lead and standards_comparison approved the exact patch.

## Validation and interpretation

| Gate | Native baseline | Candidate 148 | Chromium reference |
|---|---:|---:|---:|
| Unchanged nine-URL WPT assertions | 471/542 | 479/542 | 507/542 |
| Fully passing URLs | 1/9 | 2/9 | 4/9 |
| Final authored controls | 32/62 | 62/62 | 60/62 |
| Full native CTest suite | Separate prior suite | 131/131 | Not applicable |

The authored fixture covers canonicalized grid-area/row/column values, numeric and named line values, computed-map reads, custom properties, CSS-wide and pending-variable values, unrelated width/opacity/display/animation values, same-property roundtrips, and cross-property rejection through both set and append. All original authored versions and outcomes are retained. An early append setup incorrectly used a non-list-valued property; that premise error is documented and only the setup was corrected to background-size before the final fixture was frozen.

Two retained authored checks differ from Chromium 150: same-property background-size append returns one direct value instead of two, and its CSSOM string joins the two pairs with spaces instead of a comma. Both expectations already pass native 146. They are separately recorded observations, not new gains or evidence of shared-WPT superiority; no expected value was weakened merely to match Chromium.

Independent standards review supports both retained append expectations under the current draft: parsing first subdivides list iterations, append adds the associated representation as another entry, and getAll reifies both. The Backgrounds 3 `background-size` grammar is a comma-separated `<bg-size>#` list. The pinned helper named runListValuedPropertyTests currently contains a TODO for actual list tests; its name is not treated as evidence of append coverage. No authored expectation changed after this review. See <https://drafts.csswg.org/css-backgrounds-3/#the-background-size>.

The current Typed OM draft leaves some Grid-specific entries incomplete. The exact subclass profile is therefore supported by the pinned unchanged WPT tests and Chromium observations. The general direct-value associated-property rule is independently specified in CSS Typed OM sections 2.1 and 5.2: <https://drafts.css-houdini.org/css-typed-om-1/#reify-as-a-cssstylevalue>.

The native gate ran from 11:54:36.823213 to 11:55:40.034704 UTC with two CTest jobs and a canonical physical project TMPDIR. One-second process-tree sampling captured 63 samples, median CPU 71.0005%, peak CPU 185.001%, peak aggregate RSS 289.841 MiB and peak PSS 190.867 MiB. RSS can count shared pages more than once; short-lived processes may fall between samples. Other frozen browser gates ran concurrently, so this duration is not an isolated performance benchmark.

All native author-URL launches used the sealed capture guard. The authored launcher and upstream cohort joined the real cleanup workers, retained the genuine exit audit, and reported no remaining cleanup threads or source drift. These private probes did not access the primary WPT controller.

## Evidence and provenance

The structured companion record contains exact source, fixture, patch, binary and evidence hashes, gate commands, timestamps, cleanup results and resource observations. The frozen binary SHA-256 is `8f00ced4e6341e289365065ba157e6ed11b338674128ad1892a333e07d211438`; its exact 565-file source snapshot is retained privately at `agent/progress/runtime/private-prepared/source-snapshots/iteration-148`.

The complete evidence directory is `agent/validation/linux-wpt-20261028/session2/typed-grid-regression/`. Principal files are `candidate148-comparison.json`, `candidate148-authored-results.json`, `candidate148-parser-drain.json`, and `native148/{completion.json,ctest.log,ctest-lasttest.log,resources.csv,resources.summary.json}`. The full 148 LastTest log contains all 131 per-test records.

A separate late copy intended to preserve 147 per-test output actually copied 148's startup test because the subsequent build had already replaced CTest's LastTest log. Its bytes are preserved with an accurate startup filename and an explicit correction in `used-grid-tracks/lasttest-copy-provenance-correction.json`; it must not be cited as 147 full-suite evidence. The original retained 147 completion, run log and resource data remain valid.

## Remaining limits

This change does not complete Typed OM, cross-realm value branding, Grid Level 3 syntax, or the browser's broader conformance goal. The earlier 42 tentative Grid 3 parsing losses remain explicitly classified in their original cohort; they are outside this nine-URL Typed OM repair. No upstream expectations, test denominators, or CSSOM shorthand serialization were changed.

## Final combined-binary holdout

Frozen candidate 150 retains479/542 assertions across the same nine URLs, with exact zero gains or losses versus 148 and all 542 identities preserved. All nine harnesses are OK and two URLs fully pass. The final150 source/binary record and complete cleanup audit are retained in `session2/final-css-holdouts150/comparison.json`. This validates retention of the scoped repair after the independent module correction and placement rollback; combined broad release remains separately gated.

## Final native gate update

The immutable build 150 full native suite completed 132/132 checks with exit 0 from 12:02:46 to 12:04:01 UTC (74.87 seconds in CTest; 75 one-second resource samples). The 33 unchanged Grid rollback controls also restore all 21 geometry losses and exactly match build 145. Supplemental 200, final broad, rendering and dedicated performance gates remain pending; this completion is not a claim of full WPT parity.

## Accepted combined source and supplemental gate

Source 150 is accepted as commit `0f62172b08683c7c0d89722c588af6ff94f82ca3`, with all 584 compiled files matched to Git. The sealed supplemental cohort passes 1,767/3,370 assertions and 77/200 URLs, with all 200 harnesses OK: 554 existing gains and zero losses versus 128, and 128 gains with zero losses versus 139. The scoped correction is accepted; final primary, expanded, rendering and dedicated performance measurements continue separately.
