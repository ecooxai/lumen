# Lumen Linux non-URL conformance — 2026-10-29

Session start: 2026-10-29 17:25:47 UTC (10:25:47 America/Los_Angeles).
Normal deadline: 20:25:47 UTC. Important-bug repair extension only: 21:25:47 UTC.
User request: [agent/task.md](agent/task.md).

The remote RubyWebterm connector is currently failing before commands execute.
The recovered candidate180 bundle was independently checksum-verified before cloning.
Its export tip is `c84e876c319f798a2097f779c5bdbef405ce1142`; tested engine source is
`ff11e7336c5c73cbbb4d69e1469f7907594e035d`. This is a qualified source reconstruction,
not the entire remote compiled dependency closure. Full-browser validation is pending.

Scores distinguish fixed-suite geometry parity from overall browser quality.
Recovered candidate180 baseline: 1493/1569 matching geometry fields = 95.1562/100;
76 mismatches. Overall browser quality is unverified in the current environment.
URL conformance is deferred. Current-source tests must be compared to Chromium.

| Iteration | Start UTC | End UTC | Minutes | Work and result | Focused score | Agents |
|---|---|---|---:|---|---:|---|
| 1 / candidate181 | 17:25:47 | 17:38:16 | 12.48 | Recovered180 and restored real native dependencies; resolve positioned boxes' own intrinsic padding and min/max against their actual containing block. Fixed1569 comparison:1493→1531PASS,38 gains,0 lost passes,0 missing/added identities,38 retained failures. Separate freshly declared1008-field oracle:570→934PASS,364 gains,0 losses,74 failures. Full-browser pending. | 97.5781/100 frozen geometry;92.6587/100 new geometry; overall unverified | engine_lead, conformance_qa, release_ops; root PM |
| 2 / candidate182 | 17:38:16 | 17:39:55 | 1.65 | Floor absolute shrink border-box at its resolved border+padding after maximum-width clamping. Fixed1569 unchanged1531PASS/38FAIL; separate1008:934→938PASS,4 gains,0 losses,70 failures. Incremental native build3.0s. | 97.5781/100 frozen geometry;93.0556/100 new geometry; overall unverified | engine_lead, conformance_qa, release_ops; root PM |

Total elapsed time is updated with each completed iteration; build-only milestones do
not count as the full browser-quality goal.

Iteration1 evidence is committed under `agent/validation/linux-wpt-20261029-session6/qa/`
with raw exits, retained failures, resource samples, and source seals. Source seal: recorded
2026-10-29T17:31:24.081666Z; the three engine files were unchanged through completion.
Source semantics: CSS Positioned Layout3 separates inset-modified available space
from the original percentage containing block; CSS Sizing3 preserves cyclic child
intrinsic percentage handling. URL parser conformance remains deferred.

The initial181 frozen1569 gate was observed complete at17:35:50UTC; its independent
fresh1008-field follow-up and evidence checkpoint completed by17:38:16UTC.
Latest total elapsed from session start through iteration2:14.13minutes.

Iteration3 / candidate183 provisional source checkpoint (still in progress): began
17:39:55UTC. Frozen geometry remains1531/1569 (97.5781/100); the independent
1008-field gate improves938→940 (93.2540/100),2 gains,0 losses,68 retained failures.
The shared Length value now retains all three affine clamp arms, including scalar
and linear transformations; arbitrary nested comparison expressions and none
bounds remain outside this change. sizeof(Length)20→28, ComputedStyle1368→1640
(+272 bytes,19.88%), GridTrack44→60, StyleMove88→120. The dedicated1152-field
Chromium clamp oracle and actual-browser style-heavy resource comparison remain
pending; this checkpoint is not full-browser acceptance. Exact source seal was
captured2026-10-29T17:43:26.802679Z and verified unchanged before this commit.

A separate table-spacing worktree based on accepted182 is active while183 is
frozen. Its independently declared1170-field Chrome reference preceded native
outcomes; baseline182 matches650/1170 (55.5556/100). No table changes are included
in this183 source checkpoint. Overall browser quality remains unverified.


## Authenticated user steering — 2026-10-29 17:53:41 UTC

Source: authenticated progress inbox, message ID e70cbc25-9d7e-4328-8836-6936d57f42d0. Received by engine_lead at approximately17:56UTC via release_ops/root.

> how you work in local vm you need works in the webterm ruby mcp, how you get src to works in local. and now you can continue work in webterm mcp now, i have restart the mcp server, works at least 3h-4h

Continue in the restored RubyWebterm environment after read-only remote state review and preservation of both source histories. The requested total working window now extends through2026-10-29 21:25:47UTC (four hours from original start).

### Completed local source trials before RubyWebterm restoration

| Iteration | Start UTC | End UTC | Minutes | Work and result | Focused score | Agents |
|---|---|---|---:|---|---|---|
| 3 / candidate183 | 17:39:55 | 17:51:55 | 12.00 | Three-affine-arm clamp;1531/1569 unchanged;938→940/1008 (+2); separate declared1152:1038→1113 (+75),zero losses. Browser memory gate pending; none and arbitrary nested comparisons remain holdouts. | 97.5781/100 frozen;93.2540/100 percentage;96.6146/100 clamp;overall unverified | engine_lead,conformance_qa,release_ops;root PM |
| 4 / candidate184 local trial | 17:51:55 | 17:56:41 | 4.77 | Exact clamp arity and atomic parse result.1531/1569 and940/1008 unchanged;1113→1137/1152 (+24),zero losses. Separate648 applied grammar fields630PASS/18FAIL: unmatched closing token and angle dimension remain. Grammar acceptance held for remote repair. | 97.5781/100 frozen;93.2540/100 percentage;98.6979/100 clamp;97.2222/100 grammar;overall unverified | engine_lead,conformance_qa,release_ops;root PM |

Total wall time from17:25:47 to17:56:41 is30.90minutes; parallel work is not summed.
The descriptive table-spacing prototype97c5428 was prepared independently over182,
not integrated with183/184. Its frozen1170-field oracle improves650→1170 (+520),
zero losses/missing/added identities. Independent merged gates remain pending.

The actual-browser HTTP295 cohort retained all6933 assertion identities and
statuses from180 to182: zero gains and zero losses. The full-case179→180 change
was timing-sensitive harness completion, not a semantic gain. Compared with the
fresh Chrome reference, baseline180 retained5123 of6658 Chrome-passing assertions
and179 fully passing cases versusChrome250. These bounded results do not imply
Chromium-wide parity. URL conformance remains deferred.
