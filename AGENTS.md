# Agent notes for Lumen

Start with the dated Linux handoff at the end of this file and `devsummary.md`. `agents/continue.md` and `agents/archive/` retain historical context; their aggregate WPT standings are not current-branch validation.

## Host compilation CPU budget

All new project compilation may share 60% of all host logical CPUs, per the
user's latest instruction (480% total on this eight-core host). This is a
shared budget, not 480% per task. Keep at most six compiler jobs per build;
use fewer for memory-heavy work. The old V8 source build remains safety-frozen;
do not resume or restart it while adopting the verified prebuilt.
Launch those commands through `python3 tools/codex_compile.py COMMAND ARGS...`.
The wrapper uses the shared systemd user slice
`codex-lumen-compile.slice` with `CPUQuota=480%` and a 100 ms period, and fails
closed if quota setup fails. The compatibility `--v8` option uses the same
shared slice. All builds inherit SCHED_IDLE and nice 19. Use Gradle
`--no-daemon` and do not reuse compiler daemons launched
outside the slice. Do not change unrelated host workloads. This is a host build limit,
not an Android browser runtime performance result.

Avoid unnecessary shell wrappers inside the capped scope. This host's ananicy
bash rule assigns nice -4, and two packaging checks stopped on that niceness
violation. Invoke Python/Java tools directly. For SDK apksigner verification,
use the same SDK's `lib/apksigner.jar` through `java -Xmx1024M -jar`, preserving
all verification arguments, instead of its shell launcher. Do not change global
host rules or weaken priority checks to work around this.

## CPU monitoring during every test

Whenever you run Lumen for a test (GUI, ChatGPT, scrolling, typing, video,
benchmarks), sample its CPU every second for the whole run and check it:

```sh
python3 agent/progress/monitor.py --root-pid EXACT_TASK_PID \
  --output agent/validation/UNIQUE_RUN/resources.csv --label "owned test process tree"
```

- Idle pages (nothing animating, no input) should sit near 0-3% CPU.
- Ordinary interaction (typing, scrolling, hover, clicking) should stay
  low; typing in the ChatGPT composer must stay under 10%.
- Sustained high CPU is only acceptable while doing real heavy work (page
  load, video decode, a long streamed reply). Anything else is a bug:
  profile it (`sample <pid> 5 -file out.txt` on macOS, `perf` on Linux),
  find the hot path and fix it before moving on.
- Report the measured CPU numbers with the test results.

## Independent resource monitoring

Every host test must retain one-second process-tree CPU/RSS samples; use
`agent/progress/monitor.py` with the exact task PID, and retain its CSV/summary.
Use `agent/progress/cgroup_monitor.py` for authoritative shared `cpu.stat`
deltas during concurrent work. Process samples can miss short-lived children;
new-PID tick estimates are not proof of quota violations. PSS is sampled less
frequently and RSS can double-count shared pages. Report these limits.

Keep CPU min/median/max, peak/final RSS, timing and distinct stderr/crash findings
with each substantive gate. Investigate a concrete crash, unexplained sustained
CPU, growing settled RSS or a guard threshold before broadening the run. Do not
change global scheduling, swap, driver or unrelated workloads. The experimental
CPUWeight tuning was reverted; no causal speed gain was established.

Use same-model built-in agents for independent review/test lanes when available,
as requested for this project session. A separate scripted monitor can satisfy
the one-second requirement without consuming another model slot. Keep source
and Git metadata frozen while a build records its provenance.

## Device test pages

`python3 tools/device_tests.py PAGE|GROUP ...` opens Lumen's test pages on the
phone (debug build), samples Lumen's CPU every second and prints the pages'
numeric results. Pages: cam, screen, mic, webgl, webgpu, aquarium, chatgpt
(newest chat bookmark), google, youtube, utils (WebGPU Weather World is under
its apps menu). For an explicitly authorized Android release, run the relevant approved
groups before release; `all` includes physical-media pages and requires separate
permission/isolation. It is not a gate for this Linux task. For a small authorized
device fix, run only relevant pages (e.g. `google`, or `gfx` for WebGL/WebGPU work). Each page gets its own cold start; `--tabs` opens them together, one
tab each (background tabs are throttled, so measure there only the front tab).
The script never taps or types; screen recording needs its system prompt
accepted. A settled site whose last-10-s CPU exceeds 5% is reported.
Sites are tested in desktop mode (Desktop site is on for all sites by
default): the desktop versions must work.

