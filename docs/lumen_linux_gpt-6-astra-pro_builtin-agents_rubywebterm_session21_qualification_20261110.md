# Lumen Linux: accepted checkpoint 2104

**The selected target now passes all 48 assertions in the three unprefixed border-radius parsing and computed-value WPT files.** The accepted Linux binary also passes the separate 25-assertion prefixed companion, all 222 native test groups, the required historical regression gates, and an independently checked automatic redraw in a real native window.

The broader frozen sample is **819 / 1,248 complete files** and **23,364 / 26,245 passing assertions (89.02267%)**. Compared with the previously accepted 2007 replay, this adds **35 passing assertions and six complete files**, with no lost prior passes, registrations, complete files, or successful harness outcomes. These are measured results for the stated scopes. They do not establish all-WPT, all-border-radius, Chromium-wide parity, or the broader 98% goal.

This report qualifies **candidate 2104 only**. Subsequent optional work is excluded even if the canonical working branch or normal build output has advanced.

## Results and the Chromium comparison

| Scope | Accepted 2007 baseline | Accepted 2104 | Chromium 150.0.7871.114 reference |
|---|---:|---:|---:|
| Three unprefixed radius parsing/computed files | 21 / 48 | **48 / 48** | 48 / 48 |
| Separate prefixed radius companion | 20 / 25 | **25 / 25** | 24 / 25 |
| Expanded cohort, complete files | 813 / 1,248 | **819 / 1,248** | 1,060 / 1,248 |
| Expanded cohort, registered assertion passes | 23,329 / 26,245 | **23,364 / 26,245** | 24,517 / 25,969 |

The main target consists of `border-radius-valid.html`, `border-radius-invalid.html`, and `border-radius-computed.html` under `css/css-backgrounds/parsing/`. Their frozen upstream sources contain 23, 11 and 14 assertions respectively. The prefixed file, `webkit-border-radius-valid.html`, is reported separately. The selected-target score is therefore **100/100 for those 48 assertions**; the expanded-sample assertion rate is a separate **89.02267/100** measure.

The raw Chromium run contains three startup failures. Its original result remains **1,060 complete files and 24,517 / 25,969 assertion passes**. A separate preregistered diagnostic recovered 95 registrations: 24 PASS and 71 FAIL. The original full result is never silently replaced by or merged into that diagnostic view.

Raw assertion denominators differ, so they are not sufficient for ranking engines. The independently calculated intersection uses the exact **URL, assertion name, and zero-based occurrence within that URL/name**:

| Original Chromium common identities | Count |
|---|---:|
| Common registered identities | **25,909** |
| Both engines PASS | 22,496 |
| Lumen PASS, Chromium non-PASS | 622 |
| Chromium PASS, Lumen non-PASS | 1,961 |
| Neither engine PASS | 830 |
| Lumen passes on this common set | **23,118 / 25,909** |
| Chromium passes on this common set | **24,457 / 25,909** |

The separately labeled diagnostic-qualified intersection has 26,004 identities: 22,512 both PASS, 649 Lumen-only PASS, 1,969 Chromium-only PASS and 874 both non-PASS. It remains distinct from the original-run comparison.

