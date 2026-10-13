# Lumen: handoff notes

## Android physical-pixel scale (2026-10-13)

Android now uses an explicit physical-pixel scale independent of SDL/Android
display density. Default2x means one layout pixel occupies2x2 surface pixels;
1x is1:1, not supersampling. Settings offers1x/1.5x/2x/2.5x/3x and saves the
selection. Strict parsing rejects malformed/out-of-range values. Screen mode,
actual app framebuffer and pre-scale layout dimensions are shown separately;
the render buffer stays at native surface resolution. Readouts refresh on paint,
including rotation/keyboard resize, without a polling timer. Cached Settings
tabs refresh selected buttons without repeated DOM mutations. Settings actions
now require immutable generated-page provenance, not the editable address text.
Desktop coordinate behavior remains unchanged. SDL contracts checked against
the pinned source and https://wiki.libsdl.org/SDL3/SDL_DisplayMode and
https://wiki.libsdl.org/SDL3/SDL_GetWindowSizeInPixels .

Tests first: initial33 cases yield15 expected assertion failures, zero setup
errors (`build/codex-android-pixel-scale/red/REPORT.md`). Partial hostGREEN161
passes before main finds stale selection on cached Settings tabs; separate
selection RED confirms it before the fix. Final GREEN passes162 focused cases,
12 touch sanitizer scenarios,6 JS viewport groups, native cache sanitizers and
actual prebuilt-host-V8 runtime, plus23 unchanged APK gates. Normal/fresh compact
payloads match, zero ZIP gaps, native no-op passes. Build24.112s, mean229.76% CPU,
peak sampled RSS1724.64MiB; runtime0.262s/51.28%/68.16MiB (startup, not idle).
Shared480%/SCHED_IDLE/nice19,304 source pins, frozen source-V8 and7 hashes intact;
41 owned scopes drained. Report: `build/codex-android-pixel-scale/final/REPORT.md`.
APK: `build/codex-android-pixel-scale/final/codex-agent-android-physical-pixel-scale-compact-debug.apk`,
87,484,692 bytes,0444, SHA256
`83e59871d75d4211c653d818a61263b84b363c11cee59051da323f143de3c265`.
Host/build score95/100; phone acceptance still pending. No broad browser pass.

New user report: Android scrolling is very slow; requests default Vulkan/wgpu
acceleration and backend switching in Settings. Source already requests wgpu
Vulkan on Android, but rasterizes page pixels on CPU before GPU composition.
Actual active phone backend remains unconfirmed. Touch currently has no fling
momentum. Next phone run should capture own-app startup backend evidence and a
controlled scrolling baseline after pixel-scale acceptance. Android wgpu build
metadata includes vulkan,gles,wgsl. No renderer/scroll production changes yet.

## Android New Tab checkpoint (2026-10-13)

Android web content now uses the full available surface width. Workspace icon
cards and open tabs live in the default New Tab Home, including landscape;
new tabs do not automatically open the keyboard. Main implementation is5996691.
Follow-up fixes center the icon/badge and prevent the invisible sidebar CPU HUD
from requesting full repaints. Tests preceded both fixes. Final verification:
153 focused checks,6 viewport JS groups, native sanitizer/runtime checks and23
APK gates pass. Build26.108s, mean227.91% host CPU, sampled peak1725.81MiB RSS;
shared480%/SCHED_IDLE/nice19, source pins and frozen source-V8 remain intact.
Report: `build/codex-android-newtab/final/REPORT.md`.

Installed, data-preserving candidate:
`build/codex-android-newtab/final/codex-agent-android-newtab-fullwidth-compact-debug.apk`,
87,484,692 bytes; SHA256
`5d708cfbd115048c477a8682825235c0029d80057ce023c36ec7e8e992dea2db`.
Fresh phone acceptance passes default Home, icon containment, workspace add/switch,
native plus, rotation, edge-to-edge fixture and direct scrolling. Formal idle:
60.68s, mean0.198% CPU, peak5.56%, longest above3% streak1, RSS226–228MiB.
215 continuous samples have zero failures; cadence0.98–1.32s. Original59.72s
duration-only failure is preserved, with one bounded harness correction.
Prior bad-badge/high-CPU runs and inconclusive old-candidate profiling are
preserved, not retroactively called passes. Phone is1080x2248 with existing320dpi
override (~540CSS portrait), not a320CSS physical-phone test. Test-created tabs
and workspace removed, original New Tab restored, app stopped, rotation/density
preserved, owned server/reverse removed and locks released. Report:
`build/codex-android-newtab/acceptance/REPORT.md`.

Bounded phone score90/100: a fixture paragraph appears after the subsequent END
heading in the scrolled image; this flow anomaly remains undiagnosed and is not
a layout pass. No public-site/account, typing-CPU or broad daily-use pass.
Host/build score95/100; overall daily-use readiness remains25/100.

Next user request: use a physical-pixel-based Android scale independent of system
density, default2x, configurable in Settings, with screen and render resolution
readouts. Proposed interpretation:1x maps one layout pixel to one physical pixel;
2x maps it to2x2 physical pixels, without supersampling. Clarification requested.

## Android/Linux checkpoint (2026-10-12)

### Current viewport event iteration

Main implements trusted window/VisualViewport resize notifications and viewport-
dependent matchMedia change notifications. A per-context native size/DPR snapshot
coalesces changes in the existing JS tick, after timers and before animation
frames; unchanged ticks add no JS callback, allocation or polling timer. Changed
viewports synchronize style/layout before dispatch. VisualViewport now inherits
EventTarget, exposes live CSS geometry/page offsets and rejects construction.
Media notification snapshots creation order and prunes dead weak references.
Reference: https://drafts.csswg.org/cssom-view/#resizing-viewports . This is not
full CSSOM conformance: separate visual/layout viewport zoom, pinch, visual
scroll/scrollend production, color-scheme-only notification and listener lifetime
across weak-reference collection still need work. No WPT or major-site pass is
claimed.

Tests precede implementation. Fresh RED records6 JS assertion failures, the
native cache rotation failure and an actual prebuilt-host-V8 VisualViewport
failure. An initial Node priority failure is preserved: host ananicy assigns
nice16. A pinned, byte-identical local Node alias avoids that rule while keeping
shared480%/SCHED_IDLE/nice19; no global policy change. The real engine RED abort
takes0.195s, mean52.94% CPU, peak RSS69.56 MiB. Report:
`build/codex-android-viewport-events/red/REPORT.md`.

Fresh GREEN passes6 JS groups, ASan/UBSan cache state, actual V8 runtime,
140 prior focused regressions,12 gesture sanitizer scenarios and23 APK gates.
Runtime assertions take0.137s, mean78.00% CPU, peak RSS70.57 MiB. This is startup
work, not idle evidence. Android assembly takes28.210s, mean241.52% scope CPU,
sampled peak RSS1766.89 MiB. Four JS objects rebuilt,290 reused; native no-op,
SDK/DEX, normal-to-compact payload identity and zero ZIP gaps pass. Only
libmain.so differs from the touch candidate. Warnings remain. All39 owned scopes
and descendants are gone;300 source pins and frozen V8/seven inputs remain
unchanged at verifier completion. Main independently checks APK/test hashes and
accepts95/100 bounded host/build quality; overall daily-use readiness stays25/100.
Report: `build/codex-android-viewport-events/green/REPORT.md`.
APK: `build/codex-android-viewport-events/green/codex-android-viewport-events-compact-debug.apk`,
87,468,308 bytes,0444, SHA256
`b9ecefe00702597ddf42e471a6ac949166c446c43328017f6e98216a18722098`.
No phone runtime or installation of this candidate yet. Host runtime uses
V8 15.6.75.8; Android uses prebuilt15.0.245.2, so device testing is still required.

User now explicitly authorizes closing Lumen, install-r with data preserved and
phone tests. User also requests a new narrow-phone UI: move workspace controls
into the default New Tab area and give web content full screen width. Implement
that test-first next, then use a fresh exclusive phone verifier for the combined
candidate. Capture pidof stdout/stderr/returncode separately, not the old merged
preflight check. Preserve previous runs; maintain1Hz process/thread CPU andRSS
through GUI work/cleanup, restore device settings, and do not access accounts.
No phone agent currently owns a grant or lock.

### Current Android touch iteration

The preceding turn made concrete progress: the sampler fix and9 host tests are
committed in `32da8d4`, with honest preflight limitations in `57a7015`. Phone
takeover permission remains unanswered; no agent has current phone authorization.
Host development continues rather than treating that as a project-wide blocker.

Main adds Android single-finger drag scrolling using the existing page/nested
scroll path, rather than a separate renderer. Tests precede production changes:
12 compiled gesture scenarios fail against a no-op API, plus7 missing wiring
assertions across3 methods. No setup errors; RED evidence:
`build/codex-android-touch/red/REPORT.md`. Seven extracted app-adapter model
scenarios were also written before implementation but not separately run RED.
The implementation recognizes an8 CSS-unit drag threshold, defers taps until
release, suppresses duplicate SDL touch-generated mouse events and leaves real
mouse input intact. It maps normalized fingers to density-independent viewport
coordinates and cancels gestures on focus/background/resize/key/context changes.
Multi-touch suppresses taps/scroll rather than misfiring; it is not pinch support.
Tracking is bounded, with no per-event allocation or polling timer. Direct drag
does not yet provide fling inertia, long-press selection, DOM touch events or
touch-action arbitration. Native nested-scroll target capture also needs broader
site testing. These are remaining daily-use requirements, not declared supported.
The first verifier passes140 unique focused groups and12 ASan/UBSan gesture
scenarios, but assembly stops before native compilation because inherited
`ANDROID_SDK_ROOT=/home/a/Android/Sdk` conflicts with pinned `ANDROID_HOME`.
Report: `build/codex-android-touch/green/REPORT.md`, explicitly PARTIAL. Failed
configuration takes20.149s at mean232.65% CPU and peak sampled RSS539.38 MiB;
480%/SCHED_IDLE/nice19 and frozen V8 remain intact. Main then only renames new
coordinate locals/parameters/result fields descriptively, without changing the
gesture behavior. A fresh verifier under `build/codex-android-touch/final` reruns
the final bytes with `ANDROID_SDK_ROOT` explicitly matched to pinned
`ANDROID_HOME` after sourcing the environment outside the compile wrapper.
Final verification passes140/140 unique focused checks,12/12 sanitizer scenarios
and23/23 unchanged packaging gates. Implementation/tests are committed in
`0ada96f`. Normal assembly takes48.031s at mean213.82%
CPU, sampled peak RSS1759.38 MiB. One new touch object and rebuilt main object;
292 other objects reused. Normal/fresh compact live payloads match with zero
ZIP gaps; only libmain.so changes. Actual SDL/SDK compilation and native no-op
checks pass. Shared480%/SCHED_IDLE/nice19 and frozen V8 remain unchanged.
Candidate: `build/codex-android-touch/final/codex-android-touch-compact-debug.apk`,
87,468,308 bytes,0444, SHA256
`d30911ad5f92cd45e8d2c3a99f3984b3a21a1bb67fe828b1862f515a9f3d3991`.
Report: `build/codex-android-touch/final/REPORT.md`. One report-only correction
records allowed concurrent documentation drift; warnings remain. Main verifies
APK/source/test hashes and accepts95/100 bounded build quality, not daily-use
readiness. No phone runtime or CPU claim for the new adapter yet; it is not
installed. The previous installed APK remains the focus candidate. Once device
permission is clarified, use a fresh verifier that first records separate pidof
returncode/stdout/stderr, then tests this candidate with the corrected collector:
real taps, direct swipes without accidental link clicks, nested scrolling,
resize/IME/background regression and1Hz CPU/RSS. Do not overwrite historical
failed runs or claim the new sampler has already been verified on Android.
The original failed report finalizer remains preserved. Its corrected guarded
run is `finalize-report-correction1.result.json` (passed, payload/wrapper exit0,
7 policy samples, no issues); `final-audit.json` records the allowed docs hashes.
Guard completion drains owned descendants. Main checks this evidence before
closing the verifier; no post-commit rerun is claimed.
Overall daily-use readiness remains25/100 pending device and major-site evidence.

