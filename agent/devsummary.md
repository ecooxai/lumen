# Session17 development record — GPT-6 Astra Pro, built-in agents, RubyWebTerm

Task start:2026-11-02T11:45:32Z. Normal deadline:16:45:32UTC; optional extension17:45:32 only for important bugs. Original bootstrap minute rounding is corrected below.

## Iteration0 — recover source and connector transport

Started11:45UTC; ended2026-11-02T12:27:44.861542+00:00. Elapsed 2564.862 seconds. Root coordinates; integration_validation, engine_improvements and delivery_performance perform all implementation/test/deployment work. Initial child MCP execution stalled1719.2seconds, another inventory call234.2seconds; these are infrastructure time, not build or test time. Parallel worker waiting intervals are not summed into wall time. Root-only relay then recovered exact canonical source and prior evidence.

Source baseline:fe3e72d6d2248077abea748811ff9ca268e2fdae; binary SHA256:b2591720be38f312a42654fab3d864094796d3d9a3dfc0bf19e19d34bc4f7202. Historical fixed1248: Lumen703/1248 files,22604/26245 assertions; Chromium1062/1248 and24541/26064. These are unchanged historical measurements, not fresh tests. Browser objective remains incomplete; conservative iteration score56/100, with measured sample file rate56.33%. No full-WPT or95% claim.

Build time0; test time0 for this iteration. No source change or runtime improvement yet. Tracked tree was clean;284 pre-existing untracked artifacts retained. Read AGENTS, actual historical agents/continue and latest1603 handoff before branching. New branch gpt-6-astra-pro_builtin-agents_rubywebterm_session17-20261102; old instructions archived byte-for-byte, task saved verbatim.

Upcoming: CSSPropertyRule implementation, focused exact-identity comparison, matched resource repeat/profile, preserved native/browser regression gates, fixed1248 replay, checkpoints, final Linux/Android restart.

## Clock correction and iteration 1 — CSSPropertyRule incremental qualification

The user request timestamp is2026-11-02T11:45:32Z (first root clock11:45:42Z). The initial bootstrap used11:45:00Z;
its archived receipt is unchanged. All totals below use11:45:32Z. Initial child MCP stalls were infrastructure time,
not build/test time. Root-only request relay restored access; no unrelated terminals or processes were killed.

Iteration1 qualification window begins2026-11-02T12:30:43Z, after the resource-repeat window released. It ends
at the exact descriptor-comparison completion recorded below. Prior design/triage overlapped that earlier window;
parallel agent durations are not added together. Root handled orchestration; engine_improvements authored/reviewed
CSSOM code and frozen fixtures; integration_validation integrated, built, ran and audited qualification;
delivery_performance independently checked all retained fixture rows and runtime evidence.
Per-token/model computation times are not exposed by these tools and are not fabricated.

### Changes and scope

Candidate1701 adds side-effect-free native metadata parsing and CSSPropertyRule objects with read-only descriptors,
CSSRule.type semantics, canonical serialization, proper parent ownership, and no registration side effects during
CSSOM inspection. A pre-apply Chromium diagnostic caught explicit-empty versus missing initial values; the implementation
was corrected before its first build. Progress messaging privacy was fixed in a separate non-engine commit:
anonymous HTTP/WSS clients see no messages; the authenticated current session receives its own post and linked reply.

The exact properties41 baseline was freshly corroborated at521/813 assertions and16/41 fully passing files.
Candidate1701 has589/813 and18/41: **68 existing assertions gained, zero prior-pass losses, zero missing/new identities**.
Gains by file: at-property-cssom33, at-property18, idlharness10, register-property-syntax-parsing7.
The independent comparison passed18/18 execution checks. IDL is16/16 PASS. One pre-existing harness ERROR and224 failing
assertions remain. Chromium is1,097/1,137 assertions and34/41 files; its denominator is different.
Properties41 has zero URL overlap with the fixed1248 sample, so these are expanded-coverage gains.

### Measured build and test work

