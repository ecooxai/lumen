# Lumen Linux — candidate 196

Prepared with GPT-6 Astra, built-in agents and RubyWebterm on 2026-10-29.

## Exact source and recovery

This checkpoint copies 706 audited production, build and test files from original working commit `2b301e545be9055fbc933fdd187fce099e6fd148`. All 638 entries in its recorded source set, including tracked fixtures, match exactly; the source-set digest is `5993efeb0dee61f2ec121fda9dd953448a36ad610053cd305fa325eafad2daf0`. SOURCE-MANIFEST.json maps every copied file to the original Git blob, SHA256, byte count and mode. The six additive public release files are listed separately in EXPORT-QUALIFICATION.json.

The export continues the reviewed candidate 186 public-source history at `f82796fd8dbba1a691521c78c74c8d085e059cc1`, rooted in already-public origin commit `73fdea3e4e7fe09dd87d1b849f85c634f320f7c5`. It does not include the original private working history. The packaged source tar contains a self-contained PUBLIC-HISTORY.bundle and RESTORE-GIT.md. Packaging verifies a fresh offline clone against the exact exported commit, tree, blob identities and executable modes; see ARCHIVE-PROVENANCE.json in the tar for that final mapping.

The actual tested Ruby executable remains separate. Its SHA256 is `57a96003f1e7c789d06db637812bd043472963a10ac7d8a066ec3d323fc952d9`. Build and startup checks succeeded on the recorded Ruby environment. The separate export directory has not been rebuilt. This is a source checkpoint, not a portable Linux binary, Android release or all-WPT certification.

## Verified conformance scopes

- Authored forms retain 8,127/8,127 checks. The pinned 24-file upstream forms cohort reached 646/649 assertions and 21/24 complete files on accepted candidate 190, versus 522/649 and 9/24 on candidate 183. Chromium passed 649/649. Two dynamic validity-selector checks and maxlength editing remain outside this checkpoint. Two Chromium validation-message nonpasses in the authored reference remain separately recorded.
- Table spacing passed 960/960 authored fields on candidate 185. CSSOM scanner 188 passed 196/200 fields, with four boundary-comment serialization holdouts; the original string controls reached 27/27.
- Candidate 195 preserves all 3,268 canonical CSSOM observations byte for byte. There are 2,447 reference matches and 821 preexisting reference differences; this optimization creates no new conformance claim for those differences. The 200 scanner observations and 27 string checks are unchanged.
- The fixed 22-file network cohort reached 302/317 assertions on the accepted cache repair. Candidate 193's unsolicited-304 header regression was retained as a failed provisional result. The separate candidate 194 repair passed all 140 native tests and 258/258 zstd controls before the CSSOM optimization.

- Candidate 196 angle-unit repair improves the fixed five-file upstream sample from 31 → 89/184 assertions (+58, zero losses), and the separate authored controls from 401 → 473/832 (+72, zero losses). Prior CSSOM controls remain unchanged. The remaining failures are retained.

These scopes overlap and are not summed into WPT coverage or browser readiness.

## Matched broad Chromium comparison

On the same frozen 640 non-URL HTTP document variants, immutable candidate 176 → 187 improved from 357 → 368 fully passing cases and from 8,073 → 8,206 of 10,540 reported assertions. There are 133 gains (8 CSS-value and 125 form assertions), zero lost or changed identities, and no changed harness statuses. Chromium passed 544/640 complete cases and 9,516/10,549 reported assertions; reference failures and timeouts remain included. The differing reported assertion totals are not silently treated as equal coverage.

The lifecycle audit is complete: all owned processes are gone, all Chromium profiles were removed, and two inactive native profiles were removed after the original 638/640 exit-receipt result exposed a harness teardown defect. The original receipt remains historical evidence. The 176 baseline later completed 640/640 lifecycle checks. The broad comparison is for immutable 176 and 187, not a fresh whole-browser score for 196. URL conformance remains deferred; whole-browser parity is not certified.

## Performance: bounded findings

These measurements are for immutable candidates 194 and 195, before the candidate 196 angle change. They are not presented as a timing result for 196; a final-candidate correctness/resource check remains planned.

