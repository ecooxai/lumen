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


## Linux handoff — 2026-10-28

- Working branch: `gpt-6-astra-engine-lead-linux-wpt-20261028`.
- Session start: `2026-10-28T03:50:07.309Z`; time limit: `08:50:07Z`.
- Original requested task is preserved in `agent/task.md`; iteration timing,
  scores, agents and rejected attempts are in `devsummary.md` and
  `agent/iteration-records/`.
- Whole-goal readiness remains **19/100 provisional**, not a pass percentage.
  Focused accepted-patch scores in the mid/high90s do not establish Chromium
  parity. Full WPT has not passed.
- WPT revision: `0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb`;
  `/mnt/p3/project/browser/wpt`; installed Chromium/ChromeDriver150.0.7871.114.
  Inventory has39,394 testharness URLs and71,398 automated WPT URLs across
  harness/reftest/crash/WebDriver/print/accessibility categories. The old93,035
  reference list mixed53,679 Test262 entries with web tests; keep them separate.
- Historical original-checkpoint8,000 testharness URLs:2,946 fully passing native
  versus6,691 Chromium;289,358/394,031 exact ChromiumPASS identities matched.
  This run was stopped for native capture-isolation review; partial shard004
  remains historical and must not be appended to the new guarded measurement.
- Fresh guarded original first2,000 completed separately with36verified launches,
  no startup failures:747 fully passing native versus1,693 Chromium;
  95,417/105,689 exact ChromiumPASS identities matched. The separate127 replay
  is complete below;128 has its own records and must not inherit127 results.
- Separate same-guard100-URL rendering baseline on accepted114 completed
  07:29:06UTC with198verified releases/exits:36PASS/62FAIL/1UNSUPPORTED/1TIMEOUT,
  identical to all100 prior114outcomes; Chromium95PASS/5FAIL. It excludes later
  font/sizing/position/flex work. Final127 own-reference replay is a separate gate.

### Safe reproducible test conditions

All new native WPT launches use `agent/validation/native_capture_guard.py`
through the existing runners. It pins a sealed refusal library, verifies a
pre-main PID/nonce ACK and withholds the author URL until validation succeeds.
Library SHA256:
`45643caa22bb659bc7b431acfc7b8af04d68f971c3610f4d56dcb2847d808900`.
Set `LUMEN_CAPTURE_GUARD_LIBRARY`, `LUMEN_CAPTURE_GUARD_SHA256` and a unique
absolute `LUMEN_CAPTURE_GUARD_AUDIT_DIR`. Do not weaken the required guard or
synthesize successful physical capture. The production Linux refusal and the
old-binary test guard are separate protections. Physical camera/microphone
acquisition remains unavailable pending real site consent. Synthetic, canvas and
file streams and playback are preserved; refusal is not a positive media conformance pass.

Use project-backed temporary storage via the short aliases
`/tmp/lumen-astra-checks-20261028` and `/tmp/lumen-astra-full-20261028`;
long direct TMPDIR paths exceed Chromium's Unix socket limit. Set child core
limits to zero. For `reftest_pilot.py`, use
`/mnt/p3/project/browser/wpt/_venv3/bin/python` (Pillow/WPT dependencies).
Retain one-second monitoring and the aggregate480%CPU budget. Keep original
attempts, timeouts and full assertion identities; do not weaken tests or relax
limits to inflate pass counts.

The software reftest oracle compares each browser with its own reference at
800×600,DPR1 after pinned upstream readiness. Native engine capture is an
internal CPU viewport buffer, even with GPU enabled; label it accordingly.
Separate actual X11 presented-window checks verified default wgpu/Vulkan on
NVIDIA GeForce MX110. This is bounded presentation proof, not WebGL/WebGPU
conformance or live-media-overlay coverage.

### Final128 correction and delivery status

Runtime commit: `23e1d1eb185d85fc08ee3b930c5f6abde476cb44`; frozen executable `build/codex-native/lumen-astra-i128`; SHA256 `e41f9f04359464d4a229a74371cc6badd25aa01f3289d585286db67cec5e8801`. Post-commit build ran08:29:11–08:29:18UTC with unchanged source; compiled-input manifest SHA256 `036bbc2268973e9e55d2599225e4e95d815f7f726cadff0c1c8fc5a08c44f48b`. Final128 source is six added/two removed lines in `src/layout/block.c` relative to the reviewed127 tree: first/last baseline static-position fallback follows the container logical axes, while explicit self-start/self-end retain child axes. This targets installed Chromium150 and pinned WPT behavior; the current CSS Align draft's safe self-axis fallback differs and is not claimed implemented.

