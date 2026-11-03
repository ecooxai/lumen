# Continue Lumen Linux session18 — repair full-replay regressions first

Read AGENTS.md and agent/devsummary.md first. The full user request is agent/task.md.
Use RubyWebTerm workspace /mnt/p3/project/browser/lumen-android/lumen-android;
branch gpt-6-astra-pro_builtin-agents_rubywebterm_session18-20261103.
Session start2026-11-03T06:58:59Z, nominal deadline11:58:59Z, one extra hour only for
an important unresolved bug. Root is CTO; same-model agents own all implementation,
build/testing and operations. Preserve exact task scope, source ownership and shared locks.

Focused candidate1802 is now held after its full replay; source is1802 at c60398214ad22f5d7578ec8f1f5239e2bab53010,
binary e54c56023927627937ea6bcd2e9f4e1bb835709e8d7eb90c6d8522e27577b4db.
MediaList WPT10 is10/10 and47/47 (+8 files,+24 assertions); adjacent CSSOM13 gains2
assertions to76/113. Numeric3 retains132/199. Native218/218 and unchanged retained
45 fixtures/1318 assertions pass, independently audited. Original MediaList15 and
owner8 all pass. Keep Chromium's readonly Reflect return-value deviation explicit.
Keyframe47/49 and numeric24/28 retain their known failures, not blanket success.

Full1802 replay completed:724/1248 whole files,
22676/26245 assertions, but5 prior-PASS losses.
Four are signedzero clamp/mod semantics; one25-digit z-index literal becomesauto
when inline specNormalize rewrites it into scientific notation. The tester is holding
1803 build. Repair these before any broader promotion, preserving the union of1705
and1802 passes. Exact frozen gate is integer-regressions3 in the session18 validation
directory; minimum185/188 assertions recovers all5 losses and prior gains.

Last regression-free complete sample is1705:714/1248 files,22637/26245 assertions,
score57.21/100. No full official WPT or overall95 quality claim. Immutable1802
binary, original WPT, manifests and harness inputs remain untouched as evidence.
Source freeze was released for narrow reviewed corrective edits; coordinate the next
exact build snapshot with wpt_baseline. Background v2 remains unapplied while this
blocker is fixed; charset_agent drafts ingress changes independently.

Current candidate1803 source is applied in separate commits: z-index validation2f31207,
keyframe context2d9e7e1, and cssText literal z-index validation follow-up8801d9d.
Independent review accepted the bounded changes; the new build/gates are pending.
Draft paths: agent/ops/session18/z-index-declarations/ and
agent/source-work/session18/keyframe-declarations-v1/. Preserve prior normal duplicate
values when rejecting!important. Then investigate bounded background-clip/border-image
repeat/slice validation and serialization with frozen official cases and independent gates.
No broad parser/cascade rewrite or failed timing1704 revival.

Operations has pushed curated1802 source, public9f9dab43fa707adbb69ebc07f30d8107a5f4d9db;
archive offline restore and public HTTPS byte/hash readback are verified in
agent/ops/session18/artifacts1802/promotion-receipt.json. The archive contains exact
1802 source, not new1803 changes, and is now explicitly held as a development artifact after5 broad-regression losses.

Candidate1803 also includes CSSRule z-index setter fix8b8ddc8 and keyframe CSSOM
priority correctiona4e38c1. Important priority supplied through setProperty is valid;
text-parsed!important is ignored before duplicate resolution. Preserve the documented
Chromium empty+invalid-priority discrepancy and normative empty removal.

Keep final candidate source frozen through Android install-r (no data clear) and
verified desktop:0 restart. Preserve all untracked work, raw failures, monitors and
exact source/binary identities. Ops handoff is agent/ops/session18/handoff.md; poll
progress messages at checkpoints with node agent/progress/manage.mjs poll.
