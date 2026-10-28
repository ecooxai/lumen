# Acceptance rubric for task 10290244

This score measures the requested Apple/browser work against the following fixed gates. It is **not** an official full WPT conformance score or a claim of Chrome parity. A missing measurement is unverified and earns no points; it is not a measured failure. The target is 98/100.

Each completed iteration records its start/end, wall time, separately attributed worker effort where available, build/test duration, evidence paths, source commit and candidate binary SHA-256. Sum worker time separately from wall time; do not imply parallel worker time is elapsed time. Historical evidence remains labelled historical until reproduced or its artifact/source identity is verified.

| Category | Gate | Points | Acceptance evidence |
|---|---|---:|---|
| WPT | Original frozen important subset | 10 | Exact upstream revision, frozen selection, raw candidate and Chrome reference reports, reported file count/harness status, observed and union-inventory denominator. Award 10 × candidate PASS / reference union inventory only for a comparable current-artifact run; missing cases remain nonpassing. |
| WPT | Expanded important suite | 20 | Predeclare the additional pinned inventory before observing results, covering DOM, events, URL, encoding, fetch, microtasks and storage where supported by the existing runner. Weight the declared categories equally to prevent one very large file from dominating the score. Keep unsupported/incomplete categories visible and unearned. |
| WPT | Provenance and no-regression confirmation | 10 | Map the tested binary to current source and exact artifact hash; preserve raw reports and failures; rerun the original frozen baseline after each accepted engine change and demonstrate no regression. |
| Apple | Same IPA on Mac | 10 | Execute the identified iOS device IPA on a supported Apple-silicon Mac; record package/executable hashes, install/launch receipts and actual runtime behavior. A separate native Mac or Catalyst build does not satisfy this gate. |
| Apple | Same IPA on iPad | 10 | Execute that identical signed device IPA on an iPad; identify device/runtime and package hash. An iPad simulator build is separately useful but does not satisfy identical device-package evidence. |
| Apple | Same IPA on iPhone | 5 | Execute that identical signed device IPA on an iPhone; identify device/runtime and package hash. |
| Performance | Idle | 5 | Current binary hash; one-second CPU/RAM samples for a 45-second idle window after 15 seconds of settling; at least 10 valid steady intervals; interval CPU p95 ≤3%; no sampling gaps above 1.5 seconds. Keep peaks and sample validity visible. |
| Performance | Ordinary interaction | 5 | At least 20 seconds of representative interaction/typing, with explicit warmup separated; one-second CPU/RAM samples; interval CPU p95 ≤10% for typing, raw peaks and attribution retained. |
| Performance | Controlled memory soak | 5 | Repeat navigation/tab-close work for 2–5 minutes, return to the same baseline page, and compare settled initial/final RSS windows. Report RSS growth and pressure; investigate unexplained sustained growth before accepting. No invented universal RAM limit. |
| Performance | Build/test budgets | 5 | Record each build/test wall time and raw resource samples. Investigate an operation exceeding five minutes and optimize it if abnormal; serialize heavy builds. Prefer matching prebuilt V8 rather than rebuilding unnecessarily. |
| Reproducibility/release | Regression-first improvement | 5 | A meaningful behavioral regression fails before the fix and passes afterward; focused regressions and unchanged upstream tests accompany the change. |
| Reproducibility/release | Source/artifact continuity | 5 | Important changes committed, full task captured, current instructions/handoff maintained, current source/binary hashes and build instructions preserved. |
| Reproducibility/release | Reviewed progress/release delivery | 5 | Reachable live progress with two-second CPU/RAM/disk/latency updates and working message/response path; reviewed source backup and source archive; no credentials or unrelated files published. Static-host telemetry limits remain explicit. |

## Scoring rules

- Only evidence for the current artifact earns current-artifact gates. An older useful report can remain in the history without silently receiving current points.
- All-or-nothing gates earn their listed points only after their acceptance evidence is complete. The two WPT pass-rate gates can earn proportional points under their fixed denominator and category weights.
- Report both earned score and the outstanding gates. Do not round a score below 98 up to the requested target.
- Do not substitute custom unit-test counts for upstream WPT assertions, simulator packages for the same device IPA, a successful build for execution, or a running process for low-resource behavior.
- A UI displayed score comes from the machine-readable scorecard, not hardcoded optimistic copy. A score refresh never changes the underlying WPT report.
- The original six-file subset is retained even if an expanded category is weak. A selection change requires an explicit new inventory/version and comparison, not removal of failures.

Approved by the root coordinator during task 10290244; user target 98/100 and three-hour deadline are unchanged.