### Previous density/focus iteration

**Android density/input and focus-highlight fixes committed; phone verification partial.**
The preceding viewport iteration was concrete progress: committed engine fixes,
host/build passes and protected API35 resize/IME phone evidence, not a status-only
turn. This iteration keeps Android as the priority; Linux development is deferred.
Tests were added before production edits. Density RED executes six extracted
`update_size` model failures, three missing-input-adapter prerequisites and four
static-wiring failures; both Android/desktop variants compile without errors.
The desktop/invalid-scale failures concern the new input-scale contract and do
not establish a preexisting desktop rendering regression. Responsive RED first
failed setup because the new fixture omitted HTML sources. Main added the HTML
sources; a fresh verifier then executes two initial portrait-style failures and
one wide-style pass using the actual CSS/DOM/HTML engine. The failed rotation
scenario stops at its portrait prerequisite, not a separately exercised rotation.
Evidence: `build/codex-android-density/red/REPORT.md` and
`build/codex-android-density/red-responsive/REPORT.md`. Test compiler errors now
print diagnostics, with bounded compilation timeouts.

The patch derives Android CSS/UI units from SDL display scale while retaining
the physical framebuffer size, maps pointer positions and relative motion into
those units, preserves wheel ticks, and refreshes metrics on scale-change events.
Desktop SDL logical coordinates remain unchanged. Canvas/icon caches include
scale; page and iframe style environments receive the presentation DPR. Internal
pages stack navigation above content below600 CSS pixels, retaining the wide
sidebar layout above that threshold. Existing native toolbar/workspace sizes are
not redesigned, nor are touch scrolling or JavaScript resize notifications added.
SDL contract checked against the pinned local source and official references:
https://wiki.libsdl.org/SDL3/SDL_GetWindowDisplayScale
https://wiki.libsdl.org/SDL3/SDL_MouseMotionEvent
Commit `a640356` passes16 new groups and the previous93 focused checks:109/109,
plus23/23 unchanged packaging gates. Only `main.c.o` rebuilt,292 objects reused;
normal assembly29.395 seconds, mean218.13% CPU, sampled peak RSS1785.80 MiB.
Shared480%/SCHED_IDLE/nice19 held. Normal assembly then fresh packaging yields
identical live payloads and zero ZIP gaps; only libmain.so changes. Candidate:
`build/codex-android-density/green/codex-android-density-compact-debug.apk`,
87,468,308 bytes, mode0444, SHA256
`f98c99bc16284dcbd47bcb4e84fe357cbdd707e5191a4551cc43c9f3fc02af9e`.
Report: `build/codex-android-density/green/REPORT.md`. One report-only harness
correction, no build/test retry; compiler/SDK warnings retained. Main independently
verified APK and five production/test hashes. Original source V8 remains frozen;
the Android build continues using verified prebuilt V8. Build score95/100.
The fresh GUI verifier received explicit current-candidate install/exclusive
authorization after GREEN. Candidatef98c99bc is installed on the API35 phone.
Initial main-reviewed portrait screenshots show readable enlarged controls,
horizontal three-link internal navigation and contained wrapping card content.
Actual effective device density is320dpi (scale2), not the model's example2.75.
Fresh60.04-second home idle passes at mean2.015% CPU, range0–6.12%, longest
above3% streak1, RSS222.75 MiB. Real History/Bookmarks taps select the expected
content; address tap opens the IME and literal `lumen://newtab` is visible before
submission. **Phone run1 fails a real visual gate after address submission:**
the entire SDL surface remains gray-tinted, while system bars remain untinted.
Main reviewed submission and a second screenshot67.22 seconds later with no
Back, touch, reload or rotation masking it. IME is hidden, own activity focused,
resumed/unlocked and unobscured. Normal GUI testing stops here; current-candidate
rotation/resume/final idle remain untested. Failure cleanup force-stops the app,
restores free rotation and releases the phone; full final UI restoration was not
accepted. Evidence: `build/codex-android-density/phone/run1/cleanup-final.json`.
Main's source-level diagnosis is the platform default focus highlight: SDL's
TEXTEDIT_HIDE returns focus to its SurfaceView, and the captured hierarchy shows
focused BrowserSurface in non-touch mode. Android15 draws a default highlight
over such views unless opted out. This matches the failure but has not yet been
confirmed by a corrected phone run. Official implementation:
https://raw.githubusercontent.com/aosp-mirror/platform_frameworks_base/android-15.0.0_r1/core/java/android/view/View.java
Final failed-run report: `build/codex-android-density/phone/REPORT.md`;
556 continuous samples through cleanup, host policy/source pins/frozen V8 all
preserved. Logs retain20 allocator errors,20 AHardwareBuffer errors and one
IME-hide timeout. Bounded partial GUI score55/100, not an overall pass.
New BrowserSurface model assertions precede the adapter opt-out. Fresh RED
compiles Java and fails the constructor focus-highlight assertion; the later
resize assertion is not reached. Report: `build/codex-android-focus/red/REPORT.md`.
Main adds `setDefaultFocusHighlightEnabled(false)` in BrowserSurface's constructor,
without changing keyboard focus, SDL listeners or touch mode. Commit `1d2075a`
passes109 unique focused groups plus23 unchanged packaging gates. All293 native
objects are reused; only classes3.dex changes. SDK36 compile and DEX evidence
verify the actual Android API call. Normal assembly takes18.611 seconds at mean
275.15% CPU, sampled peak RSS1027.88 MiB; shared480%/SCHED_IDLE/nice19 holds.
Report: `build/codex-android-focus/green/REPORT.md`; zero harness corrections,
build score95/100, frozen V8 and allseven hashes unchanged. New candidate:
`build/codex-android-focus/green/codex-android-focus-compact-debug.apk`,
87,468,308 bytes, mode0444, SHA256
`a06cbad1142051177fea1da649bb5eae9b95256039a40a382e2165595be79612`.
Main independently verifies APK/four source pins and grants the fresh GUI agent
exclusive phone testing. Its host-only preparation passes34 checks without
corrections. Corrected candidate is installed on the API35 phone. Main reviews
initial, protected post-submit and rotated-existing-IME screenshots: all pass.
After hardware Enter and more than10 seconds without touch/reload, the surface
has no gray tint, retains keyboard focus and remains in non-touch mode. This
confirms the focus-highlight correction in the original failing context.
Fresh61-second home idle averages2.443% CPU, range0–6.122%, RSS223.156 MiB,
longest above3% streak1. Closed-IME rotation and rotation of an already-visible
keyboard preserve readable responsive layout and a single keyboard inset.
**The full phone run is incomplete, not GREEN:** after same-process resume,
the sampler loses one transient thread's `/proc/1452/task/5203/stat` during
`resumed-history`. The legacy strict `cat /proc/$process/task/*/stat` collector
treats normal thread exit as failure. Main inspected the collector and failure
evidence; this is a monitoring race, not a demonstrated browser failure.
The pre-cleanup image shows normal History selection, closed keyboard and no
tint, but final navigation/20-second idle/restoration gates are not completed.
Cleanup confirms stopped/released/free rotation; full UI restoration remains
unverified. Evidence: `build/codex-android-focus/phone/run1/FAILURE.json` and
`build/codex-android-focus/phone/run1/cleanup-final.json`.
Final partial report: `build/codex-android-focus/phone/REPORT.md`, score65/100
for verification completion, not production readiness. It preserves507 samples,
a2.867-second successful-sample gap and a second expected-stop label mismatch
(`App disappeared during cleanup`); neither error is erased or called a pass.
The fresh tracked collector `tools/android/codex_proc_sample.sh` now passes9
host fixture tests. Main wrote tests before seeding the actual legacy cat
behavior: RED has6 behavioral assertion failures across4 methods, no setup
errors. The builtin-read fix passes all9 without test changes. It skips vanished
task directories, not arbitrary read failures, and checks process presence again
after collection. This is directory-lifetime detection, not numeric errno
classification; PID/start/cadence checks remain the caller's responsibility.
Reports: `build/codex-android-proc-sample/{red,green}/REPORT.md`; policy/frozen V8
checks pass, zero corrections. Scope/usage: `docs/codex-android-telemetry.md`.
A fresh GUI verifier passes49 host checks including33 test groups with this
collector and correct intentional-stop labeling. One fixture-only quoting
correction is retained; production collector and sampler command are unchanged.
Main rehashes APK/collector/tests, reviews preparation and grants exclusive phone
access for the full current-candidate protocol. No new APK build is necessary;
the installed production bytes do not change. **The fresh run stops at preflight**:
`App already running; no takeover`. It performs no install, launch, sampler or
GUI action. The old guard merges pidof stdout/stderr and failed to save the raw
response, so a live PID versus an ADB diagnostic cannot be established from this
evidence. The controller exits1 and releases host locks; it does not force-stop
or otherwise alter any externally running app. Current phone stopped/released
state is unverified, distinct from historical successful cleanup. Report:
`build/codex-android-proc-sample/phone/PREFLIGHT_REPORT.json`.
Main asks whether Lumen may be closed for a fresh phone test; no reply yet.
Next verifier must capture separate pidof returncode/stdout/stderr before
classifying device state, preserve this run, and obtain a fresh exclusive grant.
The new collector is host-verified only; live Android integration remains untested.
Historical helpers and failed run remain intact. No further phone access
is authorized to the completed verifier. Overall daily-use readiness remains25/100;
public sites, Chrome memory comparison and major graphics/media APIs are not
validated by these internal-page checks.
The preceding density host-only preparation passed38 checks after two corrections;
an initial policy5/nice-4 failure remains unexplained, not claimed fixed. Strict
runtime priority checks remain required; no global host policy was changed.

### Previous viewport checkpoint

**Viewport-style fix committed and verified on the API35 phone.**
Eight new actual-CSS-engine regression scenarios compile and fail behaviorally
before production changes: viewport units, media activation, nested queries,
inheritance/pseudo styles, stable viewport caching, independent documents,
resolution/color scheme and orientation. RED evidence:
`build/codex-android-viewport/red/REPORT.md` (8 failures, no test errors).
Two resource/late-sheet cases and three app-wiring assertions were added before
implementation but were not part of that executed RED run.
Commit `ae9976d` retains `@media` rule conditions and evaluates them once per
style environment change or index rebuild. Computed styles invalidate on
viewport/DPR/color-scheme changes; unchanged clean documents avoid restyling
and stylesheets are not reparsed on resize. Native page render, synchronous
style reads and iframe layout update their style viewport before layout.
Conditional font-face/import/property resource behavior remains parse-time;
this patch does not claim complete conditional-resource support, JavaScript
resize/matchMedia notifications, density-aware UI or public-site parity.
GREEN passes93 focused groups (new13 plus prior80) and23 unchanged packaging
gates. Actual Android header rebuild:17 rebuilt, five byte-changed,276 reused.
Compact candidate87,468,308 bytes, mode0444:
`build/codex-android-viewport/green/codex-android-viewport-compact-debug.apk`,
SHA256 `7fd5aa1b9f2079500ea82888408b0dcb8be31aef7499613208d223bc04d7b6a2`.
Report/source pins: `build/codex-android-viewport/green/REPORT.md`.
Normal assembly averaged234.87% host CPU, sampled peak RSS1737.07 MiB;
shared480% quota/SCHED_IDLE/nice19 held and original V8 remains frozen with
seven unchanged input hashes. Two build-harness corrections, no production
failure/retry; compiler/SDK warnings are retained. Verified prebuilt V8 is used.
Phone testing completed and released the device. Host-only preparation recorded one unexplained
priority-check failure before models ran; strict subsequent checks passed,
but no cause/fix is claimed. The actual runtime's1328 host-policy samples and
4435 thread observations all retain shared480%/SCHED_IDLE/nice19.