| Work | Start/end UTC | Measured duration | Outcome |
|---|---|---:|---|
| Matched resource repeats, three engines x three runs |12:29:42–12:30:43|60.4s|183/183 checks, all9 runs closed|
| Fresh unchanged1603 properties41 |12:34:01–12:34:58|57.113s|521/813;16/41; no identity/status drift|
| Chromium descriptor diagnostic + reference21 +1603 baseline21 |12:42:10–12:42:19|8.760s|13 observations;21/21;2/21 respectively|
| Candidate1701 build/startup, two jobs |12:45:28–12:46:19|51.138s|Both exit0; source frozen|
| Candidate1701 authored21 |12:56:44–12:56:46|2.294s|21/21, actual native exit0, cleanup/pins valid|
| Candidate1701 properties41 |12:56:48–12:57:44|56.151s cohort;57.144s monitored job|589/813;18/41; no previous-pass loss|
| Candidate1701 retained45 |13:03:15–13:04:53|98.760s|45/45,1,318/1,318;542/542 independent checks|
| Candidate1701 native218 |13:06:18–13:08:35|137.132s monitored job|218/218, no deferred cases; pins/cleanup valid|
| Descriptor13 exact-object comparison |See receipt below|See receipt below|Chromium13/13 and native13/13, separate differential scope|

Every build/browser/native job retained one-second process-tree and cgroup samples. Build peak RSS726.577MiB,
PSS460.447MiB; authoritative cgroup CPU weighted mean173.36% (one core100%), below shared480% quota.
Native218 peak RSS271.667MiB, PSS236.052MiB; cgroup weighted mean47.48%. No build/test above five minutes
occurred in this iteration. Short-lived peaks may fall between one-second samples; process-tree RSS may double-count shared pages.

Retained resource medians1603 versus313: work448.32ms versus421.76ms (+6.30%), CPU1.12957s versus1.14004s,
RSS191.173MiB versus192.741MiB. The previous single-run +29.53% work/+18.32% CPU increase did not repeat at that magnitude.
These three repeats do not establish a causal speed improvement or a statistical guarantee. Chromium's larger adapter/browser
process tree has a different architecture and its RSS is not a like-for-like memory claim.

### Preserved setup failures and oracle boundaries

Early all-in-one launch preflights stopped before tests because canonical41 inputs and retained authored fixture roots
were not yet staged. Exact source-hashed inputs were recovered and staged without modifying tests. Retained38 has8 entries
with candidate301-authored-fixtures prefixes; a dropped-prefix map preflight was corrected before any run.
Native218's first output path was rejected because the supported runner requires the session15 evidence root.
The standalone descriptor adapter first required an explicit audit directory, then reported the missing pinned capture
refusal library; zero browser releases/observations occurred. All failed receipts remain. The existing v3 wrapper supplied
its unchanged supported guard setup; an independently reviewed versioned fixture compares the same13 full frozen Chromium
objects with no field normalization. That differential13 is never added to original authored21, retained1,318 or WPT counts.
Successful gates were not repeated to repair unrelated setup failures. Delivery's first audit assumed boolean pass fields;
its v2 explicitly verifies109 existing status:"PASS" rows, preserving v1 and recording the schema correction.

Iteration score **56/100**, grounded in the still-current full fixed1248 result703/1248 (56.33%). Neither a passing build
nor focused100% fixtures means Chromium parity or a95/100 browser. Current candidate1701 has not yet replayed fixed1248.
The remaining goal is substantive: CSS math, CSSOM, animation grammar, typed attr, DOM and cascade gaps remain.

Descriptor equality comparison: 2026-11-02T13:17:43.457805+00:00 (Chromium) and 2026-11-02T13:17:45.763054+00:00 (native); total 5.673404 seconds. Iteration1 end: 2026-11-02T13:17:45.775012+00:00. Iteration1 elapsed wall time: 2822.775 seconds. Total session elapsed at this qualification: 5533.775 seconds. Checkpoint written: 2026-11-02T13:21:51.091311+00:00, session elapsed 5779.091 seconds. Parallel-agent intervals are not summed. Next iteration: ordinary keyframes1703, then timing1704;1702 unused reservation retired.