The new original-control supplemental run exposed two reproducible127 last-baseline losses in vertical writing mode. Unchanged original→127→original runs reproduced8/28→22/28→8/28 with the same two lost passes. Candidate128 gives26/28 and repairs both without losing old passes. The identical supplemental200 now gives1,213/3,370 PASS assertions, +176/0 lost versus original and +4/0 versus127, all200 harnessesOK. Native128 suite is117/117PASS,41.40seconds CTest wall, with no skipped rows; elapsed suite timings are not a browser performance comparison. Final128 geometry24 retains18PASS/6FAIL exactly,48 verified/released/exited captures. Shared55 and the separate primary2,000/rendering100 replays are pending final disposition at this checkpoint; all127 results below remain explicitly antecedent.

The source archive's separate manifest identifies the exported source/docs commit and SHA256. Accepted runtime, archive commit and binary hash are distinct; source-tree equality is not a bit-for-bit reproducible-build claim.

### Retained127 verification antecedent

Accepted runtime commit: `163567579c077e35c7f6c6ed366162524530f5f8` (geometry `415ae50192c8218a68e6ba5b5b3575ff79ed3f98` plus cleanup and all earlier accepted repairs).
The exact exported source/docs commit, byte count and SHA256 are recorded in `agent/progress/runtime/src.tar.manifest.json` and the public static `publication-manifest.json`. The portable Git bundle identifies that exported source HEAD; the accepted runtime commit above is distinct.
Retained127 frozen binary: `build/codex-native/lumen-astra-i127`, SHA256 `a06dc09b0065a7727bb837c30fd5e5faf49a365b3401adbb1e31b4d4e143db88`. Actual post-commit relink ran07:32:35.661322–07:32:41.768763UTC and produced the same bytes as126. Compiled-input manifest SHA256 `8527cee83e3085dcd127af551a88ec2a2012c3d85ae4d5f5f4b7ff10b13e2ff1`.
Retained127 native suite:117/117PASS,26.25seconds CTest wall; binary unchanged. Run `python3 tools/codex_compile.py ctest --test-dir build/codex-native --output-on-failure -j4`. Focused55 unchanged accepted guards:5,505/5,963 assertionsPASS, all55harnessesOK, zero lost accepted identities/harnesses. The same guarded first 2,000 testharness URLs give baseline 747→final 765 fully passing URLs, native 98,485/107,088→100,737/108,961 PASS assertions, and exact matches of Chromium PASS assertions 95,417→97,712/105,689. Preserve all 2,757 raw gained and 505 raw lost native identities. Existing original→127→original batch isolates for input-events, editing and inline webfont fetch produced identical records; floated-stretch isolates were also identical with both existing runner methods. These do not replace the primary run or prove timing causality. There is no global zero-regression claim; remaining loss investigations are incomplete.

The same guarded 100 own-reference rendering URLs give capture114 baseline 36 PASS/62 FAIL/1 UNSUPPORTED/1 TIMEOUT→combined127 37/61/1/1, one Chromium PASS gain and zero lost passes. Chromium remains 95 PASS/5 FAIL; one native PASS/Chromium FAIL case persists unchanged. This baseline is accepted capture114, not the original pre-feature checkpoint. Final targeted geometry24 retains accepted125 18 PASS/6 FAIL exactly with48 verified/released/exited captures; the100 sample has198 such captures. These cohorts remain separate.

Retained127 gate timestamps: first2,000 07:38:24.334695–07:47:16.618940Z; rendering100 08:01:55.681285–08:09:24.983862Z; geometry24 08:09:54.142944–08:11:37.902762Z. The127 frozen executable remains preserved; default/named delivery identity is checked separately for128.

### Supplemental ordinary compatibility corpus

The separate supplemental layout corpus completed 2026-10-28T08:17:22.751753+00:00–2026-10-28T08:17:38.889905+00:00. It selected 200 of 235 eligible URLs from fixed grid/sizing/flex prefixes by hash order without inspecting outcomes, excluding the primary 2,000 and final 55. Final127:1,209/3,370 PASS assertions and54/200 fully passing URLs; retained same-URL Chromium150:3,337/3,370 PASS assertions and189/200 fully passing URLs. All200 harnesses wereOK in each engine. Exact ChromiumPASS matches:1,200/3,337; nine native-only PASS divergences. The guard verified16 launches with zero startup failures. Chromium results come from prior completed shards, not a simultaneous rerun. This first127-versus-Chromium phase preceded the separate original control, which exposed two baseline-alignment regressions subsequently corrected in128. The128 supplemental outcome is1,213/3,370 PASS, +176/0 versus original and +4/0 versus127. Counts are never merged into primary2,000 or rendering100 denominators.

The named host executable is `build/codex-native/lumen-linux-gpt-6-astra-builtin-agents-rubywebterm`; it is a byte-identical copy of frozen128 and the default executable, prepared as a review candidate while final disposition is pending. Final128 is `build/codex-native/lumen-astra-i128`, SHA256 `e41f9f04359464d4a229a74371cc6badd25aa01f3289d585286db67cec5e8801`. The128 x86_64 host dependency audit found154 resolved shared-library entries and no missing libraries, including SDL3, Fontconfig, FreeType, HarfBuzz, OpenSSL3 and libstdc++. This is host-specific validation, not a portable cross-distribution installer.