New candidate7fd5aa1b is installed. Evidence under
`build/codex-android-viewport/phone/run1` verifies the previously stale white
navigation column now fills the page after IME dismissal and closed-keyboard
portrait/landscape rotations, without page touches/reloads masking the defect.
Rotation with an already-visible IME also retains correct surface/column
geometry and one keyboard inset. Main reviewed actual protected dismissal and
already-open-IME rotation screenshots. One real Home/background/resume retains
the same process; fresh History/Bookmarks touches verify presentation.
Fresh HUD-On baseline60.01-second idle passes: mean2.45% CPU, range0–8%, max
three consecutive intervals above3%, RSS223.22–225.48 MiB. Final20-second home
idle passes: mean2.40%, range0–6.12%, max one consecutive interval above3%,
RSS233.14 MiB. Continuous process/all-thread telemetry covers330 samples over
329.02 seconds including review/restoration/cleanup; max host gap1.138 seconds.
Logs retain60 GraphicBufferAllocator and60 AHardwareBuffer errors, undiagnosed;
no matched fatal pattern does not mean error-free graphics. No site/Chrome
benchmark, public-site parity, media-query phone fixture or complete web-API
acceptance is claimed. Conditional media behavior is covered by actual-engine
host tests; phone visuals establish this home-page viewport regression only.
Phone restored to Bookmarks/HUD On, portrait, keyboard closed, rotation `free`,
then force-stopped and released; source pins and frozen V8 remain unchanged.
Final phone report: `build/codex-android-viewport/phone/REPORT.md`.
Main-reviewed scores: build/packaging95/100, bounded viewport GUI90/100;
overall Android daily-use readiness remains25/100, not a compatibility percentage.
Phone harness corrections: one host-preparation diagnostic, zero runtime;
the unknown preparation priority failure remains unresolved.
Next: density-aware mobile controls and responsive internal pages, plus native
JavaScript resize/matchMedia notification coverage. Missing real graphics and
device web APIs and unmeasured Chrome comparisons still block daily readiness.

### Previous safe-area checkpoint