## Scripted UI tests

Prefer scripts over AI agents for UI tests; use an agent only when a script can't
do it. `python3 tools/edit_bar_test.py --serial SERIAL [--install] [--only typing,bar,more,clip,hold,showkbd]`
drives the Android edit bar on a local Draft.js page (tests/device/editbar), with
no person or agent. It locates things through the page's posts and Lumen's
`LumenBar` logcat lines, and samples CPU/RSS every second (that sampling counts as
the run's monitor). When a step fails, find out whether the script or Lumen is
wrong and fix that one.

## Collaboration and scope

Use built-in agents of the same model/capability when available. The current
session used GPT-6 Astra for implementation, independent conformance, operations
review and project management. Agree source-file ownership, index ownership and
whole-source/build freezes before concurrent changes; preserve and verify exact
patch restoration for isolated candidates. Do not edit source, generated build
inputs or Git metadata during a frozen build. Keep tests meaningful and bounded.

Preserve applicable privacy rules: do not enter private third-party page inputs,
inspect captured physical media, expose profiles/credentials/inbox data, or keep
screenshots containing personal content. Native Linux physical camera/microphone
acquisition is refused until real origin-consent UI exists. Do not treat a test
flag as capture isolation. Android/device media tests require their own explicit
permission and isolation; do not automatically run capture pages as a Linux gate.

## Live build on display :0

After finishing each main task, restart this repo's Lumen on the user's display so
they see the latest work (leave other Lumen builds, e.g. ../lumen/output/..., alone):

Before any restart, read `/tmp/lumen-display0.pid` and verify the live
`/proc/PID/exe` resolves to this repository's expected Lumen executable. A stale
PID file must never authorize terminating an unrelated process. Send a graceful
termination only to the verified task-owned process, then launch the final
accepted default binary on `DISPLAY=:0`, preserving the existing user profile.
Do not inspect or record the user's existing display; use a fresh isolated Xvfb
profile for showcase recording. Record the new exact PID for monitoring.

## Linux engine handoff — session 6, 2026-10-29

Evidence root: agent/validation/linux-wpt-20261029-session6. References prefixed session6 below resolve there.

This section supersedes the old session 4 result section, which is preserved in agent/handoffs/linux-session4-20261029.md. The host, collaboration and protected-validation policies above remain in force. This handoff records the tested candidate 200 source. Later documentation-only commits do not change that tested engine identity.

### Tested source and working branch

The final planned feature candidate is **200**, source **a4c26febf736ef6334f05421e1c0d69b24e5a411**, on private working branch **gpt-6-astra-engine-lead-ruby-restored-nonurl-20261029-1759**. Its immutable executable is build/codex-native/lumen-astra-i200, SHA256 **9ad329ef2ba4282bc7574f34575ed6405f315f7e1a5088d956b776ccfa8d3927**. Build and startup completed with exit zero in 23.08 seconds at 20:31:45 UTC. All **642 recorded source-set entries**, including tracked fixtures, were unchanged before and after: digest **3ba963997f731e4c447737f0ecccf566961be6ccf11951382419c164c3b76ecd**. Do not call every entry a compiler dependency.

The full tested path is /mnt/p3/project/browser/lumen-android/lumen-android/build/codex-native/lumen-astra-i200. The canonical build output is /mnt/p3/project/browser/lumen-android/lumen-android/build/codex-native/lumen. A 20:52:36 UTC check verified that both are executable regular files of 7,127,360 bytes with the same SHA256 above, but different inodes: they are separate identical copies, not a symlink or one shared inode. This verification does not imply a restarted GUI instance. See the final executable-identity receipt.

The task used the requested RubyWebterm environment after the user restarted it. The earlier local recovery came from the verified candidate 180 source/Git checkpoint; reviewed production differences were then integrated into the preserved remote history through per-file checks. The original dirty remote state was checkpointed before the new branch. No reset or blanket replacement of remote work occurred. The full user request and authenticated steering remain in agent/task.md.

Any later documentation-only commit must explicitly map back to the tested source above. Never relabel an untested working tree or archive-built executable as this measured binary. The tested environment is Linux; Android and portable binary compatibility were not established.

### Independent validation and accepted changes

Final native validation rebuilt and passed **140/140 non-URL entries**, with no skips. There are 143 registered entries; exactly url, url_host_nul and data_url are deferred. The final 14 authored scopes retained all accepted observations, with actual zero native exits, removed fresh profiles and unchanged sources. Final upstream retention also preserved forms 647/649 across 24 files, angle 89/184 across five files, intrinsic sizing 76/148 across two files, and Node.removeChild 28/28. Header/network feature gates remained 317/317 plus 11/11 neighbors. All exact-final scoped jobs drained at 20:35:07 UTC with joined cleanup, actual zero exits, removed profiles and unchanged sources.

The following scopes overlap. They are not summed into WPT coverage or an overall readiness score:

| Scope | Accepted result and qualification |
| --- | --- |
| Forms | 8,127/8,127 eligibility checks; upstream 647/649 in the fixed 24-file sample. Two dynamic :invalid selector checks remain. Disabled/readonly validity introspection, radio grouping, bounded decimal-step checks and maxlength insertion improved. |
| Text-control editing | 1,021/1,034 insertion fields, 160/160 coercion/event fields and 64/64 Symbol fields; no prior-pass losses. The 13 preexisting differences are one multiline textarea event-granularity field, four disabled-control focus fields, six unsupported email insertion fields and two number-input return fields. Full rich editing is not claimed. |
| HTTP, Fetch and XHR | Fixed 22-file network sample 317/317, five neighboring header files 11/11, conversion/state controls 235/235, private/public iteration 42/42, original header/body controls 120/120. Two Chromium reference nonpasses remain separately recorded. |
| Response cloning | 68/68 whole window/worker clone and public tee assertions; 114 stable authored reference fields plus five separately normative clone-error fields. Chromium's uncloneable-function reference stall is preserved. Public default-stream tee keeps its sharing semantics. |
| Intrinsic width sizing | 3,352/3,528 mutation fields, 270/672 descendant fields, 332/336 RTL supplement fields and 76/148 upstream assertions. All previous passes are retained. Strict subpixel/computed-padding differences remain; the repair is horizontal, not a stretch/vertical sizing implementation. |
| Table spacing | 960/960 authored fields retained, covering UA versus CSS defaults, inheritance, both axes, collapsed/empty tables and mutation. |
| Angle validation | 89/184 upstream assertions and 473/832 authored fields, with no lost prior passes. Invalid min/max angle grammar is 52/52; complete transform evaluation and serialization are not claimed. |
| CSSOM | Scanner 196/200 and original string controls 27/27. All 3,268 canonical-size observations stay unchanged: 2,447 Chromium matches and 821 retained differences. |
| DOM argument conversion | removeChild controls 32/32 plus 22/22 independent brand/cross-realm controls; upstream 28/28. Genuine Attr and nullable insertBefore behavior are preserved. |

Candidate **193** is a retained failed provisional result: unconditional 304 suppression caused an unsolicited streaming-head regression, giving 139/140 native tests. Separate candidate **194** repaired only actual stored-response revalidation, restoring 140/140 and 258/258 zstd controls. Candidate **197** is also retained as failed: 16 previously passing authored geometry fields regressed. Separate candidate **198** restored all 16, retained the new upstream gains and passed fresh native/table/RTL controls. Neither failed candidate is a release recommendation.

### Final selected non-URL comparison

The unchanged 640-case HTTP/window selection at WPT revision 0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb completed on 2026-10-29 at 20:54:49 UTC. Candidate 200 passes **380/640 full cases and 8,324/10,540 assertions**. The matching original candidate 176 passes 357/640 and 8,073/10,540; candidate 187 passes 368/640 and 8,206/10,540. The exact-identity comparison therefore records **23 additional full cases and 251 additional assertion passes versus 176, with zero lost passes, zero missing or added identities, and zero completion/status changes**. Versus 187, the gain is 12 full cases and 118 assertions, again without losses.

The same installed Chromium reference passes 544/640 full cases and 9,516/10,549 reported assertions. Exact union comparison retains 7,749 of its 9,516 passing identities, with 1,767 Chrome-pass gaps and 575 candidate-only passes; 14 identities are missing from candidate 200 and five from Chromium. Different reported totals are preserved. This selected cohort is not the full Web Platform Tests suite and does not certify general browser readiness. The scoped full-case score is 59.38/100; reported assertion pass rate is 78.98/100. Neither is an overall product-quality score.

All 640 final native processes exited with code zero, all fresh profiles were removed, no owned roots or cleanup workers remained, and source identities stayed unchanged. The candidate harness totals remain 616 OK, 23 TIMEOUT and one ERROR; assertion statuses are 8,324 PASS, 2,168 FAIL, 21 TIMEOUT and 27 NOTRUN. These existing failures remain in the result. The original 187 cleanup-receipt defect is retained as historical evidence rather than relabeled.

Authoritative summary: agent/validation/linux-wpt-20261029-session6/qa-final640-comparison200/summary.json, SHA256 **42cb9682263f9ac73b090cb0f5253d4e161743f221cbcf16bfa7d586b66e75a2**. Manifest SHA256: **8b5d0d491ce8b14e6ee8107d1213c10b90127a0193207d06057216bd54f3243f**. Exact comparison uses URL, assertion name and duplicate index; expectation and harness statuses were not edited.

### Performance and rendering qualification

The exact-final unchanged protocol compared immutable 176, accepted 200 and the same Chromium in 21 balanced measured fresh-profile trials, seven per browser. Every trial passed all 9,395 workload checks. The run lasted 95.607 seconds, 20:37:22.645129 to 20:38:58.254334 UTC. All protocol sources and binaries remained unchanged; all 14 native children have actual zero exits and removed profiles, and all seven Chromium ownership lists were empty with their profiles absent. No outliers were removed. These repeated workload checks are not WPT counts.

| CPU workload phase | Lumen 176 median [range], ms | Lumen 200 median [range], ms | Chromium median [range], ms |
| --- | --- | --- | --- |
| DOM creation/removal | 42.610 [40.021–44.212] | 41.407 [39.726–56.107] | 11.400 [9.800–18.500] |
| Style/layout | 361.112 [349.733–395.324] | 356.528 [343.246–388.828] | 58.400 [53.300–72.600] |
| Grid cache rebuild | 148.210 [134.519–154.885] | 142.210 [132.739–151.985] | 49.000 [44.800–62.700] |

Final times were lower in 4/7 DOM, 5/7 style and 5/7 grid matched triplets. Native ranges overlap, so this does not establish a broad speedup or regression. Chromium remains substantially faster in these measured phases. Median-of-trial sample medians for RSS/PSS were 164.416/139.274 MB (176), 165.218/139.974 MB (200) and 935.907/382.451 MB (Chromium). These are scoped process-tree samples, not steady-idle, whole-system or general-browsing footprint claims.

No separate warmup trials were launched. Native 200 media captures preceded the quiet comparison, so no cold-start comparison is claimed. Launch/readiness differs between guarded native and WebDriver and is excluded from speed comparisons. The fixed viewport was 748×596 CSS pixels at DPR 1, using explicit CPU/offscreen rendering. The raw helper label baseline128 maps to the verified 176 binary, not candidate 128; final maps to 200. Review session6/performance-v2-176-200/reviewed-summary.json and cleanup-reviewed.json for the exact sequence, hashes, paired effects and cleanup proof. The baseline source is b4d8147c7b7945f5eddc11cfc444a80dd5a9ff13, binary SHA256 648145f52b7c11b0a95c2c4aa99f0527879a757969998fade4de15773ce70f01.

Earlier candidate 195's canonical pixel-size setter optimization preserved all semantic observations. Its separate 12,289-check diagnostic reduced setter median 58.54 to 35.46 ms across seven improving pairs, but the later direct-attribute phase slowed 14.60 to 20.16 ms in the same fixed-order process. The unchanged 9,395-check full workload was inconclusive (style/layout median 419.05 to 408.52 ms with overlapping ranges). Preserve both the benefit and adverse observation; do not claim a general browser speedup from that diagnostic.

Resource samples are browser/process-tree measurements. Build collectors sample RSS every second and PSS less often; the final performance report declares its own one-second sampling policy and median-of-trial aggregation. Summed RSS repeats shared mappings; startup/work/cleanup samples are not steady-idle or whole-host memory claims. All explicit trial exits and profile-removal receipts remain part of acceptance. Ordinary desktop-close behavior is a separate unverified path. The required DISPLAY=:0 restart was not performed: /tmp/lumen-display0.pid was absent during the final preflight, so the AGENTS identity rule could not verify a task-owned target, and no existing launcher/profile recipe was recovered. No signal, new GUI launch or display inspection occurred. This does not invalidate the separately guarded test exits or CPU/offscreen captures.

Separate Intel UHD 620 Vulkan enumeration succeeded, while the NVIDIA probe failed. Actual Lumen hardware rendering remains unverified; the measured acceptance workload uses explicit CPU/offscreen rendering. Two actual candidate 200 CPU/offscreen frames were captured successfully through the supported guarded runner before the final timing interval. They show this native binary, not a recreated webpage. The native showcase is now published at https://ecooxai.github.io/lumen/showcase.mp4, with poster https://ecooxai.github.io/lumen/poster.png; final bytes were independently downloaded, hashed and visually reviewed.

### Supported reproduction and protected interfaces

Project root: /mnt/p3/project/browser/lumen-android/lumen-android. WPT checkout: /mnt/p3/project/browser/wpt, pinned at **0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb**. Installed Chromium is 150.0.7871.114 at /home/a/.local/archpkg/pacman/usr/bin/chromium; its actual ELF SHA256 is **2d4adea820c95eee0c24dcc5eb729043df570838b8693fa3449df15040b0460f**. Retain exact launch/ELF receipts, not just a launcher filename.

Build with the shared quota wrapper:

    python3 tools/codex_compile.py python3 agent/validation/build_candidate.py --iteration NEW_ID --jobs 2 --startup

Never overwrite existing immutable candidate binaries or output directories. Keep one-second owned-process CPU/RAM evidence during every test/build. Supported interfaces are agent/validation/run_guarded_fixture.py, agent/validation/run_chromium_fixture.py and agent/validation/run_guarded_cohort.py (paired_wpt). The cohort wrapper joins cleanup; an older runner's original 638/640 exit-receipt defect remains historical evidence and was not silently discarded.

An actually executed candidate-200 fixture command is below. First configure the published README.capture-refusal.md environment/pin and a fresh caller-owned audit directory. Replace NEW_ABSOLUTE_OUTPUT_DIRECTORY with a new absolute directory; never reuse sealed output.

    python3 tools/codex_compile.py python3 agent/validation/run_guarded_fixture.py --binary /mnt/p3/project/browser/lumen-android/lumen-android/build/codex-native/lumen-astra-i200 --fixture /mnt/p3/project/browser/lumen-android/lumen-android/agent/ops/linux-restoration-20261029/showcase-final/native-showcase-filled-candidate200-v2.html --marker ops-native-showcase --output NEW_ABSOLUTE_OUTPUT_DIRECTORY --timeout 12 --cleanup-timeout 15

The fixture SHA256 is 8d137314087d906f417fcae840aeaf3cf0fd74c97073146d4599ff4f48d6e593. Its actual run returned runner/native exit zero, removed the fresh profile and retained source identity. Optional documented application media settings were LUMEN_SHOT pointing to the new output/native.png and LUMEN_SHOT_MS=1; they do not replace or bypass guard configuration. This is an isolated fixture launch, not a command to restart the user's existing display.

Approved capture configuration is documented in agent/validation/README.capture-refusal.md. The original approved library location is agent/validation/linux-wpt-20261028/capture-refusal/capture_guard.so. Use the published pin/configuration through supported launchers. **Do not read, rebuild, rehash, replace or weaken protected guard libraries, private capture helpers or auth records.** Preserve capture refusals, cleared LUMEN_WPT_CTRL/PRE, fresh profiles/audits and source/cleanup seals. No missing guard may be replaced by a stub or bypass.

The unchanged performance CLI remains under agent/validation/linux-wpt-20261028-session2/performance-final/run_performance_v2.py, with --baseline, --final, --chromium, --mode measure and a new --output directory, through the quota wrapper. Public progress health is `node agent/progress/manage.mjs health`. Do not use its private capability URL command in logs or public artifacts.

External Ahem and ChTestZeroWidthZero test fonts remain required; their documented configuration and missing-font skip behavior are unchanged. Source archives do not include dependency caches or promise hermetic builds.

### Exact final evidence locations

Under session6, read qa-final200-identity.json; qa-final-200-authored-summary.json (14 scopes); qa-final-200-upstream-summary.json (forms 24, angle 5, intrinsic 2 and Node 1); and qa-native-final200/summary.json with results.xml. Header and clone details are qa-header200-upstream-comparison.json, qa-header-clone200/comparison.json, qa-header-order200/comparison.json, qa-header-controls200/comparison.json and qa-clone-controls-v2-200/parsed-result.json. The latter retains 114 reference fields and five separately normative fields; do not merge the reference exception away.

The final broad raw runs are qa-broad-lumen200-final-v3/shard0 and shard1. The final joined summary is qa-final640-comparison200/summary.json (completed and sealed). Retain the original 176/187 baselines and Chromium reference, raw failures, selected manifest and exact assertion identities. Final benchmark records are performance-v2-176-200/reviewed-summary.json, reviewed-public-summary.json and cleanup-reviewed.json.

### Remaining priorities and review limits

The requested 98/100 whole-browser goal is not certified. URL conformance is intentionally deferred. The largest remaining Chromium-pass gaps in the frozen selection are CSS values (552), sizing (405), grid (224), DOM parsing (158), variables (147) and flex (112). Prioritize general feature support such as stretch sizing, intrinsic-size animation, typed attr(), logical/orthogonal grid sizing and CSS parsing. The preserved ranked file list and examples are in qa-final640-comparison200/remaining-priorities.json. These are follow-up scopes, not reasons to modify the frozen delivered candidate.

Known bounded residuals include four CSSOM boundary-comment serialization observations, two percentage-translate computed-matrix controls, and 16 numeric/stepUp interoperability fields. Do not imitate Chromium's negative-minimum stepUp discrepancy blindly: one step from -0.35 with min -0.05 and step 0.1 yields -0.05 under the reviewed HTML minimum-clamp algorithm, while the reference reports +0.05. Raw decimal precision beyond Number and large-number formatting remain distinct limitations.

Angle coverage still has 21 computed-unit serialization failures, 19 specified-expression simplification failures and 19 computed transforms returning none in its frozen serialization scope. An authored rotate(30deg) case still reports an untransformed bounding rectangle; painted pixels were not verified. Four RTL supplement holdouts involve mixed-text first-atomic positioning. Candidate 198 only adds neutral ordering for text-free direct atomic lines; it is not a general Unicode bidi implementation.

A :valid/:invalid bridge should reuse shared validity rules with correct realm ownership, callback lifetime and invalidation. Avoid executing author-overridden JavaScript getters during native selector/style matching; form, radio peers, ancestors and sibling/:has dependencies need coherent invalidation. Typed attr(), stretch sizing, intrinsic interpolation and vertical layout need broader work than this session's bounded changes.

EventSource is currently undefined. If it is implemented later, SSE reconnection IDs require explicit UTF-8 encoding before crossing the ByteString HTTP header boundary. Do not treat the current Fetch/XHR repair as SSE support.

Some private source-excerpt requests were rejected by automatic review. No denied excerpts or private bulk archive were rerouted. The latest intrinsic state-restoration implementation has author review plus independent frozen behavioral validation; do not claim an independent source review of that blocked excerpt. Approved production-source edits and fresh narrow fixtures continued directly through RubyWebterm.

### Public source, private history and accounting

The final public source staging is independently audited against the exact candidate 200 Git commit: 710 files, 10,564,950 bytes, with all 642 recorded source-set entries matching. Its source manifest SHA256 is a46f2f4f3be33835b63e82ead4721a596ebb84822108d44a2b7d95885ec84a4e. Final public metadata, the embedded-bundle source package, a normal source-branch push, GitHub Pages publication and the owner-private Site deployment are complete. Their published bytes were independently downloaded and verified.

The preceding historical durable source package is candidate 196, clean public export commit cfccb61082563409be18d0c8e9b981d2c68119e7. Its source tar SHA256 is 4d24c13fd96ccd70b14ffd3db00b9de25dd2c2ed4533d329d8f177942ce9bc80 and embedded public-history bundle SHA256 is 81aac52b0a507aa9e12a40bbfc4b90a0b08a619bfd060b935648764cfa191792. Offline restore verified its exact tree, blobs and modes. The completed candidate 200 export extends this public ancestry without publishing the private working history. Release operations own the separate repository under agent/ops/linux-restoration-20261029/public-export186 and proposed branch gpt-6-astra-release-ops-linux-nonurl-source-20261029.

Public exports must use a separately audited source history rooted in existing public ancestry, never the private working branch containing task text, diaries, private validation, profiles or runtime capabilities. Preserve normal non-forced pushes and existing default branches. Every source tar must contain the reviewed public Git bundle, restore instructions and original-to-export mapping; verify an offline clone's commit, tree, blobs and executable modes.

The session began **2026-10-29 17:25:47 UTC** and was closed for handoff at **2026-10-29T21:14:52.039463+00:00**, after **229.084 elapsed wall minutes**. This is the actual completion time, not the planned 21:25:47 deadline and not a claim of four full hours. The ledger contains **21 session iterations (1–21)** across **20 distinct candidate identities (181–200)**; candidate 184 has a local trial and a separate repaired Ruby iteration. Candidate 180 was recovered input, while older candidate numbers are historical. Failed provisional 193 and 197 are preserved. The per-iteration ledger is devsummary.md; overlapping agent intervals are not summed. Agents used GPT-6 Astra with built-in collaboration: root project management, engine_lead source/Git integration, conformance_qa independent tests and release_ops deployment/performance. Source, output and evidence identities—not a high authored-field percentage—determine what was actually validated.

## Final published release and next-agent handoff

The tested engine remains **a4c26febf736ef6334f05421e1c0d69b24e5a411**. Later private commits change documentation and the public progress UI only. The source export is **3b9a6a5fff8b925067a560367cccc00fb8098d67** on **gpt-6-astra-release-ops-linux-nonurl-source-20261029**; the normal Pages push is **ceb2d0644a4f28bb2ce39bc8426e002d710264c9**. Public report and downloads: https://ecooxai.github.io/lumen/ ; package manifest: https://ecooxai.github.io/lumen/release-manifest200.json .

The source tar at https://ecooxai.github.io/lumen/src.tar.gz is **9,316,289 bytes**, SHA256 **cb396f9c3dc0a0b7cbb8a59dcaa073f5ce5bf902f54680c6cce317e23322989e**. Its embedded self-contained public-history bundle SHA256 is **58197538ef39e7bb751c2db119672dbdd03210dc6ebeba834ffde7797231123b**. Offline restore verified all **725 tracked paths, blobs and executable modes**: 710 exact source files plus 15 reviewed public metadata files. The exported tree was not separately rebuilt and is not a portable executable package. The older guarded /src.tar.gz on the Ruby progress service remains historical candidate 168; the new dashboard links directly to the verified candidate 200 Pages archive.

Published video SHA256: **333027d638a26f2ed53eb45173ba59fe7d620c24b32515db7dd016549be71926**. Poster SHA256: **b4a1feee4412d0d1c5d1525faf1ce0aa96d0f8644b2f32bd5c947dc2120f21bf**. Both use actual native CPU/offscreen frames; no user display was inspected or recorded. The owner-private Site remains https://lumen-linux-gpt6astra-agents-rubywebterm.my-team-8435.chatgpt.site/ , final deployment appgdep_6ae3b6629ffc819197e4a9f6dd4af9f7.

The final durable checkpoint is libfile_83832d3596c88191a5bb3f681f64948b, 9,918,699 bytes, SHA256 **1b6b884a55871174501492379dcc4af538883c162bbf8392e0663da2f682422d**. The standalone delivery receipt is libfile_74fcd2f86e20819198dc72e4e9fa52db, SHA256 **acb4347121945eef9a670867b0535568a23d40f621ab5b2c6d4850985e9f26e3**. These identifiers are private handoff metadata; they are not part of the public source history.

### Feedback and desktop limitations

The earlier local feedback listener on port 8080 remains unavailable. Public launch/owner metadata did not contain its original invocation. A safe public-help investigation was stopped before the entry point executed because assigning a new empty scratch directory to a reduced UID/GID failed with EINVAL in the user namespace. Original state and authentication contents were never read or changed. Automatic approval review separately rejected restarting a mixed OpenTunnel profile because it would activate an unverified feedback route together with reviewed public media; that denied restart was not performed. Reviewed public media and source were published independently. Do not invent replacement authentication, infer a launch command, or replay the denied mixed-profile restart.

The current Ruby progress page remains live and its telemetry, final source link, evidence arrays and bottom-right scoped score were visually checked. The agent work session is now finished; historical messages may remain queued, but unattended replies are not promised. The required DISPLAY=:0 restart remains unperformed for the separately documented absent PID/launcher reason.

For the next development task, start from the exact tested candidate 200 source and these unchanged guard policies. Treat the current 380/640 selection, all retained failures and the 98/100 goal shortfall as the baseline. New engine changes require fresh affected controls and a new broad comparison; do not relabel this release's evidence as covering later edits. The ranked remaining CSS/layout/parser failures are preserved in qa-final640-comparison200/remaining-priorities.json.
