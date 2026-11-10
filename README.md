# Lumen Linux: accepted source checkpoint 2104

This public branch contains the exact tested engine source for the two-axis border-radius repair, curated qualification reports and preserved public history.

## Measured result

- Selected unprefixed parsing/computed WPT target: **48/48** across 3 files (previous 21/48).
- Separate prefixed companion: **25/25** (previous 20/25).
- Expanded frozen cohort: **819/1248 complete files;23364/26245 passing assertions (89.02267%)**.
- Native tests: **222/222**, retaining all 218 previous names.
- Actual same-window Y-only redraw: 10 patch checks PASS, 1,127 changed pixels inside the shape, 0 outside.

The scoped target passes; this is not all WPT, all-radius animation, Chromium-wide parity or overall 98% conformance. Known mixed-arity animation remains a retained limitation. The original Chromium run and its separate startup diagnostic are reported without silently merging them.

## Inspect and recover

Read the [qualification report](docs/lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_session21_qualification_20261110.md), [machine-readable report](docs/lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_session21_qualification_20261110.json) and [sanitized 1,248-row comparison](docs/lumen_linux_gpt-6-astra-pro_builtin-agents_rubywebterm_session21_wpt1248_comparison_20261110.json). `SOURCE-MANIFEST.json` maps 748 recorded build inputs and the remaining approved source paths to exact blobs/modes. `EXPORT-QUALIFICATION.json` binds the accepted source, binary and independent seals. `OFFLINE-RECOVERY.md` explains the public-history bundle.

The source tree contains 815 engine/build/doc paths, 28 preserved historical public files, 5 metadata files and 3 reports: 851 tracked paths. It imports no canonical private Git history, browser profiles, inbox, posting capabilities or unreviewed runtime artifacts.

## Build and run

Consult [CMakeLists.txt](CMakeLists.txt) for the current native build configuration. The preserved `docs/build-dependencies200.json` is an older dependency snapshot, not a new build result. External dependencies are not bundled and a new independent archive rebuild is not claimed. On the original project host, the accepted executable is `build/codex-native/lumen-astra-i2104`; the normal build output may advance during later work. The readable report supplies a new isolated-profile launch command and the exact ICU data pin. No existing desktop browser/profile was replaced.

## Identity

Tested engine source: `3edec1c75b9e332f16e2e5b0943eb881c3c00e1f`.

Accepted binary SHA256: `0e7f3d63259731afedafe2fd717394b49e4fe525d34d9c12b1c437dd5e6789d9`.

This branch is a new descendant of public checkpoint 2007 commit`e7569b9a15d4f323c5edb66ee7a79f9f0e85473a`. The public export commit, source archive and Pages commit are separate publication identities recorded outside this report-containing commit.

[Public release page](https://ecooxai.github.io/lumen/) · [Live progress](https://a0bb38e846444140.5x7aqstuceku.opentunnel.xyz)

Prepared by GPT-6 Astra Pro / built-in agents / RubyWebTerm.