**Android safe-area adapter committed and verified in bounded API35 phone tests.**
`BrowserInsets` pads SDL's shared surface/input parent for system bars, display
cutouts and the visible IME. It subtracts handled geometry from child insets
while preserving IME visibility, avoids cumulative padding, and explicitly
uses edge-to-edge layout on API30+ to prevent duplicate framework resizing.
API28/29 retain guarded legacy handling. Native engine/density/CSS viewport
invalidation are unchanged; model tests do not prove real keyboard behavior.
Tests-first RED was missing-class setup and Activity wiring, not seven executed
behavior failures. Two more legacy scenarios were added before implementation.
GREEN passes nine Java model scenarios plus wiring and the prior70 groups:
80 focused groups and23 packaging gates. SDK36 compilation and APK DEX are
verified, all293 native objects reused, and only classes3.dex changed.
Candidate: `build/codex-android-insets/green/codex-android-insets-compact-debug.apk`,
87,468,308 bytes, mode0444, SHA256
`13c702ca7047f7a0be357cab1ce978d992a132a7d770b3f4a79c4ad76ee177fb`.
Report/source pins: `build/codex-android-insets/green/REPORT.md`.
Normal assembly averaged258.03% host CPU, sampled peak RSS923.60 MiB;
shared480% CPU quota, SCHED_IDLE/nice19 held. Original V8 remains frozen;
verified prebuilt V8 is still used.
Phone run2 verifies the toolbar below the status bar, actual visible keyboard,
and typed `lumen://newtab` in the focused address. Main reviewed both screenshots.
Home idle passes60.01 seconds at2.33% CPU (range0–9%, at most two consecutive
intervals above3%), RSS222.02–224.28 MiB. Evidence lives under
`build/codex-android-insets/phone/run2`. The run stopped on an overstrict harness
assertion: raw mIsInputViewShown remained true after Back, although mInputShown
was false and the surface regained its full height. Android15's public
isInputViewShown uses mDecorViewVisible, not that cached input-view flag:
https://raw.githubusercontent.com/aosp-mirror/platform_frameworks_base/android-15.0.0_r1/core/java/android/inputmethodservice/InputMethodService.java
That run has no dismissal screenshot; the fresh follow-up supplies it. No
production change was made to satisfy the flawed check. API28–34 behavior remains model/SDK-only,
including ordering against SDL's queued legacy window-style changes.
Partial runtime report: `build/codex-android-insets/phone/REPORT.md`.
Run2 retained20 allocator and20 AHardwareBuffer startup errors (not the older
baseline's32 each), significance still undiagnosed. Telemetry covers the whole
successful run at0.754–1.260-second cadence. The earlier missing-namespace
harness run has no live CPU sample and is not represented as a monitored pass.

Commit `656cd64` adds the adapter and tests. Fresh follow-up evidence under
`build/codex-android-insets/followup` verifies real address typing/submission,
reopening and Back dismissal, portrait/landscape safe chrome and keyboard
geometry, and one same-process Home/background/resume with fresh History then
Bookmarks pixels. Main reviewed actual dismissal, landscape keyboard and
resumed History screenshots. Back hides the keyboard but leaves address
editing selected; an actual page touch exits editing. The existing CSS-vh
invalidation bug also leaves the inner white navigation column too short after
keyboard dismissal until page navigation. Neither is hidden as a polished
mobile layout. Rotation while the keyboard is already visible was not tested.
Post-resume home idle passes20.99 seconds, mean2.24% CPU, range0–6.06%, at most
one consecutive interval above3%, RSS233.45 MiB. Prior60-second home acceptance
was not repeated. The follow-up retains56 allocator and56 AHardwareBuffer
errors throughout its captured log; their significance remains undiagnosed.
Final restoration confirms Bookmarks, HUD On, portrait, keyboard closed and
rotation `free`, then force-stop and phone release. Installed APK is13c702ca,
not the historical f313d632 candidate below. No public-site/Chrome comparison,
WebGL/WebGPU, real camera/screen capture or daily-use readiness pass is implied.
Report: `build/codex-android-insets/followup/REPORT.md`. Its320 samples cover
319.02 seconds, maximum host cadence gap1.118 seconds, through cleanup. Two
test-harness corrections (visibility criterion and child-stdin isolation), no
production workaround or retry of a failed CPU gate. Main review scores:
build/packaging95/100, initial partial GUI coverage60/100, narrowed follow-up
90/100, Android safe-area milestone90/100. Overall daily-use remains25/100:
mobile density, viewport-style invalidation and missing real web APIs still
block the requested browser. Next: test-first viewport-dependent style
invalidation for Android resize/IME changes, then density-aware mobile controls.

### Previous verified phone baseline

**GPU lifecycle fix verified in bounded phone tests; IME and graphics APIs block daily use.**
Phone candidate SHA256 `f313d632...` passes HUD-On home idle over61 complete
intervals: mean2.38% CPU, range0–6.25%, RSS232.82 MiB, no sustained excess.
Three real Home/background/resume cycles retain the same process and visibly
repaint History/Bookmarks. SDL surface destroy/create logs pass without GPU
errors. Their20-second settled means are2.15%,2.65%,2.30%, RSS233.34–235.82 MiB.
Evidence: `build/codex-android-gpu-lifecycle/phone/narrow1` and
`build/codex-android-gpu-lifecycle/phone/narrow2-resume`.
The first harness stopped on a90-second agent visual-review timeout, not a
browser crash. Fresh verification completes rotations3/2/0 and a9.01-second
background rotation/resume, retaining process identity and fresh presentation.
Their20-second settled CPU means are2.20%,2.50%,2.35%,2.20%; resume RSS is
237.27 MiB. Reports: `build/codex-android-gpu-lifecycle/phone/REPORT.md` and
`build/codex-android-gpu-lifecycle/rotation-followup/REPORT.md`. Continuous1Hz
monitoring covered all runtime. Previous controller cleanup preceded the fresh
launch; no device overlap. Both controllers are finished and released the phone.
Natural fixed HUD numbers are not stale-frame evidence: actual page touches
now establish presentation freshness. These checks are not raster equivalence.

IME activation FAILED: address tap[450,22] followed by mInputShown=false and
mIsInputViewShown=false. Address focus itself is unverified; dismissal/reopen
were not attempted. Tiny controls and status-bar overlap persist. The inner
white navigation column also retains landscape height after portrait resize
until an interaction repaints it. Next main-agent work: test-first Android
safe-area/input/density integration and viewport-dependent style invalidation;
do not simply change tap coordinates or call the keyboard working.
Raw startup logs contain32 GraphicBufferAllocator4x4 format56/59 allocation
failures and32 corresponding AHardwareBuffer errors. Their significance is
undiagnosed; the inherited crash regex missed these. Do not claim error-free
GPU logs merely because no abandoned-window crash recurred.

The synchronous SDL
background/foreground watch detaches the presentation surface before SDL's
pause wait and recreates it from the current Android native window on resume.
It retains the GPU device, pipelines and cached textures, owns a native-window
reference, rejects incompatible replacement formats and suppresses rendering
while suspended. The prior SIGABRT does not recur in the three tested cycles;
this is not exhaustive device/lifecycle stress coverage. Commit `11e90a6`.

Tests were written first: missing APIs/callbacks produced compiler/assertion
RED, not executed behavioral-model failures. All eight actual-gpu.c mocked
lifecycle scenarios and two callback/wiring groups now pass with UBSan.
GREEN totals70 focused groups and23 packaging gates. Only two native objects
changed,291 reused. Fresh packaging-only state avoids incremental ZIP dead
space; compact candidate is87,468,308 bytes, mode0444:
`build/codex-android-gpu-lifecycle/green/codex-android-gpu-lifecycle-compact-debug.apk`,
SHA256 `f313d6320239574c4322f6382eaf25932c69841fac68054f4dd697455ab7fcb1`.
Build/report/source pins: `build/codex-android-gpu-lifecycle/green/REPORT.md`.
Two packaging-harness corrections, no production fixes by the build agent.
Shared480%/SCHED_IDLE/nice19 held; original V8 remains frozen and unchanged.
The exact owned loopback fixture now executes on the phone:7/11 checks PASS
(DOM, Intl, BigInt, Promise, Wasm, storage, fetch). Actual failures: Canvas2D
pixel mismatch, unavailable WebGL1/2 contexts and unavailable WebGPU. Main
visually reviewed all eleven results and the matching server report. Source
getImageData returns blank pixels, getContext supports only2D, and no
navigator.gpu implementation was found. The Vulkan presenter is not a working
WebGL/WebGPU web API. Camera/mic/screen API presence is not functional proof;
current getUserMedia constructs fake tracks and getDisplayMedia rejects capture.
Geolocation API is absent in this fixture. No public sites or Chrome RAM
comparison ran. Overall daily-use assessment remains25/100, not95/100.

Fixture HUD-On idle PASS:60.05 seconds, mean2.48% CPU, range0–13.27%, at most
one consecutive interval above3%, RSS241.15–245.63 MiB; Activity0%, engine2.21%.
Report: `build/codex-android-engine-smoke/phone/RUNTIME_REPORT.md`.
Independent fixture_no_keyboard scope explicitly preserved the failed IME
result rather than requiring a fake GUI pass. Phone is released, app stopped,
rotation `free`, HUD On; installed APK remains the compact f313d632 artifact.
Only the owned reverse/server were removed. Home restoration did not complete:
Back left the activity; the agent did not relaunch. No full feature pass claimed.

**Previous combined-fix APK passed the bounded home idle gate:**
60 complete seconds with HUD On, mean2.47% CPU, range0–6.93%, at most one
consecutive interval above3%, RSS229.98–232.11 MiB. Activity0.00%, engine2.22%.
At that earlier checkpoint the phone was released, app stopped, HUD On and
rotation `free`; installed SHA256 was the compact `43b721...` artifact below. Main visually
reviewed its actual home screenshot. Report:
`build/codex-android-chrome-damage/phone/REPORT.md`.
No resume retry or fixture/site/interaction acceptance ran at that checkpoint.
The then-known SIGABRT was a daily-use blocker; mobile UI remains tiny and
overlaps the status bar. Overall daily-use self-assessment remains25/100.

Pixel equivalence is **unverified**, not a measured mismatch. The attempted
diagnostic launch used SDL_ENV Intent extras, based on main's mistaken reading;
pinned SDL getManifestEnvironmentVariables reads manifest metadata instead.
No comparison or paint logs appeared, so do not claim the checker executed.
Its fprintf(stderr) output is also not verified as visible in Android logcat.
Next diagnostic work needs debug-only activation and observable logging (or
scoped own-app LLDB), not an arbitrary environment-intent bridge. Do not repeat
ignored extras or treat absent comparisons as zero mismatched pixels.

The Android surface now uses lifecycle-scoped DisplayManager notifications
instead of SDL's unused game-rate accelerometer to report display rotation.
Rotation mappings, deduplication, pause/resume, display replacement, resize,
destroy and non-accelerometer delegation have a Java model test. Its initial
RED was missing implementation (not a behavioral failure); existing phone ART
and CPU measurements motivated the change. The model, actual Android SDK
compilation, 52 total host unit groups and 23 APK packaging gates pass.
All 293 native objects are unchanged in this sensor-only APK:
`build/codex-android-idle-adapter/green/codex-android-idle-adapter-debug.apk`,
SHA256 `9be59756cc14a2b94b840e7b640024bff5f7aee463fd3b210d86f8ab5ede26ca`.
Phone verification now establishes Activity CPU at 0.00% and no sampled
sensor callbacks. HUD-On idle is still FAIL: 4.00% over 60 seconds, engine
3.80%, RSS232.26–234.34 MiB. Reversible HUD On/Off/restored gives
3.30%/0.10%/4.20%; disabling the HUD is diagnostic, not acceptance.
All four requested rotations visibly render, but background/resume FAILS:
SIGABRT after abandoned Vulkan BufferQueue errors. See
`build/codex-android-idle-adapter/phone/REPORT.md`; bounded sensor verification
80/100, not readiness. The agent restored rotation `free`, HUD On, and stopped
the app before releasing the phone. Main visually reviewed the home screenshot.

A separate native HUD regression test extracts the real damage policy and
uses UBSan/float-cast-overflow. RED: five failures, three passes; sentinel
1e30 bounds overflow integer conversion before raster assertions can run.
The fix clamps bounds before conversion and keeps page-cache validity
independent of chrome/HUD changes, while still recomposing changed chrome.
LUMEN_CHECK_DAMAGE now also compares unchanged cached pages with full raster.
All eight cases now pass, together with 60 total host groups, the native build
and 23 packaging gates under `build/codex-android-chrome-damage/green`.
These are policy-model checks, not pixel correctness or runtime acceptance.
Commits: sensor adapter `f9f7820`, native damage fix `5a50ae9`.

Main caught APK growth from 93.1 MB to 168.9 MB despite unchanged live-entry
sizes. The installed AGP incremental ZIP allocator leaves unused zero-filled
records after replacing the native library. Packaging-only fresh output and
incremental state produce identical live payloads in 87,451,924 bytes, with
no unreferenced gaps and all 23 gates passing; no native/Java compilation.
Use only the corrected delivery artifact:
`build/codex-android-chrome-damage/size-correction/codex-android-chrome-damage-compact-debug.apk`,
SHA256 `43b72133e4628a23b5337a4da9c3dd631665df3018594d88c5842c269742fd2f`.
The oversized GREEN archive is historical evidence, not endorsed for delivery.
Exact task-only Gradle command/init script and diagnosis are in the correction
directory's REPORT.md. Future native changes may fragment incremental APKs
again; compare container size with live payloads before delivery.
Fresh phone testing of the combined fixes was limited to HUD-On idle and
an ineffective cached-page/full-raster diagnostic attempt. Resume failure must be fixed
before interaction/site/fixture acceptance; no daily-readiness pass is claimed.

Historical lifecycle investigation (implemented and tested above): the presenter captured ANativeWindow once in
gpu_create. SDL replaces its Android window property on native surfaceCreated,
and releases the old window on surfaceDestroyed. Lumen handles neither
background/foreground presentation resources nor native-window rebinding;
gpu_present_frame instead reconfigures the old WGPUSurface on acquisition
failure. This strongly supports a stale presentation-surface hypothesis, not
a symbolized abort stack. Pinned SDL Android_OnPause blocks event pumping after
queuing lifecycle events and explicitly requires filter/watch handling before
the block. Test lifecycle surface detach/rebind while preserving device/cache,
then real repeated background/resume, rotation and post-resume CPU. Do not
disable Vulkan or replace the existing engine to mask the crash. SDL reference:
https://wiki.libsdl.org/SDL3/README-android and SDL_GetWindowProperties.

### Earlier measured phone baseline

**Current user priority: Android only.** Linux development and additional desktop
benchmarks are deferred. An authorized MI 8 (Android API 35, arm64-v8a) is
connected. The own-engine debug APK now builds and passes all 23 packaging
gates after fixing SDL's missing libc++_shared.so packaging. The original launch
crash from a hidden font JNI entry point is fixed: cycle2 visibly renders the
home page on the phone. Its idle gate fails at 101.20% CPU (one core = 100%)
and 234.19 MiB RSS over five complete idle intervals. SDLThread consumes
98–100%. Non-root LLDB snapshots locate that thread in SDL_WaitEventTimeoutNS,
both its event-read and event-pump paths. See
`build/codex-android-idle-profile/cycle1/symbolized-stacks.txt`.
The pinned SDL Android wait loop self-wakes by inserting a poll sentinel every
iteration. A generated-source fix retains normal poll sentinels but does not
insert them inside the blocking loop. Its eight behavioral model cases and
four source-generation checks pass after four behavioral RED failures. The
39 existing APK/prebuilt/build-limit unit checks also pass, and the corrected
APK builds incrementally in 23 seconds. All 23 packaging checks pass. The
cycle3 phone run drops idle CPU to 9.40% (5.05–14.14%), RSS232.17 MiB, but
still fails the 0–3% gate after five complete intervals. SDLActivity uses
4–5% and SDLThread intermittently 0–10%; residual profiling is required.
Do not call this an idle pass. The installed APK is
`build/codex-android-sdl-wait/green/codex-android-idle-wait-debug.apk`, SHA256
`1fa9a030fe1a7e85febf46b19ec0c58d9c8970f4c170c5d81205902eabaf667c`.
See `build/codex-android-sdl-wait/green/REPORT.md` and
`build/codex-android-browser-phone/cycle3/REPORT.md`.
Residual diagnostic (`build/codex-android-idle-profile/cycle2/REPORT.md`):
real-GUI HUD on/off/restored yields 8.56% / 5.58% / 8.55% total CPU; with HUD
off the native engine samples 0% while SDLActivity averages 4.80%. Main
visually confirmed HUD removal/restoration. ART sampling captures sensor
dispatch, SDLSurface.onSensorChanged, Display.getRotation and onNativeAccel;
app-scoped sensor history confirms successful 20 ms requests, not measured
delivery frequency. Next fixes to test: avoid full-page repaint for periodic
HUD updates, and replace unused game-rate accelerometer rotation polling with
event-driven display rotation tracking while preserving lifecycle/rotation.
No device-motion/orientation web API is currently wired to that SDL sensor.
Do not simply disable the HUD or lose rotation to pass idle. Phone is free,
HUD restored and app force-stopped; no diagnostic process remains active.
The controls are desktop-sized and overlap the Android status bar. Fixture,
interaction and site checks have not run. Evidence:
`build/codex-android-browser-phone/cycle2/REPORT.md`.
No browser runtime pass is claimed. The isolated
font test APK was previously installed/tested. Android artifacts are under
`build/codex-android-*`.

**Latest host compiler budget: all new builds share 60% of all CPUs (480% on
this eight-core host).** `tools/codex_compile.py` now places both ordinary and
`--v8` commands in `codex-lumen-compile.slice`, with SCHED_IDLE/nice19 and a
100 ms quota period. At most six compiler jobs; prefer fewer for large links.
The host watcher has one additional narrowly scoped exemption for that slice
at the exact 480% quota. Its prior backup is
`/usr/local/lib/cpulimit/cpulimit_watcher.py.codex-before-shared-sixty`.
Unit/mock checks pass 8+15. A 25-second six-worker loaded interval averaged
479.06% shared-slice CPU, peaked at 116.15 MiB summed RSS, and recorded all
182 thread observations as SCHED_IDLE/nice19. Membership and 480000/100000
held across all 26 saved samples. The raw observer exited 1 on an extra
post-completion controller check; its unsaved final state prevents claiming
a clean observer teardown, not the bounded live measurements. See
`build/codex-android-compile-sixty/green/LIVE_REPORT.md` for that limitation.
No unrelated quota is raised. The old V8 source scope stays frozen with its historical 320% leaf
and 200% parent; this allowance does not authorize a duplicate or unsafe resume.

**Earlier host compiler budget: V8 may use 40% of all CPUs (320% on eight
cores), with SCHED_IDLE/nice 19; other builds still share 140%.** The user
explicitly requested 40%/320% after the earlier reduction and lowest priority.
V8 uses
`python3 tools/codex_compile.py --v8 COMMAND ARGS...` and the separate
`codex-lumen-v8.slice`, with at most six compiler jobs. Other compilation uses
the wrapper without `--v8` and retains `codex-lumen-compile.slice` (140000/100000
microseconds), at most two compiler jobs per build. Do not stop either slice or
the old anchor service while any build processes remain in it. The authorised
V8 concurrency change preserves existing GN outputs; it is not a clean rebuild.
The iteration-7 scope `run-p3601367-i28741414.scope` received
`cpu.max=320000 100000`, retaining parent `560000 100000` for its monitor.
However, at 13:11:38 PDT another host configuration change set both Lumen
parent slices to `200000 100000`; the CPU watcher moved the build into
`/cpulimit.slice/cpulimit-70.slice` and its safety monitor stopped Ninja and
six workers. The old scope disappeared. The global cpulimit parent is also
200%. Ninja PID 3601367 was stopped at 826/2592 steps; that attempt did not
pass its 320% live acceptance gate. Do not launch a duplicate build.
The original monitor's guard is latched, so merely sending SIGCONT is unsafe.
Evidence: `build/codex-android-v8-forty-percent/REPORT.md` (13 mocked checks
pass; sustained live verification failed). Do not overwrite unrelated host settings.

Recovery now preserves the stricter shared host limit rather than raising it.
The existing seven stopped processes were moved to the system scope
`/cpulimit.slice/codex-lumen-v8-recovery.scope` with its own `320000 100000`
ceiling, beneath the unchanged shared `200000 100000` parent. An initial scope
below `cpulimit-70.slice` failed safely with EUCLEAN (threaded parent); no build
process resumed during that attempt. The host watcher now preserves only the
exact new scope with the exact 320% quota; other mappings remain unchanged.
Original backup: `/usr/local/lib/cpulimit/cpulimit_watcher.py.codex-before-nested-recovery`.
New guard: `tools/android/build_guard.py`, documented in
`docs/codex-android-build-guard.md`, run by user service
`codex-lumen-v8-recovery-guard.service` with Restart=no. It verifies process
identities, per-thread idle/nice19, quotas, memory, concurrency and input hashes,
and stops/freezes its own build on failure. The original monitor remains alive
to reap Ninja and report the actual exit code; its latched alert is historical.
After guard readiness and stopped placement checks, the same Ninja and six
workers were resumed via identity-pinned pidfds, without restarting compilation.
Configuration, placement, resume and per-second CPU/RSS evidence are in
`build/codex-android-v8-recovery-host/`. Guard/watcher unit and simulated
fault tests pass 30/30 in `build/codex-android-v8-recovery-test/green-cycle4-q36oL3T3/`.
Fresh independent live verification passes 10/10 recovery gates over 91.074
seconds (92 samples): progress 831 to 837/2592, weighted mean CPU 138.45%,
interval peak 159.65%, peak summed RSS 4.086 GiB, minimum available 20.073 GiB.
Five priority scans preserve all identities, hashes, quotas and idle/nice19.
Evidence: `build/codex-android-v8-recovery-live/REPORT.md`. Main bounded recovery
score: 95/100; this is not a browser-readiness score. Do not claim the full
archive, native link or browser APK has passed. The current ceiling is 200% shared,
not guaranteed 320% throughput. Do not stop either monitor or launch another V8.
**Subsequent safety stop:** at 13:54:34 PDT the new guard observed seven compiler
processes and exited 2 after freezing its scope and sending identity-checked
SIGSTOP. At 14:03 PDT the scope reports `frozen 1`; Ninja remains the same
process at 891/2592 steps. There are six direct compiler jobs, but the Clang
driver PID 3942053 spawned `clang -cc1` PID 3942055, so the guard double-counted
one job. Both wait in `do_freezer_trap`. Fix compiler ancestry counting before
resuming. `guard-latest.json` is now stale, not live progress evidence. Preserve
this frozen fallback while evaluating the user-requested prebuilt alternative;
do not automatically thaw it or restart V8. Prebuilt investigation:
`docs/codex-android-v8-prebuilt.md` and `build/codex-android-v8-prebuilt-probe/`.
A V8 15.0.245.2 Android archive now links the unchanged smoke object after
building only its exact Temporal Rust support library (66 seconds). Candidate
C++ syntax and the original 15.6 consumer object both pass after the checked
Module::Evaluate conversion in src/js/js.cc. All 45 Rust registry versions and
checksums match upstream. However, the first phone run failed before main:
TLS alignment 8 instead of 64. Main traced this to linking a native-ELF-TLS
archive with API28 startup objects. Android ELF TLS requires API29; API29's
NDK crtbegin has the required alignment. The isolated API29 relink now passes
three actual phone runs: initialization, ICU date/number, Wasm, BigInt,
microtasks and teardown. TLS64, 16 KiB LOAD alignment and API29 dependency
closure pass. Runs took 0.097–0.106s, CPU 91.38–94.43% of one core, peak RSS
24.20–24.96 MiB (active smoke, not idle/browser metrics). Report:
`build/codex-android-v8-prebuilt-probe/phone-api29/REPORT.md`.
Bounded prebuilt-smoke milestone 95/100; daily browser still 20/100.
An opt-in prebuilt integration is now in the worktree: `AndroidV8Prebuilt.cmake`,
the SDK/Clang checksum manifest and verifier, ICU77.1 headers, explicit ABI
definitions/absolute vtables/Temporal, and Gradle prebuilt/compiler properties
with minSdk29. The source route retains API28. The prepared SDK lives at
`build/codex-android-v8-prebuilt-sdk`; archive/header pins are verified.
SDK/APK/compile-limit units pass 37/37. Gradle RED was confirmed before its
implementation; native CMake's first real configure found that NDK legacy mode
sets CMAKE_SYSTEM_VERSION=1. Main fixed API detection to use normalized
ANDROID_PLATFORM_LEVEL. The maintained CMake smoke now links and the actual
Gradle API29 prebuilt configuration passes. Cycle3 compiled the root objects
but exposed missing SDL C++ runtime linkage; the private NDK STL fix passes
cycle4, without leaking NDK headers into the custom-STL engine. Main's final
link then exposed omitted BZip2 and MediaCodec dependencies. Android-only
bz2/mediandk linkage fixes that real RED result. Cycle5 now passes the complete
native link, 37 units and 24 static checks: all 989 strong imports across the
four native libraries resolve against their API29 providers, and custom C++
runtime isolation passes. The link reused 289 objects; mean CPU97.74%, peak
131.40%, sampled peak RSS1015.37 MiB. Report:
`build/codex-android-prebuilt-integration/cycle5/REPORT.md`.
Main's bounded native-link milestone score is 95/100, not browser readiness.
Gradle's Ninja compile6/link1 pools and LLDthreads2 were added after an actual
configuration RED. Their real Gradle configuration and generated Ninja graph
now pass. The first actual debug APK assembles in 35.58s (mean CPU352.30%,
sampled peak RSS1460.60 MiB), but APK validation caught missing libc++_shared.so.
AGP records that runtime on SDL3-shared, not on main. The prebuilt Gradle route
now explicitly selects both shared targets, without exposing NDK STL to V8.
Cycle2 passes all 23 packaged-APK checks, including 1,093 strong imports,
API29 provider versions, debug signature, manifest and 16 KiB ZIP/ELF alignment.
The shared-library TLS layout is checked separately from executable TLS;
reproducing stripping matches every packaged library byte-for-byte.
Assembly took 20s, mean CPU256.37%, peak sampled RSS651.73 MiB. Report and APK:
`build/codex-android-apk-prebuilt/cycle2/REPORT.md` and
`build/codex-android-apk-prebuilt/cycle2/codex-android-own-engine-debug.apk`.
APK SHA256 `54d317853e63527641a051e0a50c2708d74af42e28366236f1ce37f2fab2a514`,
93,103,068 bytes. Main build/packaging milestone score: 95/100.
First phone run in `build/codex-android-browser-phone/cycle1/REPORT.md` installs
successfully, then crashes with UnsatisfiedLinkError for
AndroidFontRaster.nativeInitialize. Its symbol is LOCAL inside libmain because
the engine archive is hidden by --exclude-libs,ALL. The app remains installed
and force-stopped. Nine 1Hz samples were armed throughout, but the subsecond
process fell between samples; CPU/RSS/PSS are unavailable, not zero. No browser
screenshot, idle pass or feature fixture run occurred; no accounts were used.
Main now compiles the existing Android font bridge directly into main rather
than the hidden engine archive, retaining custom C++ runtime isolation.
APK verification now requires all four SDL/JNI entry-point exports. Its two
new regression groups first failed as intended (16 existing groups passed).
The JNI fix passes 39 units, rejects the old APK's missing font entry point and
builds a new APK that passes the maintained native verifier. All 291 existing
objects were reused; only the bridge's new main-target object was compiled.
Build mean CPU198.40%, peak sampled RSS1442.03 MiB, all 2705 thread observations
idle/nice19. New APK SHA256 is
`0e70d056106b52d8a75e4e0e11d54ce156ff691061477c273afc98bd8023cba5`.
Expanded packaging stopped twice on host niceness violations (SCHED_IDLE and
480% remained intact): first apksigner Java, then its retry's bash preflight
had nice -4. The host's ananicy bash rule uses Doc-View/nice-4, consistent with
the observations; no syscall trace establishes the actor. No host rules were
changed. The failed verification scope/controller were identity-safely
terminated without thawing. One shell-free packaging retry (the same SDK
apksigner JAR directly under Java with identical verification flags) passes
23/23 packaging checks and explicit GLOBAL definitions of all four entry points.
All 359 thread observations pass idle/nice19 and shared480%; mean CPU123.88%,
peak sampled RSS401.98 MiB. Report and validated APK are in
`build/codex-android-jni-entrypoints/green/retry2/`.
Original V8 stays frozen and seven hashes remain unchanged. Main's bounded
JNI/build milestone score is 95/100; browser readiness is still unproven.
Fresh phone verifier `01a13230-500d-78d2-9b88-b11bb28dba9b` is running the
corrected APK in `build/codex-android-browser-phone/cycle2/`.
Do not duplicate its device operations or claim startup before its result.
Its predecessor's monitor incorrectly included the unrelated nice0 anchor
sleep; the fresh verifier must inspect only owned scope descendants for
thread priority/RSS, while using the shared slice for aggregate CPU quota.
No device-browser startup pass is claimed yet.
Do not fake API28 compatibility or silently swap archives. Keep the source
fallback frozen. `tests/android/browser-smoke.html` is the offline DOM/Intl/Wasm/
Canvas/WebGL/WebGPU/storage/fetch fixture for the next actual browser runtime
gate; only its syntax has been checked so far.
The temporary normal-priority host exemption was reverted; the original
`dev-workload-priority.service` again enforces idle scheduling. Do not restore
normal priority without a new user request.

Android link-opening now has cold/warm HTTP(S) intent routing, a bounded native
FIFO and regression tests; actual APK/activity lifecycle testing remains pending.
Android Back now has API 33+ callback and legacy/hardware routing, a bounded
engine-thread queue, popup/address/focus/history/root-task behavior and
focus-transition IME activation (no automatic reopening after dismissal).
Test-first native policy/concurrency, ASan/UBSan and ThreadSanitizer tests pass;
100 threaded stress runs transfer 400,000 requests with exact wake counts.
The registered CTest passes; actual changed
Android app/JNI objects and SDK36/SDL Java sources compile. Verification report:
`build/codex-android-back-test/REPORT.md`. Gesture/IME/history/lifecycle behavior
still needs device testing; there is no predictive page-preview animation.
Android fonts now use the API 29+ platform matcher with copied file/index/axis
metadata, CSS family fallback and variable-weight handling. The isolated
`org.lumen.codex.fonttest` APK is installed/tested on the phone; it is NOT the
browser APK. The shared Android Canvas/JNI color-raster adapter repaired the
COLR system-font raster failures: 25 grouped phone checks and actual API28
object/SDK36 Java compilation pass. Evidence:
`build/codex-android-color-font-green/REPORT.md`; font Activity settled CPU was
0–2.02%, RSS 145.26–150.73 MiB. API28 runtime, full shaping coverage and browser
startup remain unverified. The font matcher checkpoint is `191e800`.
An expanded 30-check sequence gate then exposed keycaps (29/30 passed):
`build/codex-android-emoji-sequences-red/REPORT.md`. Main added bounded Android
combining-cluster font selection to prevent splitting the ASCII base from the
combining keycap, six further regression groups and an actual engine-pixel
preview chart. These changes form the Android color-font checkpoint, not a
browser APK or comprehensive standards certification.
All 36 groups now pass and the clipping counter is zero; root API28 objects
and real SDK36/SDL Java also compile. Evidence is in
`build/codex-android-font-visual-green/`. Main visually reviewed the actual
engine PNG: sizes, colors, scripts and combined emoji look correct. A subsequent
fresh test-Activity system-bar-insets run in
`build/codex-android-font-insets-green/` still passes all 36 groups; main
reviewed the new screenshot (header no longer overlaps) and confirmed the
native PNG is byte-identical to the earlier verified chart. Browser UI is
untested. The expanded preview run sampled 0–2% settled CPU and
151.14–168.95 MiB RSS; these are test-Activity, not browser/Chrome figures.
The final inset-fixed run measured 0–2% settled CPU (mean 0.20%) and
161.34–161.47 MiB settled RSS; startup/preview peaked at 103.92% CPU. Main
review score: 96/100 for this bounded API35 font milestone; daily-browser
readiness remains 20/100, with V8 linking/startup and site/API tests outstanding.
V8's native GN tool is built and Android generation passes. Actual compilation
required V8's checksum-pinned Clang24 and custom libc++ (NDK libc++19 lacks
std::atomic_ref). The active build uses generated v8-gn.h; CMake consumers still
need final archive-link verification. The Android CMake adapter now selects
matching Clang24/__Cr STL/relative-vtable ABI and imports the monolith's ICU;
Gradle forwards the required `lumenV8Dir` and optional `lumenWgpuDir` inputs.
See docs/codex-android-build.md. All 23 foundation packages have built; the
real arm64 prefix is available at `build/codex-android-deps/prefix/arm64-v8a`.
tests/android/v8_smoke.cc is the new dependency startup/ICU/Wasm runtime gate;
it must pass on Android before stronger integration claims.

