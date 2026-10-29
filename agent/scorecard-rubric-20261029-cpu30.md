# Lume Apple task20261029-cpu30 acceptance rubric

This100-point task checklist is separate from any WPT pass percentage. The requested test platforms are Mac and iPad simulator. Historical task rubric/receipts remain unchanged in the inherited scorecard. Unknown gates earn0; a changed executable requires renewed evidence.

| Gate | Points | Required evidence |
|---|---:|---|
| Original important WPT subset |10|Unchanged pinned upstream assertions executed on the exact candidate; case and assertion denominators retained|
| Expanded important WPT suite |20|Frozen1515-variant schedule retained and valid comparative completion; no dropping failures, unsupported contexts or unrun cases|
| WPT regression retention |10|Valid same-artifact unchanged regressions and qualified comparator evidence where claimed|
| Shared signed iOS device IPA on Mac |15|Actual immutable signed device package/app identity plus observed compatible Mac execution; native Mac/Catalyst builds are different artifacts|
| iPad simulator execution |10|An explicitly identified iPad simulator runs the app from the same reviewed source, with actual runtime checks; the simulator app is not the device IPA|
| Idle performance |5|One-second telemetry on the accepted executable; ordinary settled idle near0–3%CPU|
| Ordinary interaction |5|Same-artifact controlled scrolling below30%CPU under the user's revised limit; report p95 and peak, and separately retain typing's10%limit|
| Memory soak |5|Same-artifact controlled navigation workload and settled RSS/native-object trends with explicit limits|
| Build/test budgets |5|Stage wall times and one-second sampling; explain abnormal stages over five minutes; reuse compatible prebuiltV8|
| Regression-first engineering |5|Meaningful preserved RED-to-GREEN evidence with unchanged assertions|
| Source/artifact continuity |5|Committed reviewed source, exact binary/source/dependency hashes and reproducible ownership|
| Progress/release delivery |5|Verified public progress/source bytes, live metric cadence and message path with private credentials excluded|

Apple points total25 as before; the historical iPhone-only5-point gate is removed from current acceptance. The Mac device-IPA gate carries15 and iPad simulator gate10. This scope correction does not award points. No physical iPhone test is required by this task. A simulator launch must never be presented as proof that a signed device IPA executes there.

The user revised ordinary scrolling acceptance from the previous10%ceiling to below30%. Original failed receipts remain unchanged. New task receipts must state the current policy, raw p95/peak, sample coverage and exact executable. Passing this task checklist still must not be called complete official WPT conformance.