Download the [machine-readable qualification report](https://ecooxai.github.io/lumen/candidate2104/lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_session21_qualification_20261110.json) and the [sanitized 1,248-row comparison](https://ecooxai.github.io/lumen/candidate2104/lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_session21_wpt1248_comparison_20261110.json). The latter preserves standard WPT paths, per-file registration/pass counts, harness outcomes and aggregate common-identity matrices. It excludes browser capabilities, profile paths, process IDs, environment values and error-message payloads.

## What changed in the engine

### Radius grammar and CSSOM behavior

The repair validates the complete radius value before changing declaration state. It handles one to four horizontal components, an optional slash and one to four vertical components, and one or two components for a physical corner. Invalid slash/token counts, negative literal values, nonzero unitless values and number-valued math are rejected through the existing native validation route.

Specified-value serialization compresses the four physical corners correctly and preserves declaration priority and order. The existing parser's end-of-file closure is followed by the existing math folding logic, so closed and EOF-closed constant expressions serialize consistently. Relative units, unresolved percentages and pending substitution retain their dependence instead of being prematurely reduced.

Computed values still come from the native engine through the existing `N.computed` route. There is no stored-string substitute for native computed geometry. The CSS-wide `all` dispatch gained the missing bounded radius reset; the broad cascade architecture was preserved.

These rules are grounded in [CSS Backgrounds: corner shaping](https://drafts.csswg.org/css-backgrounds/#border-radius), [CSS Values: math-function range checking](https://drafts.csswg.org/css-values-4/#calc-range), and [CSSOM value serialization](https://drafts.csswg.org/cssom/#serializing-css-values). The frozen tests and exact qualification receipts, rather than prose alone, determine this checkpoint's accepted behavior.

### Two-axis native geometry

The native style representation now retains vertical radii as well as horizontal radii. Optional owned vertical storage avoids allocating it for the common case where both computed axes are identical. Parsing, adoption, copying, comparison and destruction follow the actual storage lifecycle.

Painting and clipping consume both axes. Horizontal percentages use width and vertical percentages use height. Overlapping corners share the required normalization factor; the zero-axis and inset behavior retain the independently frozen geometry expectations. Inner border/padding curves use the corresponding side insets and clipped ellipse geometry rather than a second overlap normalization.

The display and application damage paths include the vertical geometry, so a change confined to a vertical radius can invalidate the actual native window. Rounded clipping also propagates through the affected positioned descendants. Hit testing preserves both rounded and zero-radius rectangular clip boundaries, including the retained affine cases that caught an intermediate regression.

### Bounded default image-mask repeat repair

Within the existing background-colour mask approximation, intrinsically sized image masks use one tiled DItem for absent or initial-resolved repeat, repeat, or repeat repeat, so a 1x1 mask covers the intended box. Explicit no-repeat and other authored modes, and fitted contain/cover, retain their previous paths. No per-tile allocation or full CSS Masking conformance is claimed; the original failure and nonuniform-alpha/no-repeat/fitted guards remain preserved.

### Adjacent typed-length validation

The same bounded batch tightens the relevant native background-position length validation. The nearby five background interpolation files were independently frozen and passed all 770 reference-registered assertions. This was a small adjacent repair, not a general rewrite of CSS arithmetic or animation interpolation.

## Validation that qualified the exact binary

| Gate | Accepted result | Qualification |
|---|---:|---|
| Native test groups | **222 / 222** | All 218 previous names retained, plus four new tests |
| Native ownership subchecks | 133 / 133 | Nested native checks, not additional WPT assertions |
| Native rendering/hit subchecks | 157 / 157 | Includes the repaired fixed and rectangular clip cases |
| Native background type subchecks | 30 / 30 | Typed length/math behavior |
| Actual-main damage-signature subchecks | 9 / 9 | Uses the real application code |
| CSSOM, review and geometry controls | **94 / 94** | 93 required checks plus one original retained diagnostic |
| Upstream rendering comparisons | **17 / 17** | Original fuzzy metadata preserved |
| Adjacent background interpolation | **770 / 770** | Five upstream files |
| Supplemental math controls | 8 / 8 | Authored after the initial implementation and frozen before execution |
| Fixed-descendant hit controls | 5 / 5 | Independent native/Chromium controls |
| Late geometry controls | 9 / 9 | Separate authored scope |
| Explicit-pair radius animation | 1 / 1 | Does not establish mixed-arity interpolation |
| Known mixed-arity radius animation | **0 / 1** | Preserved limitation |
| Background controls | 37 required passes | Three separate retained observations in the 40-row scope |

The historical companion gates also preserve:

- Object-position behavior: 75 authored assertions and 123 assertions in three upstream files.
- CSS keyframe behavior: 26 assertions.
- Transfer behavior: all 107 prior passes among 109 registered assertions, plus the supported independent audit.
- Authored animation controls: 53 original assertions plus seven reconciliation assertions.
- Upstream animation selection: all 552 prior passes among 618 assertions in 50 files; 41 complete files and all 50 successful harnesses retained.
- Authentication-related engine controls: 80 plus 19 assertions, without any account login or private-profile work.
- Messaging selection: all 34 prior passes among 39 assertions in 18 files; 15 complete files and all 18 successful harnesses retained.

These scopes overlap and use different units. Their counts must not be added into one conformance denominator.

Two targeted sanitizer groups passed: style/ownership 133 / 133 and rendering/hit 157 / 157. Their stated source/test units were instrumented with ASan/UBSan, with stack-use-after-return enabled for the hit group. The rest of the engine and V8 were ordinary builds; this is not a claim that the entire application was sanitizer-instrumented.

The expanded run executed all 1,248 native browser lifecycles with exit code zero. Its WPT harness outcomes were 1,238 OK, one ERROR and nine TIMEOUT, and assertion outcomes were 23,364 PASS, 2,853 FAIL, seven TIMEOUT and 21 NOTRUN. These remaining outcomes are retained in the report rather than hidden by the successful process exits.

## Real native-window redraw and the recording

The linked accepted application ran in a fresh isolated Xvfb display and task profile. The page contained a 240 by 160 pixel shape. Its top-left horizontal radius stayed at 80px while a single timer changed only the vertical radius from 20px to 80px, 12 seconds after the page's READY marker.

The accepted raw captures began at READY+10.000075s and READY+16.000073s. The first finished at +10.286942s, before the unchanged +11.5s guard. The mutation marker arrived at +12.016350s. All ten independently specified 3 by 3 pixel-patch checks passed. **Exactly 1,127 pixels changed inside the target shape and zero pixels changed outside it.** The fiducial, window geometry, text and toolbar remained stable. Capturing read existing X11 pixels and did not request an application repaint, layout result or computed-style read.

The first capture attempt remains preserved as a timing failure. Its one-shot input rate was 1fps, and both captures took about 3.1 seconds, causing the first completion to cross the frozen boundary. The retry changed only that one input rate to 30fps. The fixture, 12-second setter, +10/+16 capture schedule, quiet interval, pixel oracle and 15fps recording remained unchanged. The first attempt has no accepted pixel verdict: its timing guard failed before the pixel evidence could qualify.

The separate credited video is **25.000 seconds, 1280 by 960 pixels, 4:3**, made from the real native window. The recorder process launched at page READY+5.001811s. The mutation therefore occurred about 7.014539s after recorder launch, so the 8-second video frame already shows the changed shape. The launch timestamp does not separately instrument the first encoded frame. The strict page-relative raw captures provide the pixel proof; the recording illustrates it.

The root reviewer personally viewed the exact before/after images and all 2-, 8- and 20-second credited frames. The independent QA audit decoded the images again and confirmed the oracle and cleanup. The run used software rendering and makes no GPU qualification claim.

All recorded owned leaders and sampled descendants were gone after completion, their process groups had no surviving members, and the fresh task profile was removed. The existing desktop browser and its profile were left intact.

## Intermediate failures were retained

| Candidate | Outcome | Why it was held |
|---|---|---|
| 2101 | Native 220 / 222; selected upstream radius assertions passed | Ownership-test singleton setup and real positioned-clip propagation needed correction; two EOF serialization controls and the bounded `all` reset also failed |
| 2102 | Native 221 / 222; focused parsing/computed controls passed | New fixed-position descendant hit guard failed while paint passed |
| 2103 | 179 PASS and two FAIL among 181 observed of 222 selected; 41 unexecuted | Retained affine hit exclusions failed and a C assertion aborted the remaining run |
| 2104 | Required native, browser, inherited, full-sample and window gates accepted | Exact accepted source and binary recorded below |

No held binary was substituted for the final accepted artifact. The first 2007 full run's HOLD and its later accepted reliability replay remain separately recorded. The original Chromium run, its three startup failures and its diagnostic replay also remain separate.

## Performance and the remaining traversal cost

The full expanded run took 1,354.066 seconds and produced 1,354 one-second resource samples. Its sampled peak process-tree RSS was **630.977 reported MB**, median tree CPU 46.002%, and peak sampled tree CPU 222.998%. Shared-cgroup weighted CPU was 55.468% under the 480% quota, with zero additional throttled periods.

The accepted window/media run took 36.072 seconds. Its sampled peak RSS was **643.764 reported MB**, peak tree CPU 299.834%, and shared-cgroup weighted CPU 48.888%, again with zero additional throttled periods. These are the monitor's reported MB units, not an unannounced conversion to MiB. One-second sampling can miss shorter peaks, and the shared cgroup includes other tasks.

The native hit-test diagnostic used 5,000 ordinary nodes, 20 warmups and 300 measured queries:

| Query case | Held 2102 p50 / p95 (microseconds) | Accepted 2104 p50 / p95 (microseconds) | Correctness |
|---|---:|---:|---|
| No fixed descendant, point outside the clip | 0.157976 / 0.166008 | 293.956022 / 689.315959 | Both correct: 0 expected and observed fixed-target hits |
| Same tree, point inside the clip | 535.161002 / 814.838917 | 571.591896 / 1,113.764010 | Both correct for the no-fixed tree |
| Inline fixed descendant, point outside the ancestor clip | 0.199070 / 0.230968 | 134.222908 / 242.153998 | Old **0 / 300 incorrect**; accepted **300 / 300 correct** |

The old no-fixed outside fast path was correct and much faster. The corrected escape-aware traversal has a real O(n) cost for that case. The old escaped-fixed measurement was fast because it returned the wrong target result; it is not a valid successful-hit baseline. These are the same frozen probe's measurements, not a general browser benchmark or an assertion that all latency improved. A future optimization should preserve the fixed/affine behavior while restoring and independently measuring a correct no-fixed fast path.

Held 2102 report SHA256: `a69329573513e2d037b72b766ed9fd55a7b6754b0077bbe39a8f0acd57b46d05`. Accepted 2104 report SHA256: `2c49d239e931005baf4ef6d599bd6074a3c1439a6e7996661b740ddf76e93906`. Both use the same probe source SHA256 `8a14b416cdd03af6bed209036737bd164a13981308a2ace0fb5cf4ef24aa0071`.

## Sampled compatibility priorities

The following groups use only the 25,909 identities shared with the **original** fresh Chromium full run. They describe gaps in this diagnostic sample, not complete feature-family conformance or estimates of implementation effort.

| Path family | Common assertions | Chromium-only PASS | Both non-PASS | Lumen-only PASS | Both PASS |
|---|---:|---:|---:|---:|---:|
| /css/css-values | 8183 | 1124 | 634 | 179 | 6246 |
| /css/cssom | 3922 | 340 | 55 | 38 | 3489 |
| /css/css-backgrounds | 920 | 151 | 38 | 72 | 659 |
| /css/css-variables | 582 | 111 | 24 | 48 | 399 |
| /dom/nodes | 10455 | 89 | 25 | 70 | 10271 |
| /css/css-animations | 574 | 61 | 5 | 1 | 507 |
| /css/css-syntax | 444 | 42 | 6 | 2 | 394 |
| /css/css-transitions | 252 | 21 | 0 | 2 | 229 |
| /css/css-sizing | 183 | 15 | 28 | 24 | 116 |
| /dom/ranges | 394 | 7 | 15 | 186 | 186 |

Chromium-only PASS can reflect an implementation gap, an existing Lumen harness limitation, an error or a timeout. Both non-PASS is not evidence that either implementation is correct. A repair should start from the exact frozen assertions and independent controls. Dedicated URL conformance remains deferred.

## What remains outside this checkpoint

Mixed-arity radius animation still fails its retained observation. An explicit pair interpolates successfully, but a one-component endpoint transitioning to a two-component endpoint is not established by that result. The proposed follow-on would need a side-effect-free, context-correct computed endpoint resolver; it was deferred instead of adding a second incomplete CSS evaluator.

The existing limitation around partially mutating an opaque pending-variable shorthand is also preserved. This checkpoint does not claim complete CSS cascade, all CSS math, MutationObserver parsing/XML behavior, media, workers, WebDriver, Test262 or whole-platform conformance. Any later callback-realm candidate is separately qualified and is not part of these source or binary identities.

One original authored `removeProperty` expectation remains normative even though Chromium returned a different old-value string. The oracle was not rewritten to match the reference; [CSSOM's removeProperty algorithm](https://drafts.csswg.org/cssom/#dom-cssstyledeclaration-removeproperty) supplies the relevant behavior. The report also retains the original geometry diagnostic and all comparator exceptions.

## Run the accepted browser on the Ruby host

The immutable accepted executable is `build/codex-native/lumen-astra-i2104`. The normal `build/codex-native/lumen` output can change during subsequent development, so the command below selects the accepted binary explicitly.

Run this yourself in a desktop terminal on the project host. It creates a new isolated project profile and does not reuse the existing desktop browser's data:

```sh
cd /mnt/p3/project/browser/lumen-android/lumen-android
lumen_2104_profile="$(mktemp -d "$PWD/build/lumen-2104-profile.XXXXXXXX")"
mkdir -m 700 "$lumen_2104_profile/config" "$lumen_2104_profile/data" \
  "$lumen_2104_profile/cache" "$lumen_2104_profile/downloads" "$lumen_2104_profile/tmp"
XDG_CONFIG_HOME="$lumen_2104_profile/config" \
XDG_DATA_HOME="$lumen_2104_profile/data" \
XDG_CACHE_HOME="$lumen_2104_profile/cache" \
XDG_DOWNLOAD_DIR="$lumen_2104_profile/downloads" \
TMPDIR="$lumen_2104_profile/tmp" \
LUMEN_NO_SESSION=1 \
LUMEN_ICU_DATA="$PWD/build/codex-deps/v8/bottle/v8/15.6.75.8/libexec/icudtl.dat" \
./build/codex-native/lumen-astra-i2104 about:blank
```

The isolation uses the engine's supported XDG/SDL storage path and session environment behavior, not an invented `--profile` option or a changed HOME. The capture profiles were removed after validation; their actual nested SDL path was not retained as an observation. The exact ICU data file is 10,822,000 bytes, SHA256 `319501bd0fdf9c9cb9926c0d0fd64ed95961118af700fbb4342981f088e73e8c`.

The existing display0 browser belongs to another sibling release. The project instruction requires ownership verification before restarting it, so that window was preserved. No private account state was inspected. This checkpoint provides the accepted runnable host binary, source, reports and actual-window recording without replacing that separate desktop release.

The source archive includes source and complete approved public Git history. It does **not** bundle the host's external native/V8 dependencies or constitute a portable binary package. Follow the dependency/build documentation in the source tree for another host.

## Exact identities and recovery

- **Tested engine commit:** `3edec1c75b9e332f16e2e5b0943eb881c3c00e1f`
- **Tested engine tree:** `db46f32170a4ecca76c5d3c82aca369c9fa63cfa`
- **Accepted Linux binary SHA256:** `0e7f3d63259731afedafe2fd717394b49e4fe525d34d9c12b1c437dd5e6789d9` (7,756,616 bytes)
- **Recorded build inputs:** 748, digest `b8b0e73ddfba95353cf9d3dda57437341fad3698258eeebdd3c2978a5f1a17ff`
- **Core independent seal SHA256:** `7e41f44b5faf042c4e0cdf9234f4b944b8b0e2019301991a0fd9b6d778edf068`
- **Window independent seal SHA256:** `8b7f09c9cb204e188fb2d67e459b849d00959e7a953f3b675c652a875ea07a1e`
- **Root visual record SHA256:** `df5092a4f2e22c8f0db7b38a49270665f8c6597edc505d9ec431ec3491c87966`
- **Qualification JSON SHA256:** `68e089240c6a4a711d292438040aaad23dbff1a943a3be06e832dea4e923a903`
- **Sanitized comparison JSON SHA256:** `80bef1da9b031b3a13c2d7785a74a31b20d4d4336e2cf25c7394e5fbdc0a4f95`

The [public source branch](https://github.com/ecooxai/lumen/tree/gpt-6-astra-pro_builtin-agents_rubywebterm_linux-source2104-20261110) is a new descendant of the previously approved public 2007 source history. It contains 815 exact tested source paths, 28 preserved historical public files, five refreshed metadata files and three curated reports: 851 tracked paths. The canonical private Git history, profiles, inbox, capabilities and unreviewed runtime files are excluded.

The archive's `PUBLIC-HISTORY.bundle`, `RESTORE-GIT.md` and `ARCHIVE-PROVENANCE.json` identify the public export commit and the offline-restored tree. The public export commit, later documentation commits, tested engine commit and Pages commit are separate identities. Archive and public HTTP readback hashes are recorded in the release manifest created after this report; the report does not claim its own containing commit hash.

The prior 2007 public source branch and retained archive queries remain intact. The previous Pages checkpoint 234 page and source archive are preserved under pinned historical names. New 2104 artifacts use an immutable candidate-specific Pages directory.

The [live HTTPS progress page](https://a0bb38e846444140.5x7aqstuceku.opentunnel.xyz) provides two-second telemetry. Its plain link is read-only for messages until the current session key is entered; authenticated posting is not claimed. The human-readable posting expiry is **16:52 UTC on 10 November 2026 — 00:52 on 11 November 2026 (Shanghai time)**.

The existing Surge CLI was unauthenticated and ordinary Fileharbor access returned 403. No account, authentication or network workaround was used. The existing project GitHub Pages destination supplies the public release page. The initial separately saved progress report remains its original version because the supported transfer executor stayed unavailable after the requested retries; current durable engineering and report checkpoints use the authorized project Git/public-report path.

---

Prepared by **GPT-6 Astra Pro**, **built-in agents**, and **RubyWebTerm**. Results refer to the exact frozen scopes and accepted candidate identified above.