Verified integration checkpoint: the actual root Android CMake project compiles
all 38 engine/app objects, including JNI, against real target OpenSSL/JPEG
headers; 155 graph/header/ABI checks pass. All 11 Gradle configuration cases
pass, including actual AGP-to-CMake flag forwarding. Reports:
`build/codex-android-engine-cmake-test/cycle1/REPORT.md` and
`build/codex-android-gradle-v8-test/REPORT.md`. These are object/configuration
gates, not a successful final native link, APK or device run. The real SDL3
shared library and engine/base/net archives now also build; all 44 artifact
checks and 67 SDL Java/native signature matches pass. Report:
`build/codex-android-sdl-engine-test/REPORT.md`. SDL requires the matching NDK
`libc++_shared.so` in the APK. Final `libmain.so` linking awaits the real V8
archive. `tools/android/verify_apk.py` checks actual packaged native dependency
closure and ELF layout when an APK exists, not signing or runtime behavior.

V8 iteration 6 stopped at 263/2855 resumed steps: the pinned `gc-info.cc`
checked its GC mode before including generated configuration. The minimal
maintained fix is `android/native-deps/patches/v8-15.6.75.8-gc-info-guard.patch`.
Both real host and Android objects now compile; all 19 mode/symbol/input/patch
checks pass (`build/codex-android-v8-gcinfo-test/REPORT.md`). No GC setting, GN
argument, generated configuration or engine behavior was changed.

