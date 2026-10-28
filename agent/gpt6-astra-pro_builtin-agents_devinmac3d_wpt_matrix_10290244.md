# Final scoped WPT matrix for task 10290244

**Current native artifact:** SHA256 `1b9d43ed19cd616b0029e34fde97050b86c9acbdbda16a0dca5892922df704e9`, compiled source checkpoint `436916ceb3b32630ed251bb666b02fa39a3cd0c8`.

The preserved six-file regression selection passes **1,850/1,850 assertions**. In the separately frozen priority selection, **207 of 1,515 variants were recorded and 1,308 remain unrun**. After the verified two-row Headers fixture replacement, **160 of 207 recorded variants fully pass**. The denominator remains 1,515; these results do not establish full WPT or Chrome parity.

The priority inventory contains 235 upstream source files at commit `7f8d49b5a82606efc771f43d10aad5bce5ccbfeb`. All generated window, dedicated-worker, shared-worker and service-worker variants remain in the frozen schedule. Categories were selected before their outcomes were observed.

## Current artifact results

| Category | Frozen variants | Recorded | Fully passing | Harness OK / ERROR / TIMEOUT | Subtests PASS / FAIL | Unrun |
|---|---:|---:|---:|---|---|---:|
| DOM | 14 | 14 | 13 | 14 / 0 / 0 | 995 / 1 | 0 |
| Events | 16 | 16 | 16 | 16 / 0 / 0 | 78 / 0 | 0 |
| URL | 77 | 77 | 76 | 77 / 0 / 0 | 12316 / 7 | 0 |
| Encoding | 1339 | 31 | 16 | 24 / 2 / 5 | 7264 / 57 | 1308 |
| Headers — original raw | 52 | 52 | 24 | 26 / 13 / 13 | 499 / 2 | 0 |
| Headers — verified replacement view | 52 | 52 | 26 | 26 / 13 / 13 | 539 / 0 | 0 |
| Microtasks | 10 | 10 | 6 | 6 / 2 / 2 | 15 / 0 | 0 |
| Storage | 7 | 7 | 7 | 7 / 0 / 0 | 1152 / 0 | 0 |

There are no additional recorded subtest TIMEOUT/NOTRUN or harness CRASH statuses in these guarded counters. Harness errors/timeouts include zero-assertion files and are never counted as passing. Raw totals are 158 fully passing files, 22,319 passed assertions and 67 failed assertions; the separate adjusted view is 160 fully passing files, 22,359 passed assertions and 65 failed assertions. Those assertion totals are weighted by each test file's assertion count and are bookkeeping, not an overall browser-quality score.

## Headers fixture correction

The original 52-variant run reported 499 PASS and 2 FAIL, with 24 fully passing files, 13 ERROR and 13 TIMEOUT. Both failures were the `Loading data…` setup in `headers-no-cors.any.html` and its dedicated-worker variant. A sparse-checkout helper, `fetch/api/cors/resources/not-cors-safelisted.json`, was absent, so the official server returned an HTTP 404 JSON error object instead of the expected array.

The helper was restored from the same pinned upstream revision. Its Git bytes, local SHA256, HTTP 200 response bytes and 11-entry JSON-array shape were verified. Exactly those two variants were rerun on the unchanged native artifact: 54/54 assertions passed. The original two rows contained 14 passed assertions and 2 setup failures; replacing those rows gives 539/539 observed assertions across the same 52 scheduled variants, with 26 fully passing files. The 13 ERROR and 13 TIMEOUT remain visible. The repeated 54 assertions must not be added to the raw 501-assertion denominator.

The byte-exact receipt is committed at `agent/evidence/10290244-headers-fixture-supplemental.json`, SHA256 `6bd1483c10336f701501333530520443eca80658f879b4e6066222a2f980a7d6`. It preserves the original ledger hash and each old/new row hash. Original results remain at `output/wpt-10290244/priority-1b9d-v1/headers/lumen/results.jsonl`; the separate replacement ledger is `output/wpt-10290244/priority-1b9d-v1-headers-supplemental-1/adjusted-headers-results.jsonl`.

