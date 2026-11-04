# Continue Lumen Linux — session19 active

Updated 2026-11-04T09:40:33.574060+00:00. Session started2026-11-04T06:33:55Z and remains active.
Work through RubyWebTerm in `/mnt/p3/project/browser/lumen-android/lumen-android`.
Branch: `gpt-6-astra-pro_builtin-agents_rubywebterm_session19-20261104`.
Read AGENTS.md, the complete user request in agent/task.md, and the latest entries in
agent/devsummary.md. The previous session handoff remains archived at
agent/archive/continue-session18-20261104T063800Z.md; intermediate handoffs are archived too.

## Roles, freezes and resource policy

Root is CTO. source_steward owns canonical source/Git/index and continuity records;
test_baseline owns independent builds/tests and immutable candidate reservations;
environment_delivery owns preview, monitors, public exports, runtime/device delivery and
agent/progress/status.json. Coordinate explicit tracked source/Git/status freezes before
builds; native218 also requires the exact tracked-clean HEAD throughout its run. After a
build seal, immutable tests may continue while new source is edited. Never reuse consumed
candidate IDs or alter trusted guards, WPT assertions, expected outcomes, timeouts or workers.
Preserve historical evidence and all HOLDs. Public exports use reviewed approved ancestry,
exact source blobs and narrow allowlists, never private canonical history or runtime data.

Use project-backed temporary files because /tmp is nearly full. One heavy task at a time,
under the existing shared480% CPU quota, with one-second CPU/RSS monitoring. Preserve the
required owned WPT server when reconciling cgroup tasks; inspect identity before any cleanup.
Test/build task timing is separate from source mutation time and model compute time.
The five-hour budget runs to2026-11-04T11:33:55Z; an extra hour is only for important fixes.

## Latest fully qualified result — candidate1908

Exact source HEAD: `d995e1a5eb8daebd07ec7b67f0572f40b70fc6e6`.
Source tree: `487bed118fb7920b8872bd038b575364165f61ad`.
Binary SHA256: `cf2692999572a09524c11697d76de073ccbba7e3c6fe496b89bb241ccfc9bdc9`.
Build/startup117.23s; native218 passed218/218 in200.22s at the exact clean HEAD.
Full unchanged1248 replay completed09:18:50UTC in1040.14s: **781/1248 URLs**
(62.580128/100 selected-manifest compatibility), **23103/26245 assertions**.
This is +9 URLs/+79 assertions over1906 and +22/+277 over1808. Every previously passing
URL, assertion and harness result was retained; all1248 processes exited0, source/binary
pins and cleanup passed. Full acceptance SHA256:
`c6c1341edd3feb1171651cebf943bbd65fbac91438f203aa6f94366a31d7c058`.
Raw receipt SHA256: `f72ea86207eaa207802d86d9ad17d821687a860dbd150dcc1fb77fcf9075bb28`.
Evidence lives under agent/validation/linux-wpt-20261104-session19 and
agent/source-work/session19/test-baseline.

The frozen266-URL union passed154/266 and3525/4256. Independent DOM26 and inert9 passed
26/26 and9/9. Range24 passed23/24, with parsed declaration importance still failing;
two separate root-pseudo rem controls also failed. These known cases were retained for
next-candidate fixes. The manifest is1248 selected HTTP/default-timeout DOM/CSS testharness
URLs from1247 files, not all WPT. Historical pinned Chromium150.0.7871.114 scored1062/1248;
this is not a current latest-Chromium run or an overall browser quality score.

## Current source and next candidate

Current functional production HEAD before the next docs freeze:
`051a0d6408ff5c00874fd300fe4d15df964ad2c0`, source tree
`680ebea75737d135b4d1fe0416a26405589afe6d`.
The three separate, statically reviewed follow-ups are not runtime qualified yet:

- `2abd7f95c45b12a3bc70bd1e9bd70583798aecb2`: range-only native priority replay,
  using existing priority tiers and validated all projections. Explicit CSSOM API
  replacement remains unchanged.
- `070bf79632a3391960eca1e2fe5553edd0323174`: root pseudo range rem uses the
  originating root font; ordinary root/non-root contexts stay unchanged.
- `6ee883838a8dba9bad203428c62c8a0bec5f6f21`: private live branded StyleSheetList
  collections, item/identity/liveness/index semantics and genuine same-origin cross-realm
  borrowing. Generic02-node.js helpers are unchanged.

Exact integration hashes, reviews and source mutation intervals are in
agent/source-work/session19/range-and-list-integration-ledger.json and the associated
range-priority-v1, range-root-pseudo-v1 and stylesheet-list-v1 directories.

Candidate1911 is now being sealed from this production tree plus continuity docs. Keep all
tracked source/Git/status frozen through build and native218 until the test owner releases
that exact HEAD. Required frozen range priority, root-pseudo, StyleSheetList and prior-pass
neighbors precede the unchanged full1248 replay. The separate specified-calc draft is excluded.

The isolated1910 descriptor-batching hypothesis was rejected after all six fixed comparisons:
owned CPU15.383061s to15.431443s did not meet5% improvement, and wall8.583s to8.3747s
improved2.426890%, below3%. Only the RSS criterion passed. Production64fd6463 was reverted
in051a0d6408ff5c00874fd300fe4d15df964ad2c0; exact source comparison proves the08-window
change relative to1909 is solely the StyleSheetList export. All benchmark receipts and the
unused reflection plan remain retained; no extra runs may be used to erase the rejection.
The default-off sampler remains unchanged and explicitly unset during qualification.

## Reliability and profiler evidence

Original1903 namespace harness TIMEOUT after51 PASS assertions remains HOLD. Similar
historical1808 behavior and fixed diagnostics are retained; no timeout change or claimed
lifecycle fix was made. Successful phase traces show prompt final parent-load completion,
with about95–96% of child initialization CPU/wall in prelude execution. Cached script
binding takes only about5ms across80 children. No mutable realm objects or globals are shared.
Separate default-off Linux diagnostics LUMEN_TRACE_LOAD and LUMEN_PROFILE_PRELUDE remain
explicitly unset for qualification/performance OFF scenarios. Sampler ON measurements
identify shape230/1869 child ticks (~12.3%) and WebGL install192 (~10.3%); printed top20
counts are lower bounds. The isolated batching hypothesis was measured, rejected, and reverted.

## Public persistence and remaining delivery

Qualified1906 public commit: `0d76bf0aeefb3c975b94b550f831681c41e3beda`.
Current live source archive remains1906:10,395,999 bytes, SHA256
`ad13919b9b5963c8ecf440f0d3b6d173f8139459073155d7cb9924e69930962c`,
verified by offline recovery and full HTTPS readback. Qualified1908 passed independent source/ancestry review and environment is publishing it
separately; defer only tracked status updates while1911 is frozen. Exact publication/archive
readback is still required before changing the live archive label.
Iteration20 work-only public commit: `e86534d5831f4d1a66b9fec029f44d2a0c3350cc`;
exact31dbb source, marked unqualified at preparation, reviewed and remote-readback verified.
The accepted live dashboard is1908; the archive label remains1906 until replacement verifies.

Continue the authorized development session. After the final candidate qualifies, update
AGENTS/continue/devsummary, preserve original evidence, publish the exact reviewed source,
and restart the browser on display:0 and the authorized connected MI8 using the established
install-r/data-preserving workflow. Do not present public source publication as device
installation or infer Android conformance from Linux tests. Root/environment own delivery.