The incremental six-worker build is now iteration 7, initially 2592 pending
steps, not a clean rebuild. At handoff its Ninja PID is 3601367 and monitor
service is `codex-lumen-v8-iteration7.service` (`Restart=no`). Current log:
`build/codex-android-v8/android-v8-monolith-iteration7.log`; read live process
state and fresh `build/codex-android-v8-recovery-host/guard-latest.json` before
reporting progress (the original monitor's guard/membership is historical).
The persistent monitor owns the child and records its eventual exit status;
it samples every second and retains memory guards without automatic restart.

Current branch is `codex/android-phone-engine-port`, after shared-engine
checkpoint `8cceb05` on `codex/android-linux-daily-browser` and original clean-tree
checkpoint `c742180`. Read `docs/codex-android-build.md` and
`docs/codex-android-linux-status.md` before continuing. Native core
tests and directory rendering now run on this Linux host; full Lumen and jsrun
now build with pinned V8/wgpu dependencies (see
`docs/codex-linux-native-dependencies.md`). Bounded offline GUI checks passed
discrete zoom, media controls and workspace switching with the default renderer.
CPU fallback hung after starting audio; 10% video zoom hides native controls.
Android is a same-engine
SDLActivity/CMake adapter, not a built or tested APK. An incomplete offline Chrome
comparison is recorded in `build/codex-local-chrome-benchmark/REPORT.md`; it is not
an Android or top-site benchmark. No personal-account tests have been performed.
Keep the goal open and do not treat older macOS results below as Linux/Android
verification. Continue test-first with fresh build/test-only agents and one-second
CPU/RSS monitoring. Main agent owns implementation and debugging.

## Current goal (user, 2026-10-08)
Lower RAM/CPU/GPU/load time than Chrome on: YouTube home, 2 YouTube videos playing,
bing.com, Google search with AI Overview — all loaded and active. Compare with
off-screen media eviction setting off and on (on: target < 150 MB for YouTube playing).

## Done
- RAM: decoder threads capped (4), CSS custom props share parent chain, script sources
  freed, VQ_MAX 3, selector vectors trimmed. Single YouTube ~300 MB (was 396).
- VideoToolbox hardware decode (`LUMEN_HWDEC=0` disables), software fallback.
- Real tabs in one process (`src/app/main.c`: `Tab`, `tab_new/select/open/close`,
  tab strip in chrome, Cmd+T/W/1-9, Ctrl+Tab; extra argv URLs open as tabs).
  Background tabs keep JS ticking and media playing.

## Next
- Off-screen media eviction: setting plumbing done (`g_lowmem` in main.c, `offscreen_media_eviction=1` in
  SDL pref dir settings.txt, `LUMEN_LOWMEM=1` env). Still to do: evict decoded images not painted for a few
  seconds (ImgSlot in main.c; img_size layout hook also calls node_img, so mark use from paint only), skip
  BGRA conversion for videos in background tabs/off screen (media.c emit_video). V8 MemoryPressureNotification
  + malloc_zone_pressure_relief did not help (tested, removed).
- GPU %: read from Activity Monitor % GPU column (CLI has none in this VM).
- CPU: profile shows swscale NV12->BGRA + memmove; consider GPU NV12 upload.
- Rerun 5-page compare: `~/sites/lumen-tabs.sh`, `~/sites/chrome-tabs.mjs`, `~/sites/cpusum.sh`.

## ChatGPT / Cloudflare Turnstile (2026-10-09)
Goal: chatgpt.com usable in Lumen incl. login. Fixed so far (each has a tests/js fixture):
- WHATWG streams: pipeTo/pipeThrough/tee/from, WritableStream, TransformStream, Text{En,De}coderStream
  (Cloudflare's chl_page rejected Lumen as "Browser not supported" without pipeTo). `streams.html`
- Iframes inside shadow roots never loaded (frames_scan walked light tree only; shadow trees were never
  NF_CONNECTED). Turnstile mounts its iframe in a closed shadow root. `shadow-iframe.html`
- postMessage / MessagePort / worker `message` events had isTrusted=false; Turnstile and its blob worker drop
  untrusted messages. `frame-msg-identity.html`, `worker-trusted.html`
- Worker fetch: headers, binary bodies, response headers. `worker-fetch.html`
Status: challenge now runs end to end (worker probes, PoW, timing) and ends in `interactiveBegin` (the
"Verify you are human" checkbox). That needs a human click in the GUI; agents must not click it.
`brunhild.challenges.cloudflare.com` does not resolve from the Devin VM (curl fails too), so one worker
probe always reports "Failed to fetch" here.
Debug: `LUMEN_FRAME_PRE='js'` evals a script in every non-about: iframe context before its scripts (pair with
`JSRUN_PRE`, `LUMEN_CONSOLE=1`). Don't wrap Worker/eval/Function with Proxies; it breaks the challenge.

## Chrome 154 baseline (5 tabs)
RAM 1,307-1,314 MB, peak sum ~1,850 MB, CPU 10-14%, GPU proc ~94 MB, video 854x480.
Load (DCL/load): Google 0.53/3.66 s, Bing 1.97/2.23 s, YT watch ~2.2/3.47 s, YT home 2.29/2.33 s.

## UI (2026-10-08)
- Tabs live in the title bar right of the traffic lights (`mac_style_window` in src/app/macui.m: full-size content view,
  transparent titlebar; `win_hit` makes empty tab-row space draggable).
- Workspaces: left bar `SIDEW` 68pt (right edge = 3rd traffic light), `Tab.ws`, `ws_select/ws_new/ws_close`,
  Workspace menu (`mac_install_menu`, events `EV_MENU` -> `menu_cmd`). Double-click a workspace to rename.
  `LUMEN_NO_SIDEBAR=1` hides the bar.
- Low-memory mode (`LUMEN_LOWMEM=1`): `img_evict` drops images not painted for 3 s (refetch on paint),
  `media_mark_visible` + `hidden` in media.c emit 1x1 frames for players outside the active tab.
  3-tab YouTube test: 523 -> 496 MB, images 24.8 -> 6.1 MB; JS heap (~70 MB/YouTube page) dominates.
- Tab CPU limiter: `cpu_monitor` (main.c) sums per-tab main-thread time (js_tick/restyle/render) + decoder-thread CPU
  (`media_cpu_ms`). >80% for 60 s -> `limited` (token bucket 0.4 ms/ms, `media_set_limit` sleeps decoder) + infobar
  (`g_info_h`, buttons `HB_INFO+k`: 1/4/10 h unlimit, x dismiss). `LUMEN_CPU_DEBUG=1` prints per-tab %. Test: tests/js/cpuburn.html
  (100% -> 40.6% process CPU after the limit kicks in). FFmpeg internal decoder threads and VideoToolbox are not counted.
- Workspace bar 84pt (= first tab's left edge). Click inactive workspace = switch; click active = native menu
  (`mac_ws_menu`): Rename, Change Icon (16 SF Symbols, `mac_icon_rgba` -> cached Image), Refresh All Tabs, New, Close.
- Benchmark: ~/sites/lumen-real.sh off|on URLs... (5 real tabs, `LUMEN_TAB_TOUR=6000`). Latest: off 619-640 MB / 29-37% CPU,
  on 574-596 MB / 24-28%; Chrome 1307 MB / 10-14%. Profile: biggest CPU = libswscale (BGRA conversion) -> next: VT BGRA
  output or GPU YUV upload; JS heap ~90 MB per YouTube page.
- Limited tabs show a green dot left of the tab's x (`HB_TABDOT`); hover or click (pinned 4 s, `tip_until`) shows
  `chrome_tip`. Hit codes: check `HB_TABDOT` (500) before `HB_INFO` (400) in the click handler. "+" / New Workspace
  creates "Workspace N" without a prompt (rename from the workspace menu).

## New tab page, bookmarks, settings, tab menu, video controls (latest)
- `+` opens `lumen://newtab` (built by `internal_page()` in src/app/main.c): left bar Bookmarks / History / Settings.
  Settings actions go through `apply_set()`; persisted in settings.txt (SDL pref dir) plus bookmarks.txt / history.txt.
- Toolbar: SF Symbol `arrow.clockwise` refresh, bookmark star right of it (`HB_STAR`, `bm_toggle`).
- Settings: Lite mode (`offscreen_media_eviction`, same as LUMEN_LOWMEM=1), CPU limit n%/m s/t% (`cpu_pct/cpu_secs/cpu_lim`),
  always-show video controls (`video_controls`).
- Second click on the active tab -> `mac_tab_menu()`: per-tab CPU policy (`Tab.cpumode/lim`) and per-tab `vctl` (-1 = follow global).
- Native video bar (`video_bars`/`vbar_click`): inside the video's bottom edge for `<video controls>`, below every video when
  always-show is on. Clicks send `lumenmediactl` to page JS (toggle / seek:frac). Test page: tests/media/controls.html.
- YouTube: `YT_VCTL` CSS keeps .ytp-chrome-bottom visible when always-show is on; `page_move_keep()` re-sends mousemove
  for 8 s after the pointer stops so the controls hide later.
- Layout fix: floats / atomic inlines inside inline formatting contexts now get the containing-block height
  (`Layout.inl_cbh`), so YouTube's progress list has height. Paint: axis-aligned `scale()` now applied (display.c),
  so YouTube's scaleX progress fill shows played/buffered correctly.
- Not done: video bar below a video overlaps following content (no layout space reserved); `loop` video state shows paused at end.

## Session 2026-10-08: YouTube search, Lite restore, DMG
- YouTube search works (type, Enter, search button, URL bar updates, shade closes). Fixes:
  - `document.all` via `N.makeAll` (undetectable object) — Polymer Resin sanitizer was replacing `hidden` binding with "zClosurez" because `document.all` was missing, hiding results.
  - `focus()`/`blur()` now fire bubbling `focusin`/`focusout` with `relatedTarget` (dropdown/scrim close).
  - `history.pushState` borrowed from an iframe realm delegates to the receiver (YouTube binds iframe history methods).
  - Click path fires pointerdown/pointerup; a prevented mousedown keeps focus.
  - Cmd+A selects all in page text fields.
- Lite mode: per-tab toggle in tab menu, dark green title, `IntersectionObserver` re-checks every 300 ms, images re-decoded from retained encoded bytes.
- `LUMEN_WINDOW=800x600` sets the window size.
- DMG: `python3 tools/mkdmg.py 0.1.0-preview` -> `dist/`. Signing identity "Lumen Debug Signing" lives in `~/.lumen-signing/` (keychain + p12 + passwords, NOT in repo); unlock the keychain with the password in `keychain-pass` before running, and pass `LUMEN_SIGN_ID=<sha1>`.
- Pushed to ecooxai/lumen branch devin/lumen-early-preview (PR #1) via `git push https://github.com/ecooxai/lumen HEAD:refs/heads/devin/lumen-early-preview`. Promo video tooling in tools/promo. Not done: GitHub release (needs a token), Lite-off CPU on YouTube results page (~30%).

## Video CPU work (branch devin/1791493877-youtube-ads-controls, after v0.1.1-preview)
- Profiling (LUMEN_DEBUG_PAINT=1 prints paint counts + ms/s for style/layout/dl/raster): on a YouTube watch page the
  cost was NOT decoding. YouTube updates progress-bar styles ~30x/s -> restyle -> full layout_run (~11 ms) + full raster
  every video frame (~45% CPU). VideoToolbox decode + sws_scale was small.
- Fix in progress: render() defers DOM/relayout-driven full paints to <=4/s while media plays and no user input for 1 s
  (`a->deferred`, `last_full`, `last_input`); video frames still go through the GPU punch path every frame.
- GPU YUV path (done): VideoToolbox CVPixelBuffer NV12 planes are copied into Image.yuv (no av_hwframe_transfer_data,
  no sws_scale) and uploaded as R8 + RG8 textures; WGSL shader does BT.601/709 + range conversion. Fallback: BGRA.
  Env: LUMEN_NO_YUV=1 (BGRA path), LUMEN_NO_HWDEC=1 (software decode). Local 1080p30 H.264 file (~/sites/v1080):
  hw+yuv 18.5%, hw+bgra 33%, sw 44%, Chrome 27.5% (top, 2x10 s). YouTube 1080p: 31-39% (rest is YouTube JS/layout).
- Deferred paints no longer rebuild the display list from a stale layout (restyle freed styles -> crash in cache_get);
  while deferred only the video texture is swapped using the saved rect (App.vown/vrect).
- Next: paint-only style changes (transform/opacity/color) should skip layout; true zero-copy via IOSurface->Metal texture.
  paint-only style changes (transform/opacity/color) should skip layout entirely.

## Video CPU, round 3
- Tried `thread_count = 1` for VT decode: no gain over 6x10 s windows (22-24% both ways); reverted.
  Short 10 s windows on this VM swing 12-24%, so use >= 60 s of samples before believing a change.
- `9e5dca5`: video-only frames reuse the saved video rect/owner and skip the display-list rebuild.
- VM floor (ffmpeg CLI, 1080p30, `-re`): VT decode only 6.3%, VT + transfer 6.6%, software 14.6%.
- Present costs ~0 (video in a background tab costs the same CPU); audio ~3.5%.
- YouTube 1080p still ~33%: main thread ~22% (YouTube JS ~9%, `getBoundingClientRect` -> full
  `layout_run` ~3%, render ~4%). Next: incremental layout for `n_rect`/`h_sync`, paint-only style
  invalidation, IOSurface zero-copy into Metal textures.
- `c5e83ab` paint-only restyle: recalcs that change only paint-time properties update the ComputedStyle in place
  (`paint_only`/`adopt` in src/css/style.c) and `style`/`class` attribute writes bump `dom_version` but not
  `layout_version`, so `restyle()` only sets `relayout` when needed. `LUMEN_NO_PAINT_ONLY=1` forces relayout.
  Correctness page ~/sites/po/index.html (getBoundingClientRect after style/class/text changes) passes.
  CPU: YouTube content 1080p measured 28-33% with it vs 23-28% without in back-to-back 6x10 s runs; another
  pair (one during an ad) went the other way. Layout ms/s dropped 24 -> 8 in one pair, equal in another.
  Conclusion: run-to-run noise on this VM (+-5 points) is bigger than the main-thread savings; needs a real Mac.

## ChatGPT session (2026-10-09)
- Login verified in native Lumen: email -> password -> TOTP -> logged-in chatgpt.com home.
- "hi" sent via ProseMirror composer (contenteditable support); conversation created, but the reply rendered as "This response couldn't load".
- Root cause candidate: WebSocket was a stub; ChatGPT streams replies via wss://ws.chatgpt.com. Native RFC 6455 WebSocket now in src/net/http.c (net_ws_*), JS in 07b-net.js; verified against echo server.
- Next: retest reply rendering with real WebSocket; fix remaining errors.
- Fixes this session: document.forms.namedItem (HTMLCollection), lazy console formatting, contenteditable editing, document.timeline, iframe.sandbox DOMTokenList, worker Blob/File + URL.createObjectURL, WebSocket.

## 2026-10-09 update
- UI: a workspace with one tab is "compact": toolbar + URL bar sit in the title bar, `+` new-tab button right of the URL (`g_compact`, `update_compact()` in src/app/main.c). 2+ tabs restore tab strip + URL row.
- WebSocket WPT: full `websockets/` dir via real `wpt serve` = 554/588 (94.2%). Setup: sparse clone ~/src/wpt (resources tools websockets webrtc common docs infrastructure interfaces), append tests/wpt/serve.py REPORT to resources/testharnessreport.js, `./wpt serve --no-h2 --config /tmp/wptcfg.json` with `{"check_subdomains": false, "server_host": "127.0.0.1"}`, then `WPT_URL=http://web-platform.test:8000 LUMEN_HOST_MAP=web-platform.test=127.0.0.1 tests/wpt/run.sh out/wpt-ws websockets`. Remaining: bfcache/unload, multi-globals, cookies timing, initUIEvent, NUL/windows-1252 URLs, serialized connects, basic auth.
- Workers: MessagePort transfer main<->worker works (tests/js/worker-ports.html).
- ChatGPT reply ("This response couldn't load"): rendered by DIL runner iframe https://cdn.platform.openai.com/deployments/dil/v14/runner.html (sandbox=allow-scripts). Its blob worker answers healthCheck correctly in jsrun (protocolVersion 14). Iframe gets createRunner then disposeRunner from chatgpt.com; next: capture createRunner payload + frame->parent replies (LUMEN_FRAME_PRE hooks) to find what fails.
- WebRTC: JS implementation in `src/js/prelude/07r-rtc.js` (RTCPeerConnection, SDP offer/answer, ICE candidates/states, RTCDataChannel, transceivers/senders/receivers, certificates, stats, fake `getUserMedia` tracks). Transport is in-process loopback: peers in the same Lumen process find each other by ICE ufrag (`rtcPeers`). No real UDP/DTLS/SCTP yet (needs native stack, e.g. libdatachannel from source). WPT: `WPT_URL=http://web-platform.test:8000 LUMEN_HOST_MAP=web-platform.test=127.0.0.1 tests/wpt/run.sh out/wpt-rtcN webrtc` (wpt serve on 127.0.0.1:8000); baseline 6/1158 -> 927/1039 (89.2%). Regression: `tests/js/webrtc.html`. Remaining gaps: per-m-section transports before BUNDLE, codec negotiation details in getParameters, ICE disconnected/failed states, DataChannel transfer to workers.
- Module workers now supported (d6d0640, a924029; tests/js/worker-module.html). ChatGPT's "Orb warmup" uses one. DIL reply still "couldn't load" after this. DIL frame receives createRunner (compiledDil ~5KB, data, dilComponents, statePersistence, globals) then disposeRunner; wrapping `event.source.postMessage` in LUMEN_FRAME_PRE logged no replies, so check whether Lumen's iframe `MessageEvent.source` is a real parent WindowProxy, or whether the runner replies on a transferred port.
- ROOT CAUSE of "This response couldn't load": MessagePort.postMessage ignored its transfer list. DIL frame hands the host a port in `ready`; host sends `createRunner` over that port with a second port, and the frame forwards runner ports to its worker the same way. Fixed in 2e6469b (page + worker + cross-realm `_deliver`; tests/js/port-xfer.html). Runner now compiles/evaluates/renders and the error is gone. Debug tip: frame pre that patches `URLSearchParams.prototype.get('dil-diagnostics')` to '1' makes the runner post `kind:diagnostic` records to the parent.

- ChatGPT: verified natively in Lumen (login email/password/TOTP, send "hi", reply "Hi! What can I help you with today?" renders). Last blockers were MessagePort transfer-through-port (2e6469b) and cross-realm `structuredClone` of ArrayBuffer (6dd8448, test `tests/js/port-xrealm.html`).
- Benchmark (2026-10-09, macOS VM, 7 tabs: local 1080p H.264 video + 6 long ChatGPT chats, 2 runs each; YouTube gives both browsers "confirm you're not a bot" here): RAM used Lumen 923/1326 MB vs Chrome 1285/2146 MB; footprint 1960/1832 vs 2448/2539 MB; CPU 17.2/16.6% vs 25.4/28.6%. Scripts in ~/bench (bench.sh NAME SETTLE cmd..., chk.mjs, cdp.mjs). Release held until ChatGPT UI parity fixes are done (some Lumen tabs rendered less text; one cause was a 429 rate limit from repeated test loads).
- ChatGPT UI parity: fixed unitless calc() line-height (Tailwind `--text-sm--line-height: calc(1.25/.875)` was treated as px, so sidebar rows were ~1px tall). Still to check: sidebar border colour, header icons (`<use>` sprites), composer box, avatar colour, video `width:100%` sizing.
- ChatGPT UI parity progress (2026-10-09): fixed flex column min-height:auto (673f9ae), custom-property var() resolved after cascade (e3be8a5), 0% flex-basis in indefinite container = content (1d1ec0e), column-reverse scrollers start at end (this commit). Sidebar now matches Chrome. Conversation text now shows but turns OVERLAP and text runs to right edge: next look at ChatGPT's virtualized turn positioning (inline top/transform from measured heights, ResizeObserver/offsetHeight) and thread max-width vars. Probes: ~/bench/probe*-lumen.js via LUMEN_PRE_FILE, Chrome via `node ~/bench/cdp.mjs 9334 eval`.
- ResizeObserver now re-checks sizes every 100ms while observers are live (it only fired once before, so ChatGPT's virtualized turns kept placeholder heights and overlapped). Test: `LUMEN_CONSOLE=1 ./build/lumen file://$PWD/tests/layout/resize-observer-late.html` (build/jsrun has no layout, so size tests must use build/lumen). Remaining ChatGPT diffs vs Chrome: thread column right-shifted/overflowing to window edge (check container queries + `--thread-content-max-width`, TW 680 vs content at x=620..1260), link underline drawn as strike-through, composer sometimes missing at bottom; then rerun benchmark (~/bench/bench.sh).
- ChatGPT UI parity (2026-10-09, later): abs boxes whose containing block is outside a scroller no longer scroll/clip with it (ChatGPT composer lives inside `.thread-scroll-container` but is positioned against `group/thread-scroll-layout`; 82a1986, test `tests/layout/colrev-abs.html`); `<use href="file.svg#id">` resolves external sprite documents, fetched once per URL via `svg_ext_ref_hook` in main.c (d940347, test `tests/layout/svg-use-external.html`); real `color-mix()` (c980230, `tests/js/color-mix.html`). Still different from Chrome: composer placeholder "Ask ChatGPT" + mic button, file-card divider line, `getComputedStyle` returns "" for border/box-shadow longhands, `scrollHeight` returns own height (03-element.js) and element `scrollTop` is JS-only (not wired to native scroll).
- FileReader.readAsDataURL spread the whole buffer into String.fromCharCode and hit "Maximum call stack size exceeded" on ChatGPT (152b400, `tests/layout/filereader-large.html`, run with build/lumen; jsrun's Blob has no bytes).
- Benchmark gotchas: Chrome blocks unmuted autoplay, so launch it with `--autoplay-policy=no-user-gesture-required` and check `video.paused` via CDP (`~/bench/vchk.mjs`); ChatGPT rate-limits repeated conversation loads ("Could not load this ChatGPT conversation"), so check every tab's text (`lumchk2.js`, `tchk.mjs`) and only count runs where all six loaded.

## Memory + scroll (2026-10-09, 6 ChatGPT tabs)
- Scrolling: wheel now scrolls the innermost `overflow:auto|scroll` box under the pointer (`wheel_scroll` in main.c), falling back to the page; element `scrollTop/scrollLeft/scrollTo/scrollBy/scrollHeight` are backed by native `Node.scroll_x/y` (`jsg_scroll`/`jsg_set_scroll`, N.scrollPos/N.setScroll). Test: tests/layout/scroll-api.html (run with build/lumen).
- `LUMEN_AUTOSCROLL=x,y,dy,start_ms,count` pushes wheel ticks every 16ms (CPU-while-scrolling measurement).
- Where RAM goes (footprint ~1.8-2.0 GB): page isolates ~1.0-1.3 GB (140-300 MB per ChatGPT tab), 18 worker isolates (3 per ChatGPT tab) ~370 MB, script sources ~50 MB per tab. DOM nodes (~20k, 216 B), ComputedStyle (~12k, 880 B), parsed CSS (12 MB) are small. malloc goes through PartitionAlloc (V8 shim) so malloc_zone stats are useless; use `LUMEN_MEM_STATS=1` (lumen-mem / lumen-mem2 lines), `LUMEN_MEM_DETAIL=1` (per isolate), `LUMEN_HEAP_SNAP=path` (heap snapshot of first isolate after 50s), analysis scripts ~/bench/snap.py, snap2.py.
- Done: big script sources (>=64 KB) are external strings shared across isolates by SHA-256 (`jsrc`), idle workers GC once after 10s, background tabs get IsolateInBackgroundNotification + one LowMemoryNotification after 15s (`js_set_background`).
- Tab sleep (ChatGPT-specific): idle background `https://chatgpt.com/` tabs drop their JsCtx after 60s (no fetch in flight) and keep DOM/layout on screen; activating reloads. `LUMEN_TAB_SLEEP_MS` (0 = off), `LUMEN_TAB_SLEEP=all` for any site. 6 ChatGPT tabs at 110s: 418 MB footprint with sleep vs 1317 MB without (was ~1.8-2.0 GB before this work).
- Scroll paint: page scroll by whole device pixels shifts the page canvas and rasters only the exposed strip + position:fixed bounds (`DisplayList.fix`); element-scroller wheel repaints only the scroller rect (`App.srect/sdirty`). Wikipedia 60 wheel ticks/s: 50% -> 38% CPU (remaining cost: dl_build ~55ms/s + event loop); ChatGPT thread scroll ~6%, idle ~2%. `LUMEN_DEBUG_PAINT=1` shows partial/full counts.
- Next ideas: cache display list across pure scrolls (dl_build is now the main per-frame cost); skip layout_hit on every wheel tick (cache scroll target for a gesture); throttle timers/rAF in background tabs (document.hidden is still always false).

## Stability / settings (Oct 9)
- Tab sleep is opt-in (Settings toggle, `tab_sleep=` in settings.txt); `LUMEN_TAB_SLEEP_MS` still overrides.
- UA: Chrome 150 default; Settings offers Chrome 150 / Firefox 150 / Lumen (`user_agent=` 0/1/2). `navigator.vendor` / `userAgentData` follow the UA.
- Crash recovery: SIGSEGV/SIGBUS on the main thread `siglongjmp`s back to the event loop; the tab's page is abandoned (leaked), the tab reloads (max 3 times) and a dismissable red notice is shown. `LUMEN_CRASH_TEST=<ms>` triggers a test fault. Faults on other threads still terminate. The handler logs the faulting pc/lr (use `atos -o build/lumen -l <load> <pc>`).
- ChatGPT sidebar toggle crash: `click_page` read `b->node` from a layout freed by handlers/restyle; now walks from the pinned target. `js_dispatch` pins targets until the event batch ends (`js_release_pins`).
- Open: collapsed ChatGPT sidebar keeps the aside at 340px (`--app-shell-left-panel-width` resolves empty on the aside in Lumen; Chrome gets it from `--app-shell-animated-left-panel-width` set inline on `.Layout-*`).

## CSS / fonts / audio (Oct 9, later)
- `@container` rules are now conditional (`ContainerCond` on `Rule`, `css_container_eval` in parse.c, `container_matches` in style.c). Size comes from `Node.cq_w/cq_h` set by `track_containers` after layout; `style()` queries read the container's custom props.
- `@font-face`: `sheet_fonts`/`font_pump` in main.c, `font_declare`/`font_loaded` in font.c. WOFF2 skipped (no brotli in FreeType build); TTF/WOFF fallbacks used. Check face-memory lifetime if fonts misbehave.
- Layout-dependent tests (`cq-units`, `container-query`, `webfont`) must run in `build/lumen`, not `jsrun`.
- Audio: `play()` before metadata never opened the SDL device (silent playback). Fixed in media.c. VM has no audio device: verify with `SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=out.raw` (F32LE stereo 48k) + `~/bench/audio/analyze.py`.
- Missing: Web Audio API (`AudioContext` undefined).

## ChatGPT layout parity notes (min()/max(), pseudo, cell valign)
- `Length` now carries a deferred second operand (`mm`, `px2`, `pct2`) so `min(100%, Npx)` / `max()` resolve at layout time via `res()`; arithmetic in `calc_term`/`calc_expr` propagates it, `clamp()` with percentages still uses the old best-guess. Test: `tests/js/minmax-pct.html` (full `lumen`, not jsrun).
- Absolute/fixed/floated `::before`/`::after` are blockified in `build_pseudo` (ChatGPT composer placeholder).
- Table cells with explicit `height` align their real content (`scroll_h`), fixing KaTeX `vlist` superscripts. Test: `tests/js/cell-valign.html`.
- With Lumen's 84px tab strip, ChatGPT's main area is <856px (53.5rem) so it uses its compact 640px column; at Chrome's width (`LUMEN_NO_SIDEBAR=1 LUMEN_WINDOW=1200x960`) it is 768px like Chrome. Not a bug.
- Open: `getComputedStyle` returns "" for shorthands (`padding`) and for `vertical-align`, `float` and pseudo-element styles (`getComputedStyle(el, '::before')`).

## ChatGPT live streaming (fixed in 6599817: fetch streams heads + decoded chunks; manual check ~/bench/stream/srv.py)
- Net layer has no body streaming: `http_once` (src/net/http.c) buffers the whole body; `NetChunkFn` is declared in net.h but unused. `fetch()` resolves only after the response completes, and `Response.body` is a single-chunk stream (07b-net.js).
- Repro: send "Count from 1 to 60 in words, one per line." in a new chat (`~/bench/fc5pre.js` tees the fetch). `POST /backend-api/f/conversation` returns the full ~54 KB `text/event-stream` in 1 chunk ending with `message_stream_complete` and `[DONE]`, but `main.textContent` stays ~158 chars (only the prompt) until reload. After reload, the reply renders fully (same length as Chrome, 5345 chars for 1..300).
- Plan: stream response heads and chunks from the worker to the main thread (queue drained in `net_poll`), resolve `fetch` on headers, and feed the body `ReadableStream` per chunk. Buffer when `content-encoding` needs whole-body decompression unless an incremental decoder is added. Then recheck whether the live reply renders.
- Interactive check (`~/bench/fcheck.js`): model picker, profile menu, search, chat actions, More, rate, copy, edit/cancel all match Chrome. Settings via `#settings` hash opens in neither browser.

## Typing CPU / damage raster / CPU readout (2026-10-09)
- `layout_box()` memoises results per box (mode + containing block + forced size) and re-registers
  absolute descendants from `L->alog`; tables are excluded. `LUMEN_DEBUG_LAYOUT=1` prints runs/s and hit rate.
- `font_get()` has a thread-local memo invalidated by `font_gen` when web fonts change.
- Damage raster (`render()` in main.c): each page display item gets a hash + bounds (`dl_sigs`);
  on DOM/layout/UI-only frames only the union of changed items is rastered. `LUMEN_CHECK_DAMAGE=1`
  compares against a full raster and logs mismatched pixels (0 on ChatGPT typing).
- Typing in the ChatGPT composer is now ~17-30% CPU (was 66-95%); remaining cost is ChatGPT's JS
  (`js_dispatch`) plus synchronous layouts from its geometry reads (`h_sync`). Target is <10%.
- Settings > "CPU usage" (`cpu_hud`, off by default, on in debug builds) shows Lumen's CPU and RAM (GB)
  at the bottom of the workspace sidebar every 2 s; clicking opens a popup with host CPU and RAM.
- Next: ChatGPT composer click-to-caret and selection (paint_edit_caret always draws at the end).
- Long ChatGPT conversations: nested flex columns measured each item (fh=-1) then laid it out again with
  fh = the same height, so the memo missed and work doubled per level (~2^11 calls; 490ms layouts, 100% CPU).
  `layout_box()` now treats a forced height equal to the natural one as a hit unless a child resolves
  percentages/insets against it (`hdep`): 490ms -> 7ms. `LUMEN_DEBUG_LAYOUT=2` prints the hottest chain.
- `LUMEN_DEBUG_JS=1` prints per-event-type dispatch counts and ms each second.
- contenteditable selection: JS `Selection` is canonical; every range change mirrors to `Document.sel[]/selo[]`
  (UTF-8 byte offsets) via `N.selSet`. Native clicks/drags/dblclick/triple/Cmd+A/arrows dispatch `lumensel-*`
  events into the prelude, which moves the JS selection. `edit_sel_sync()` copies it into `g_tsel` so the
  highlight and Cmd+C reuse the page-selection code; the caret paints at the actual focus offset.
  Text inside contenteditable counts as selectable even under `user-select: none` (ChatGPT composer).
- Wheel: whole-number SDL deltas = mouse notches -> fixed `WHEEL_STEP` (120px); fractional (trackpad) stay 40px/unit.
  Settings "Smooth scrolling" (`smooth_scroll`, off by default) animates notches (30% of remainder per 16ms frame).
  Wikipedia, 60 notches/s: ~21% CPU (was 38%); remaining cost is display-list rebuild (~36 ms/s).
- Web Audio (`src/js/prelude/07w-audio.js`): graph renders in JS in 128-frame quanta, pushed as stereo f32 @48 kHz
  through `N.waPush` to one SDL stream per AudioContext (`wa_*` in media.c, closed with the JS context).
  Timer runs only while sources play (idle clock advances by wall time). decodeAudioData = FFmpeg `media_decode_pcm`.
  Real: Gain, Oscillator (+PeriodicWave), BufferSource, ConstantSource, StereoPanner, Delay, Analyser, OfflineAudioContext.
  Pass-through stubs: BiquadFilter, DynamicsCompressor, Convolver. Missing: AudioWorklet, MediaElement/MediaStream sources.
  Verified with SDL disk driver (`SDL_AUDIO_DRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=...`): 440 Hz osc + decoded mp3, ~1% CPU.

## ChatGPT feature audit (latest)
- Working: send/stream/stop, regenerate, edit, copy (navigator.clipboard → SDL clipboard), like/dislike, Share (this ChatGPT build creates the link and copies it; no dialog), rename, search dialog, projects expand, profile menu, Settings page.
- Fixed this round: getBoundingClientRect transforms/scroll (`jsg_vrect`), hit testing honours transforms and `pointer-events:none` on text/inline boxes, Chrome-like pointer event fields, clipboard + `ClipboardItem` + `execCommand('copy')`, box-shadow lists, backdrop-filter alpha approximation (not a real blur).
- Screenshots from `screencapture` are ~0.98x of screen points (1568 vs 1600): scale coords by 1.0204 before `click.js`.
- Also working: Settings page, Appearance dark/light switch, Cmd+Shift+O new chat, Cmd+K search, Escape.
- Keyboard: Cmd/Ctrl shortcuts go to the page first (browser keeps Q/W/T/N/L/Tab/1-9); unfocused keys go to body; events carry modifier flags and `code`. Test page: `tests/layout/keys.html` (drive with System Events after `set frontmost of (first process whose unix id is $P)`).
- `:hover` now works (NF_HOVER set from page_move; only nodes whose hover state changes are restyled). Before this, ChatGPT sidebar rows never showed their "..." (Chat actions) and pin buttons. Row menu shows Rename/Unpin/Share/Archive/Delete/Move to project like Chrome; Delete opens the confirm dialog and sends `DELETE /backend-api/conversation/id/<id>`. Regression: tests/layout/hover.html (`RESULTS hover row:PASS group:PASS inherit:PASS has:PASS off:PASS`).
- Hover invalidation (`style_hover_affects` / `style_hover_has` in src/css/style.c): `idx_build` records which id/class/tag keys have `:hover` rules (self, subtree, or parent subtree for `+`/`~`) and which have `:has(...:hover...)`; `page_hover` restyles only those nodes. Without this, every hover change restyled the whole ChatGPT document (~100-350 ms each).
- Archive and delete verified on throwaway chats (PATCH / DELETE sent, row removed, navigates home).
- Still to check: model picker/tools, GPT pages.