The unchanged CPU DOM/layout/grid protocol ran 21 measured fresh-profile trials: seven each on candidate 194, candidate 195 and Chromium 150.0.7871.114, in balanced order. All trials passed all 9,395 workload checks. No separate warmup trials were launched or discarded; the public protocol metadata does not declare an internal warmup count. The actual Chromium ELF hash is `2d4adea820c95eee0c24dcc5eb729043df570838b8693fa3449df15040b0460f`. Both native binaries, the Chromium launcher/ELF and protocol sources were unchanged throughout.

| Fixed phase | Candidate 194 median [range], ms | Candidate 195 median [range], ms | Chromium median [range], ms |
| --- | --- | --- | --- |
| DOM create/remove | 50.74 [38.80–64.59] | 49.60 [37.22–57.69] | 11.80 [11.20–17.30] |
| Style/layout | 419.05 [368.42–461.92] | 408.52 [363.81–443.86] | 62.30 [57.00–76.70] |
| Grid rebuild | 187.88 [151.04–215.17] | 178.07 [140.04–197.60] | 50.50 [46.60–76.30] |

The full-workload effect is inconclusive: the native ranges overlap, and the style phase improved in only five of seven balanced triplets. Chromium remains faster in these measured phases. No broad speedup is claimed.

One separate, already frozen phase diagnostic ran 14 measured fresh-profile trials, seven alternating-order pairs, retaining all 12,289 checks per trial and every outcome. Its canonical CSSOM setter mutation median fell from 58.54 ms [51.54–61.01] to 35.46 ms [31.33–48.00], about 39.4%; all seven pairs improved (15.43–44.29%). The following direct-attribute mutation stage became slower: 14.60 → 20.16 ms, in all seven pairs. These stages share a process and fixed order, so allocation/JIT work may move between phases; that cause is unproven. The benefit is specifically the canonical setter path. The diagnostic is included for reproducibility and does not replace the fixed acceptance workload.

All 14 native children in the fixed comparison and all 14 diagnostic children have explicit public exit receipts with exit code zero and profile removal. No owned workers remained. Resource sampling was every second over browser process trees during startup, work and cleanup. Median trial-sample RSS/PSS was 164.514/139.286 MB on 194, 164.455/139.193 MB on 195 and 932.864/381.865 MB on Chromium. Summed RSS repeats shared mappings; PSS is sampled. These are not steady-idle, whole-host or general browsing memory claims. Raw ranges and paired effects are preserved in performance195-summary.json.

The fixed viewport was 748 × 596 CSS pixels, DPR 1, with explicit CPU/offscreen rendering. Chromium's actual ELF was previously observed alive with software compositing/rasterization and disabled Vulkan/WebGL/WebGPU. Separate Intel UHD 620 Vulkan enumeration succeeded, but actual Lumen hardware rendering remains unverified.

## Build instructions and limitations

The recorded build family uses Clang, CMake Release, SDL3, V8 15.6.75.8 with pointer compression, wgpu-native, libdatachannel, OpenSSL, ICU, FreeType, HarfBuzz, Fontconfig, libcurl, zlib and related platform libraries. Public recorded settings are in build-dependencies196.json. No new external dependency was introduced by these changes; dependency libraries and caches are not redistributed.

```sh
cmake -S . -B build/review \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DLUMEN_BUILD_BROWSER=ON \
  -DLUMEN_BUILD_TESTS=ON \
  -DLUMEN_V8_POINTER_COMPRESSION=ON \
  -DLUMEN_DEPS_DIR="$V8_PREFIX" \
  -DLUMEN_WGPU_DIR="$WGPU_PREFIX"
cmake --build build/review --parallel 2
```

Provision compatible platform dependencies before configuring. The documented Ruby environment uses its shared CPU wrapper in tools/codex_compile.py. This source archive does not promise hermetic or bit-identical builds. Some test adapters require separately installed operator validation infrastructure and external test fonts. Their presence in source does not imply a fresh run from this export.

Private task text, development diaries, guard/auth implementations, runtime capabilities, inbox state, browser profiles, raw private validation output and dependency caches are excluded. The tested Ruby executable and private working checkout remain separately available to their owner. The active session continues beyond this checkpoint; later candidates and final broad validation are not represented as completed here.
