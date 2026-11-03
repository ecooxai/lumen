# Continue Lumen Linux session18 — qualify integrated candidate1804

Read AGENTS.md and agent/devsummary.md first. The full user request is agent/task.md.
RubyWebTerm workspace: /mnt/p3/project/browser/lumen-android/lumen-android.
Branch: gpt-6-astra-pro_builtin-agents_rubywebterm_session18-20261103.
Session start2026-11-03T06:58:59Z; nominal deadline11:58:59Z, optional extra hour only
for an important unresolved bug. Root is CTO; same-model agents own all source,
build/test and operations. Preserve source ownership, locks and exact qualification.

## Current source and next gate

Candidate1803 is preserved at eda38fd5e79daf2cab3070e4890d0809aee953e5;
binary5f8edbe982876fdaa03967b81c4fc609411fecac7b37b71e0824415197333c50.
Its frozen integer-regressions3 is188/188, including all185 required union passes
and all5 full1802 regression recoveries. Large integers12/12, syntax38/38 and
computed z-index28/28 pass. Keyframe31 is28/31 with no prior-pass loss; three
canonical opacity .5 versus0.5 failures stopped the worker before broader gates.
Native218, retained1318 and full1248 did not run on1803.

Integrated1804 source adds independently reviewed charset decoding69aa677,
opacity scalar canonicalization dce2d612 and image-longhand grammar fb6d3bd.
The final opacity literal-percentage-to-number correction is committed as95e8bf3b2c0f201984cf560a3725a6f5bbe39c1a.
Freeze the exact clean documentation HEAD with wpt_baseline. All changes have separate source
commits and review receipts under agent/source-work/session18/.
Keep every original WPT and authored acceptance fixture unchanged. The new opacity
fixture v1 has three explicitly documented wrong percentage assumptions and is
preserved unqualified; v2 is separately versioned and reference-qualified10/10.
Original keyframe31 remains unchanged. Use all frozen feature cohorts plus original
regression gates; a fresh complete1248 replay is mandatory after quick/native/retained.

## Broad baseline and held checkpoint

Last regression-free complete sample remains1705:714/1248 files,22637/26245
assertions,57.21/100 selected-file score. Chromium150 is1062/1248 files,
24541/26064 assertions with its separate denominator. No full official WPT or
overall95 quality claim is established.

Candidate1802 focused MediaList10/10 and47/47, neighbor76/113, native218/218,
retained45/1318 passed, but its complete replay724/1248 and22676/26245 had5
prior-PASS losses. Preserve that raw HOLD even though1803 focused repair recovered
the exact losses. Immutable binaries, WPT, manifests and harness inputs are evidence.
The latest public1802 source/archive remains a development checkpoint with the HOLD
clearly labeled. Public9f9dab43fa707adbb69ebc07f30d8107a5f4d9db; archive10308895bytes,
SHA-256a30b5dd525f623d50440fa16fa4f569177d8849cb4548e77337f73fa8c47b8c4.
Receipts: agent/ops/session18/artifacts1802/ and its promotion-receipt.json.

## Important semantics and limits

Parsed keyframe text ignores!important before duplicate resolution. CSSOM setters
accept recognized important priority; empty removal precedes priority validation,
with Chromium's differing empty+invalid-priority behavior kept explicit. MediaList
readonly indexed Reflect FALSE is normative despite Chromium's TRUE/no-op deviation.
Media query semantic depth128, partial math simplification and native stylesheet.media
rendering invalidation remain limits. Charset covers three real external-byte paths;
missing import/XML-PI/external-CSSOM source loading remains unsupported. Image longhand
work covers grammar/serialization, not new rendering or full computed slice math.
Do not revive failed timing1704 wholesale or perform the previously blocked broad
cascade rewrite; retained AGENTS authorization boundaries remain applicable.

## Operations and final promotion

Ops handoff: agent/ops/session18/handoff.md. While ops is inactive, repo_lead polls
node agent/progress/manage.mjs poll and maintains honest dashboard status. The live
HTTPS dashboard and source archive remain running. Surge lacks authentication and
FileHarbor returned403; no deployment claim to either. Final qualified source must
stay frozen through Android build/install-r (preserve data) and verified desktop:0
restart. Preserve profiles and verify task-owned PID/executable before stopping.
Reserve45–60minutes before the session deadline for final replay, device promotion,
source publication and a short actual-browser showcase.