The actual128 showcase used `xvfb-run -a -s '-screen 0 960x720x24'` and inherited its fresh DISPLAY, with `SDL_VIDEODRIVER=x11`, `LUMEN_WINDOW=960x720`, `LUMEN_CONSOLE=2`, `LUMEN_NO_SESSION=1`, and a fresh temporary XDG_DATA_HOME/XDG_CONFIG_HOME. The executed browser argv was the absolute final128 path followed by `http://127.0.0.1:4187/`. LUMEN_WPT_CAPTURE, LUMEN_SHOT, LUMEN_SHOT_MS, LUMEN_PRE_FILE, LUMEN_TESTDRIVER and LUMEN_NO_GPU were unset. Renderer logs identified wgpu/Vulkan NVIDIA GeForce MX110. No claim is made that DISPLAY=:0 was tested by this recording. For an ordinary GUI launch, run the same executable from an existing authorized X11 terminal with its DISPLAY inherited; preserve the user's profile unless a fresh one is explicitly requested.

Live preview: https://a0bb38e846444140.5x7aqstuceku.opentunnel.xyz . Reviewed source is available at /src.tar.gz; its separate manifest identifies the archive/docs commit and source SHA256. Approved128 showcase: https://a0bb38e846444140.5x7aqstuceku.opentunnel.xyz/showcase.mp4 ,441,746bytes,SHA256840018657cd5e9d37de063b5908e4256f03febf03f0aedcbff7d9df50374f488,36seconds,960×720. It is actual128 X11 presentation recorded08:31:15–08:31:55UTC during verification, with large white model/agent/tool credits on black. Visible127 metrics and iteration11 remain historical. HTTPS GET200 bytes were rehashed08:36:42UTC. It is not a full-WPT demonstration.

PAGES_PUBLICATION_BLOCKED: Automatic approval review rejected source-branch push and Pages activation to ecooxai/lumen because explicit end-user authorization identifying that destination/source archive was not present. No push or activation was performed. The local six-file payload is prepared for dedicated branch gpt-6-astra-ops-preview-pages-20261028; do not split or indirectly route the rejected publication. The static page has no telemetry/message backend and links to the live tunnel. Messages persist between sessions, but replies require an active agent session.

Accepted areas include structured grid animation, in-process BroadcastChannel
and EventTarget branding, pending-image decode lifecycle, version-gated Fontconfig
matching performance, replaced-element aspect-ratio constraints, background
position animation, flex absolute-child static positioning, Linux capture refusal,
and reviewed test/build infrastructure. Background geometry125 is accepted
(quality96;24own-reference tests10→18PASS,+8/0loss,Chrome21PASS/1FAIL/2TIMEOUT;
25native color-list checks). Cleanup126 is accepted(quality97; two deterministic
actual-JS lifetime regressions fail before/pass after with eventual node balance;
five unchanged guarded cleanup repetitions pass). These scores are focused
review quality, not full-browser readiness. Remaining gaps include broad layout/rendering coverage, full multilayer
background images, image/SVG/natural-ratio cases, document.domain prerequisites,
separate-process BroadcastChannel IPC, long-run retention slope, tentative
outer-containing-block safe alignment, and unexecuted WPT categories.

Keep the final accepted binary as the normal `build/codex-native/lumen` entrypoint
only after all accepted changes are combined and verified. Preserve frozen
candidate hashes separately from source/archive commits; source-tree equality
alone is not a reproducible-build guarantee.

Public progress serves only reviewed aggregates, telemetry, messages and audited
source. The raw-results archive publication was automatically rejected and stays
local; do not publish, split, re-encode or route it through another destination.
Credentials, profiles, inbox data and QA screenshot collections are excluded from
source/Pages archives. The explicitly requested final real-screen video is a
separate reviewed deliverable. Existing Git-backed code must not be duplicated
into ChatGPT Library. `manage.mjs poll` reads new task messages; without an active
agent the server cannot respond autonomously after the session ends.

Seven legacy/unattributed files under `agent/validation/fixtures/` remain untracked and preserved; do not blanket-stage or delete them. Current generated build wrappers were moved with SHA/original-path metadata into ignored session evidence. The denied raw-publication generator is also retained only under ignored evidence.

## Next-agent investigation priorities from measured evidence

The primary2,000 report nominates grid and sizing (48 URLs,513 exact Chromium PASS gaps), embedded/script lifecycle (59 URLs,25 Chromium-fully-passing/native-incomplete pages), and SVG animation/interaction (23 URLs,18 such pages). These are investigation priorities, not diagnosed shared causes. Begin with one unchanged case and retain the guard, timeout and independent comparison requirements. Floated width:stretch90→110 is present in six fresh isolated original/final runs; its ordinary width-selection branch is byte-identical across original/final sources (SHA256e222b4ed83535e2f76735df5d373e7ebfae2f9037e3a3bf4309d6ac34fa124a4), while the primary session variation remains unexplained. Do not alter expectations to hide gaps.
