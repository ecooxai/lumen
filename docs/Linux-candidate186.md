# Lumen Linux — candidate 186

Prepared with GPT-6 Astra, built-in agents and RubyWebterm on 2026-10-29.

## Exact source and scope

This export copies the audited production source, build inputs and test sources from private working commit `3731a114676dc87ffe7bce27bff78235431bd6f5`. Its Git parent is already-public origin/main commit `73fdea3e4e7fe09dd87d1b849f85c634f320f7c5`; private working history is not copied. SOURCE-MANIFEST.json maps every copied file to its original Git blob and SHA256. Additive release metadata is identified separately.

The original Ruby candidate 186 executable has SHA256 `124ebb8663e0293d4820de406c0e9d5f976477c2f4d52032526ea0b652232744`. This source export has not yet been rebuilt in its separate export directory. It is not a portable binary distribution, an Android release, or certification that the full Web Platform Tests pass.

## Verified progress

- Independent authored form fixture: 8,127/8,127 checks, gaining 1,099 from candidate 182 with no prior-PASS losses. Chromium's separately retained reference result is 8,125/8,127; its two reversed-time message nonpasses were not hidden.
- Pinned upstream forms: 644/649 assertions and 19/24 complete files, versus baseline candidate 183 at 522/649 and 9/24. The 122 assertion gains have zero prior-PASS losses. Installed Chromium passed 649/649 and all 24 files. Five Lumen assertions remain.
- The preceding accepted table-spacing candidate 185 passed 960/960 authored fields, gaining 456. Candidate 184 passed 109/111 clamp, 30/30 comment and 26/27 string controls. These are separate test scopes and are not additive WPT coverage.
- The approved fixed CPU performance protocol completed 21/21 balanced trials (seven per candidate 176, candidate 186 and Chromium). Each trial passed all 9,395 workload checks. All 14 native children exited zero; all fresh profiles were removed and no owned processes remained. Exact medians, ranges and sampling limits are in performance186-summary.json.

The current whole-browser readiness assessment is pending. A previous numerical estimate had no reproducible formula and is not used as the current score. URL conformance remains deferred as requested. Candidate 187/188 work is deliberately not included in this 186 snapshot.

## Source-set count erratum

The exact candidate 186 recorded source set contains **624 entries**, including tracked fixtures; all 624 are present with identical SHA256 values in this 692-file source export. Earlier narrative references to 618 entries for candidate 186 were stale. The original receipts and equality checks remain unchanged. The corrected counts are candidate 176: 617; candidates 182/183: 618; candidate 184: 622; candidates 185/186: 624; and candidates 187/188: 627. These are source-set entries, not a count of compiled translation units.

## Build requirements and reproducibility limits

The measured Ruby build used Clang, CMake Release mode, SDL3, V8 15.6.75.8 with pointer compression, wgpu-native, libdatachannel, OpenSSL, ICU, FreeType, HarfBuzz, Fontconfig, libcurl, zlib and related platform libraries. Exact recorded dependency settings are in build-dependencies186.json. External dependency libraries and caches are not distributed in this source archive.

An ordinary source build follows the existing CMake contract, with paths selected for the operator's environment:

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

The project-specific shared CPU wrapper remains available in tools/codex_compile.py for the documented Ruby environment. A different host must provision compatible dependencies; this archive does not promise hermetic or bit-identical builds. Test fonts such as WPT Ahem and the zero-width ch fixture are external test inputs when those gates are used.

Automated browser validation must use the operator's approved isolation and launch configuration. Private capture-guard/authentication implementations, audit capabilities, profiles and private workload infrastructure are excluded. Some retained browser test adapters therefore require separately installed operator infrastructure. Native source and fixture presence does not imply every test has been rerun from this export.

## Measurement interpretation

The fixed comparison used a 748 × 596 CSS-pixel viewport at DPR 1 in explicit CPU modes. Chromium's real ELF was observed while alive and separately queried through SystemInfo: compositing and rasterization were software, while Vulkan, WebGL and WebGPU were disabled. Intel UHD 620 Vulkan adapter enumeration succeeded in a separate probe, but actual Lumen hardware rendering remains unverified.

Timing ranges overlap between Lumen 176 and 186; no general speedup or stable regression is claimed. Chromium was faster in the measured DOM/style/grid phases. Resource samples are one-second browser-process-tree measurements across startup, work and cleanup. Summed RSS can count shared mappings repeatedly; PSS is sampled. They are not a steady-idle, whole-host, or general browsing memory claim.

## Privacy and history

This export excludes the task text, development diary, agent handoffs, private validation outputs, guard/auth files, browser profiles, inbox/capability state, build artifacts and dependency caches. Historical documentation outside this release note is not republished as current evidence. The full working checkout and its private checkpoint remain separate.
