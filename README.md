# Lumen Linux: accepted source checkpoint 2105

This public branch contains the exact tested engine source for the two-axis border-radius repair and bounded MutationObserver callback-exception realm repair, curated qualification reports and preserved public history.

## Measured result

- Selected unprefixed parsing/computed WPT target: **48/48** across 3 files (previous 21/48).
- Separate prefixed companion: **25/25** (previous 20/25).
- Expanded frozen cohort: **820/1248 complete entries;23365/26245 passing assertions (89.02648%)**.
- Native tests: **222/222**, retaining all 218 previous names.
- Actual same-window Y-only redraw: 10 patch checks PASS, 1,127 changed pixels inside the shape, 0 outside.

The scoped target passes; this is not all WPT, all-radius animation, Chromium-wide parity or overall 98% conformance. Known mixed-arity animation remains a retained limitation. The original Chromium run and its separate startup diagnostic are reported without silently merging them.

The 16 selected MutationObserver assertion files now pass158/163; five upstream failures remain. Authored callback controls pass12/13 with the foreign-constructor/no-callback normative failure retained. The separate nested crash guard was not executed. The expanded sample has1,248entries/URLs backed by1,247unique source files.

## Inspect and recover

Read the [qualification report](docs/lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_session21_qualification_20261110.md), [machine-readable report](docs/lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_session21_qualification_20261110.json) and [sanitized 1,248-row comparison](docs/lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_session21_wpt1248_comparison_20261110.json). `SOURCE-MANIFEST.json` maps 748 recorded build inputs and the remaining approved source paths to exact blobs/modes. `EXPORT-QUALIFICATION.json` binds the accepted source, binary and independent seals. `OFFLINE-RECOVERY.md` explains the public-history bundle.

The source tree contains 815 engine/build/doc paths, 28 preserved historical public files, 5 metadata files, 3 reports and one current build/run guide: 852 tracked paths. It imports no canonical private Git history, browser profiles, inbox, posting capabilities or unreviewed runtime artifacts.

## Build and run

Consult the [current host-specific build/run guide](docs/lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_session21_build_and_run_20261110.md) and [CMakeLists.txt](CMakeLists.txt). The guide is curated documentation outside the815 tested source paths. The preserved `docs/build-dependencies200.json` is an older dependency snapshot, not a new build result. External dependencies are not bundled and a new independent archive rebuild is not claimed. On the original project host, the accepted executable is `build/codex-native/lumen-astra-i2105`; the normal build output may advance during later work. The readable report supplies a new isolated-profile launch command and the exact ICU data pin. No existing desktop browser/profile was replaced.

## Identity

Tested engine source: `20a7dd4b54f1a4e587168f10875e43f307a76fb8`.

Accepted binary SHA256: `2d38085828206f384aeafc3afc38c63009fda483c55f0fcd057e0df9255ffcb3`.

This branch is a new descendant of public checkpoint 2104 commit `54999c2cfe6b159167b89c1b5b98427e9eaf5370`. The public export commit, source archive and Pages commit are separate publication identities recorded outside this report-containing commit.

[Public release page](https://ecooxai.github.io/lumen/) · [Live progress](https://a0bb38e846444140.5x7aqstuceku.opentunnel.xyz)

Prepared by GPT-6 Astra Pro / built-in agents / RubyWebTerm.
