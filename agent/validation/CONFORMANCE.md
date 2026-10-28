# Conformance measurement tools

These helpers retain observed outcomes; they do not establish complete WPT compliance. The October 2026 session pins WPT revision `0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb` and Chromium 150.0.7871.114. Its automated inventory contains 71,398 URLs across six categories, including 39,394 testharness URLs. Test262, manual, and visual tests are separate populations.

## Exact identities and provenance

`paired_wpt.py` runs a frozen native executable or Chromium against an explicit URL list with fresh profiles and one-second process-tree monitoring. It now retains complete assertion names. Diagnostic messages remain bounded. Earlier reports produced with the legacy 900-character name limit are preserved; affected comparisons require a separately recorded rerun. The original 241-URL batch comparison used a different reporter and was unaffected.

`compare_wpt_runs.py --urls URLS.json --baseline BASE/results.jsonl --candidate NEW/results.jsonl --chromium CHROME/results.jsonl --output comparison.json` compares URL, full assertion name, and duplicate occurrence. Missing assertions, harness changes, and lost passing assertions remain explicit. A missing observation is never a passing assertion. Raw names remain authoritative; any fixture-port normalization needs a separate, one-to-one provenance audit and must not alter outcomes.

`build_candidate.py` records source manifests before and after compilation, including the embedded data and generator inputs, and hashes the frozen binary. It propagates compiler and startup failures and fails when the recorded source changes during the build. Coordinate a whole-source freeze with all active agents before invoking it. A successful native project test suite is an adjacent regression gate, not a WPT denominator.

## Execution constraints

Use the project resource wrapper and one-second monitoring. Keep temporary profiles and caches on project storage, using a short TMPDIR alias when Unix socket path limits apply. Disable child core dumps. Record scheduling, concurrency, ports, binary hashes, WPT revision, reporter hashes, deadlines, and every attempt. Preserve partial results at a cutoff. Do not change timeouts or expected outcomes to improve scores.

The native runner's ERROR, TIMEOUT, and process-exit categories are not automatically equivalent to Chromium's CRASH category. Explicit native pre-page instrumentation performs reporting and automation and can affect page timing and lifetime. It is not part of the production engine; record its digest and semantic scope. Frozen browser preludes are compiled into the executable, whereas some separate Node unit tests intentionally read working-tree fragments.

## Review boundary

The October 2026 connector review denied full-source exports. These measurement changes were reviewed through bounded semantic summaries, exact small diffs, and retained execution evidence. Denied synthetic test payloads remain unexecuted and are not included in this tooling commit. Large reports, profiles, one-off experiments, and old invalid attempts remain in ignored session evidence.
