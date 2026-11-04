# Continue Lumen Linux — session19 active

Updated 2026-11-04T10:14:49.047982+00:00. Session started2026-11-04T06:33:55Z and remains active.
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

## Current source and candidate1912

Production HEAD `b9a98b328b6647271e1ff97e11ebd54c2cdc63f3`, source tree
`639d5b6ce603c6b89affd6590be98e43aa321d76`. Candidate1912 adds three separate reviewed
commits to immutable1911:

- `a0061811f7852ee184f6e68bb6eb6a422bde6a30`: CSSStyleDeclaration WebIDL conversion,
  required arguments, private genuine receiver state, indexed properties, live iterator,
  parentRule and computed readonly behavior. Existing color validation is reused.
- `abf3591f756ea2292f59b8b9a5cbc26d18f40612`: all captured declaration operations use
  the immutable canonical public receiver; raw-target dispatch is removed. Computed
  grid/font recursion uses captured internal operations. This fixes an interaction caught
  during independent review before any candidate1912 runtime qualification.
- `b9a98b328b6647271e1ff97e11ebd54c2cdc63f3`: range-only specified calc serialization
  preserves wrappers and typed zero while simplifying supported exact terms and sorting
  percentage/dimension operands. Native grammar/computed behavior is unchanged.

Independent reviews and exact integration intervals are retained in
agent/source-work/session19/css-declaration-api-v1, css-declaration-receiver-v1 and
range-calc-v1. Margin/padding storage expansion, constructor availability, broad cascade
rollback and immunity of every pre-existing parser helper to arbitrary overrides are not
claimed. API state is private and frozen; legitimate same-origin cross-realm borrowing
uses the existing private REALM bridge, with exceptions created by the calling operation's
realm. The separate generic inline importance assembly correction is approved for a later
bounded draft, not included in1912.

Candidate1912 build/startup and frozen API50, API boundary/realm/receiver controls, range
controls and prior-pass/full1248 gates are pending. Hold all tracked source/Git/status
writes until the test owner releases the exact frozen candidate. Diagnostic flags
LUMEN_TRACE_LOAD and LUMEN_PROFILE_PRELUDE must remain unset during qualification.

Candidate1911, exact frozen HEAD `a16d5f4bdda91036fd01c8a99325460285b6143a`, passed
build/startup and native218 (218/218 in201.16s), frozen307 URLs (180/307 and3670/4468,
+3 URLs/+7 assertions, no prior losses) and independent110 (108/110, no old PASS loss).
Binary SHA256 `46b373e10f613ceedab6e58106279a617faa3e38e0616401ae7ed60e30777f0c`.
Its unchanged full1248 run completed10:14:04.54UTC with783/1248 URLs and23110/26245
assertions. Three new URLs and seven assertions passed, but the namespace URL lost its
required harnessOK/fullURL status despite all51 assertions passing. All1248 browser exits,
pins and cleanup were valid; no assertion PASS was lost. The original run is HOLD and must
not be relabeled by later passing runs. Last fully qualified full candidate remains1908.

The isolated1910 descriptor-batching hypothesis was rejected after all six fixed
comparisons: owned CPU15.383061s to15.431443s did not meet5% improvement, and namespace
wall8.583s to8.3747s improved2.426890%, below3%. Only RSS passed. Production64fd6463 was
reverted in051a0d6408ff5c00874fd300fe4d15df964ad2c0. The benchmark receipts and unused
reflection plan remain retained; no extra runs may erase this rejection.

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

Qualified1908 public commit: `9d3882d753faa45fc5c52f9e9748b9f7cdaaf7e4`.
The live exact-source archive is10,424,331 bytes, SHA256
`0f6156e1c9649236e2ec227e22df9539be56f6dc8435a059aa38fc4881a9435e`.
Offline recovery and full external HTTPS readback verified at09:41:18.884971UTC.
Receipt: agent/ops/session19/artifacts1908/public-readback.json, copied into source-work.
The1906 archive remains safely archived. Iteration20 work-only public commit is
`e86534d5831f4d1a66b9fec029f44d2a0c3350cc`; it is not a qualified runtime artifact.
The next mandatory work checkpoint is iteration30 if reached. Environment has prepared
only a lightweight1911 source inventory; no1911 full acceptance or promotion is implied.

The normal five-hour deadline is11:33:55UTC. Root approved up to one additional hour only
if the concrete generic importance bug's validation/delivery needs it; absolute cap12:33:55UTC.
Keep user elapsed wall time separate from summed parallel task durations. Do not invent
unmeasured model compute time. Continue authorized development and preserve all evidence.
After final acceptance, finalize AGENTS/continue/devsummary and agent/next-agent-prompt.md,
publish the exact reviewed source, and restart on display:0 and the authorized connected
MI8 with install-r/data preservation. Source publication, desktop execution and Android
installation have separate identities and receipts. Linux results do not imply Android
conformance. Root/environment own final delivery.
