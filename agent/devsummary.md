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

## Iteration 2 — ordinary keyframes1703, independently qualified

Logical iteration start: `2026-11-02T13:17:45.775012+00:00` (previous1701 qualification closure).
Iteration end after independent retained review: `2026-11-02T13:58:03.041877+00:00`.
Elapsed wall time: **2417.266865s**. Cumulative session wall time from11:45:32: **7951.041877s**.
Last required execution ended `2026-11-02T13:54:15.546805+00:00`; core ended `2026-11-02T13:51:26.339993+00:00`;
independent audit ended separately at `2026-11-02T13:58:03.041877+00:00`. These are distinct timestamps, not extra test runs.
Objective score **56.33/100**, the last complete fixed-sample file rate703/1248; no broader1703 score is inferred.

Engineering role: engine_improvements authored ordinary CSSOM selector/list/EOF/precision fixes, preserved
the two named-range failures, and reviewed timing1704. Integration_validation independently reviewed,
selectively committed, built and qualified exact source `70d99dc0d8e9646f0a256bc3cf68aa47235b92c9` / app `38f8185c73bf17b3ec625bc30945216a19950a2bf7d9f597afb53519f11122ba`.
Delivery_performance independently reviewed keyframe runtime precision and audited all retained1318 rows.
Root provided scope/quality oversight and connector relay only. Some engineering preparation overlapped
iteration1; overlapping agent work is not summed into session elapsed. Model compute time is not exposed.

| Measured task | Start UTC | End UTC | Wall seconds | Outcome |
|---|---|---|---:|---|
| build-startup | 2026-11-02T13:47:53.888097+00:00 | 2026-11-02T13:48:40.052748+00:00 | 46.164653 | exit 0 |
| keyframe34 | 2026-11-02T13:48:40.088281+00:00 | 2026-11-02T13:48:44.572053+00:00 | 4.483769 | exit 0 |
| keyframe-eof-numeric13 | 2026-11-02T13:48:44.581266+00:00 | 2026-11-02T13:48:46.928140+00:00 | 2.346872 | exit 0 |
| keyframe-runtime2 | 2026-11-02T13:48:46.937510+00:00 | 2026-11-02T13:48:49.248835+00:00 | 2.311325 | exit 0 |
| css-property-rule21 | 2026-11-02T13:48:49.260630+00:00 | 2026-11-02T13:48:51.565257+00:00 | 2.304627 | exit 0 |
| descriptor-comparison13 | 2026-11-02T13:48:51.576519+00:00 | 2026-11-02T13:48:53.871212+00:00 | 2.294692 | exit 0 |
| keyframe8 | 2026-11-02T13:48:53.880602+00:00 | 2026-11-02T13:49:04.983469+00:00 | 11.102867 | exit 0 |
| native218 | 2026-11-02T13:49:04.993900+00:00 | 2026-11-02T13:51:26.125034+00:00 | 141.131133 | exit 0 |
| retained/authored38 | 2026-11-02T13:52:36.749576+00:00 | 2026-11-02T13:53:59.879078+00:00 | 83.129502 | exit 0 |
| retained/retained-additional14 | 2026-11-02T13:53:59.879283+00:00 | 2026-11-02T13:54:04.284623+00:00 | 4.405353 | exit 0 |
| retained/retained-extras143 | 2026-11-02T13:54:04.284941+00:00 | 2026-11-02T13:54:10.982010+00:00 | 6.697074 | exit 0 |
| retained/retained-universal40 | 2026-11-02T13:54:10.982215+00:00 | 2026-11-02T13:54:13.258804+00:00 | 2.276598 | exit 0 |
| retained/retained-frame4 | 2026-11-02T13:54:13.259014+00:00 | 2026-11-02T13:54:15.538018+00:00 | 2.279013 | exit 0 |

Build46.164653s; complete build/core212.461050s; retained45 campaign98.797229s. No individual task exceeded
five minutes. Process tree build peakRSS489.144MiB/PSS366.924MiB; shared cgroup mean302.202356%,
maximum381.496403% across46 one-second samples, below480% quota in every sample. Shared cgroup includes
the full slice, while process-tree snapshots may miss subsecond peaks; do not conflate their CPU measures.

Fixed8 assertions20/55→44/55 and complete files0/8→5/8; Chromium49/55 and6/8. Exact matched-identity
audit records24 gains, no prior-pass loss, missing/new assertion or harness regression. Five whole-file gains:
CSSKeyframeRule-keyText, CSSKeyframeRule-keyText-edge-cases, CSSKeyframeRule,
CSSKeyframesRule-appendRule-invalid and CSSKeyframesRule-deleteRule-detach.
Original authored34 remains32/34 (named-range normalization/escaped identifiers fail); separate13 and2 pass,
giving47/49 versus16/49 before and49/49 Chromium. Prior1701 owned21/21 and descriptor13/13 pass;
native218/218; retained45/45 and1318/1318,563/563 independent evidence checks.
Preserve synchronous getAnimations activation and unresolved named-range effect metadata as existing limitations.

Full1248 has not yet been replayed on1703;703/1248 and22604/26245 remain1603 measurements.
The earlier properties41 +68 assertions remains a separate, zero-overlap scope measured on1701.

Setup/review corrections are explicit: compact staged keyframe patch payload differs from the full local
review JSON only by pinned fixture transport representation; independent review caught the digest mismatch
before commit/build launch, and exact compact hash10bed2c3… was verified. A proposed timing6 selection
stopped before any writes/tests because animation-timing-function-valid.html is absent from fixed1248;
use original timing5 and its transition-valid alias rows. Reviewv1 is preserved and correctedv2 records this.
Timing42's first worker failed Python parsing before any test execution; its exact failed worker/log and
absence of outputs/progress were verified before a fresh retry with all nested worker layers parsed locally.
This is infrastructure/setup time, not engine failure or test time. Original30 timing controls and27/30
Chromium result are unchanged; versioned30 compatibility corrections plus12 differential rows have separate identities.

Public1701 push/readback4.351823s, public commit2ec6af18…; monitored archive/offline recovery4.157009s,
11/11 checks, all834 tracked members and734 build-input blobs/modes exact, archive SHA-256
55ab04c72136c8e35a6d4878d743eb0595c91dec27c1db06390be8fd22275bf0.
No public private-history transfer; final newer-candidate publication still pending.

Next:1704 timing, bounded MIME if safe, first integrated1248 replay14:30–14:45. Final60min remains reserved.

Checkpoint written 2026-11-02T14:05:07.616684+00:00, cumulative session wall seconds 8375.616684. Parallel intervals are not summed.


## Session18 — 2026-11-03 continuation and baseline recovery

Start UTC: **2026-11-03T06:58:59Z**. Nominal deadline **11:58:59Z**; up to one extra hour for an important bug.
Same-model built-in lanes: root CTO, repo_lead repository/source, wpt_baseline independent qualification, ops_recovery progress/delivery.
Tracked source was clean at13c22d3d28a68746fede3f265f55c5eda3247da6; existing committed history is the preservation checkpoint. Hundreds of prior untracked artifacts were preserved without staging or cleanup.
Created session18 branch; archived previous task/continue and recorded current request verbatim.
Saved1705 full selected1248 replay reports714 files/22637 assertions, +11 files/+33 assertions with no prior-pass loss; independent recount pending. This57.21% selected-file rate is not full WPT or an invented95/100 product rating.
Iteration1 start:2026-11-03T06:58:59Z; end pending. No session18 build/browser tests yet. Parallel-agent wall intervals will not be summed; model compute time is not exposed.