## Remaining encoding failures and next priorities

| Count | Area | Interpretation and next step |
|---:|---|---|
|23|Legacy Big5/ISO-2022-JP form/query encoding|The fixtures receive UTF-8 where legacy bytes or numeric character references are expected. Trace document/form encoding propagation; existing acceptCharset reflection is present. This is separate from the UTF-8 TextEncoder API.|
|24|Stream interfaces and IDL|Repair TextDecoderStream inheritance, private receiver checks and worker exposure with a shared stream implementation.|
|8|Legacy streaming/reset state|Implement bounded persistent ISO-2022-JP decoder state. Do not remove the explicit unsupported guard or buffer entire streams without a bound.|
|2|MessagePort ArrayBuffer transfer|The fixture transfers a destination buffer, but current channel paths discard buffer transfer lists. Add sender-detachment/recipient-identity RED tests and repair transfer machinery; genuine-detachment codec controls already pass.|

These four groups account for all 57 recorded encoding assertion failures. Encoding coverage remains 31/1,339 variants, with 1,308 unrun; no broader encoding pass claim is supported. The runtime follow-up document contains implementation paths and regression requirements: `agent/gpt6-astra-pro_builtin-agents_devinmac3d_runtime_followup_10290244.md`.

## Denominators and reference limitations

- Report fully passing files against every attempted file, with unrun files adjacent, or against the complete frozen inventory. Never divide only by harness-OK files.
- Keep 60 frozen shared/service-worker variants visible: 26 Headers, 4 microtasks and 30 encoding. Outcomes apply only to cases actually executed; unrun variants remain unrun.
- Do not add the original 1,850-assertion selection to this matrix because the selections overlap. Custom native checks are also separate.
- Installed Chrome failed independent about:blank URL sanity controls. Its historical URL counts remain quarantined; no aggregate Chrome parity or outperformance claim is made.

## Separate native verification

The same artifact passed 1,122/1,122 custom native/window/worker checks, plus the separate existing native encoding target at 89/89. The combined build took 11.104s. These checks are not upstream WPT assertions. The original selected1,850-assertion regression remains 6/6 fully passing files.

Current-artifact CPU/RAM and Apple deployment evidence is adjudicated separately in `agent/evidence/10290244-010-current-native-resources.json` and `agent/scorecard-10290244.json`; this matrix awards no performance, IPA or quality-score credit.

## Reproduction and duration

Run the unchanged priority wrapper with `run --plan output/wpt-10290244/priority-1b9d-v1/cohort.json --category <category> --engine lumen --max-seconds <budget>`. It uses the official fixture, immutable native SHA, fixed 15s/65s per-file timeouts and 1Hz process telemetry. Resume retains every unrun frozen variant. A clean new cohort/output directory is required for a separate execution history.

The serialized final batch completed naturally in 304.226s. This exceeds five minutes because it includes sequential upstream cases and expected unsupported-context timeouts under the unchanged limits; it is not an abnormal build delay. Category timings and termination flags remain in `output/session-10290244/final-priority-batch-1/receipt.json`.

The measured wrappers are `remote_priority_batch_10290244.py`, `remote_headers_supplemental_10290244.py` and `remote_final_resource_batch_10290244.py` under `tools/apple-session-gpt6as/`. The matrix JSON records their exact hashes. The priority batch wrapper has no external SIGTERM handler; its observed run ended naturally. Preserve that measured revision, and verify owned-child cleanup explicitly if a future invocation is externally interrupted.

Generated by GPT-6 Astra Pro, built-in same-model agents and devinmac3d. The adjacent JSON and `agent/evidence/10290244-final-wpt-provenance.json` bind every recorded matrix row to its source results and reproduction files.
