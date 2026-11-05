# Lumen Linux WORK22012

**Reviewed source checkpoint — integrated qualification pending, 5 November 2026**

Created with GPT-6 Astra Pro, built-in agents, and RubyWebTerm.

The exact source commit is `60f6cdbfddd9c6da906c31b4cf684a7974feea54`; its source tree is `8b07962b833658f614a42eec2092697f497fd7e2`. This snapshot integrates five reviewed worker and structured-clone files onto accepted candidate 22010.

Native SharedWorker and dedicated Worker messages now prepare independently owned ArrayBuffer storage, preserve aliases and resizable views, and commit transfers in the original list order. The private worker cloning path also preserves existing rejection of unsupported host objects. Isolated candidate 22011 passed its required frozen controls and an ownership harness using ASan and UBSan. Those results do not qualify the changed integrated candidate: its native, focused, retained, and full selected-cohort gates are still pending.

## Accepted baseline

Candidate 22010, source `4248fd4a8d6b72a09e1f89f2b7f161b022f721af`, is accepted **with one documented reference/spec discrepancy**. Its raw results remain:

| Measure | Result |
| --- | ---: |
| Fully passing selected URLs | 816 / 1,248 |
| Passing assertions | 23,352 / 26,245 |
| New assertion passes relative to 22001 | 24 |
| Explicitly authorized prior raw pass-to-fail discrepancy | 1 |
| Native tests | 221 / 221 |

`strict_no_loss=false`. Every other prior assertion pass and all 813 prior fully passing URLs were retained. `ACCEPTANCE-22010.json` records that qualification for its named source; it does not qualify candidate 22012.

`CONFORMANCE-CONTRACT-22012.json` preserves the exact same single raw discrepancy and additionally requires every accepted 22010 assertion pass, fully passing URL, harness pass, and registration identity. Original HOLD results remain preserved. Worker controls are separate from the 1,248-URL WPT denominator.

Same-isolate local ArrayBuffer transfer remains a known gap. X sign-in through Google remains unresolved. The overall project assessment is 70/100; that score is separate from test pass percentages. Whole-WPT conformance and Chromium parity are not established.

## Recovery and exclusions

`SOURCE-MANIFEST.json` binds 813 portable source inputs. The archive preserves genuine public export Git history and supports offline restoration. It does not include canonical private Git history, profiles, credentials, private captures, inbox contents, or host configuration. External dependencies and browser binaries are not bundled. Offline source restoration is not an independent browser rebuild.
