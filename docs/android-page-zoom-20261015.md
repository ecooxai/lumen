# Android page zoom candidate (2026-10-15)

Update: device checks now recorded in [integrated Android report](android-device-integrated-20261015.md).
That report supersedes the historical installed-build/test-window status below.

Status: implemented and host-tested; Android APK built and verified, NOT installed.
The daily-browser goal remains active, Vulkan first. This is reflowing page zoom;
pinch zoom and visual-viewport pan/scale are NOT implemented by this change.

## Behavior

Settings provides a persistent default from 50% through 300%. Desktop/external
keyboard Ctrl/Cmd +/− changes the active tab; Ctrl/Cmd+0 inherits the default.
Ctrl+wheel changes zoom. Tab overrides survive navigation within the tab but
are not persisted per site or session. Browser UI stays at 100%; page layout,
CSS viewport and DPR, paint canvas, iframe resolution and pointer/touch/scroll
coordinates share the page zoom factor. Physical framebuffer dimensions stay
unchanged. Android scroll/fling distances remain proportional to finger movement.
Zoom invalidates partial presentation and cancels stale selection/drag/hover and
Android gesture state. Initial top-level and frame stylesheet parsing receive DPR.

## Verification

All compilation used tools/codex_compile.py with the shared 480% CPU quota,
SCHED_IDLE/nice19, at most six compiler jobs. Frozen source V8 was not resumed.
Host full-browser and Android assembleDebug builds passed. Native ELF dependency
closure and APK v2 signature verification passed. Candidate includes the pending
page V8 wakeup, module-cloning and video-upload reuse changes.

- Android scroll: 8/8, including zoomed nested hit coordinates, finger distances,
  physical pixel rounding, default inheritance and internal UI exclusion (UBSan).
- Android touch: 24/24; Android density: 15/15; viewport styles: 13/13.
- Isolated Xvfb GUI, wgpu/Vulkan presentation, local HTTP fixture:
  tests/js/page-zoom.html. Page clicks at 100% and 200% map to (90,45).
  At 200%, viewport is 400×328 CSS pixels, DPR2; iframe retains 160 CSS pixel
  width, reports DPR2 and maps clicks to (40,30). One wheel notch scrolls the
  nested box 60 CSS pixels without moving the root. Reset restores 100%.
- Actual Settings button click selects 150%, writes page_zoom=150, and leaves
  Settings chrome/layout at normal size (screenshots inspected). Following page
  navigation and process restart use DPR1.5 and width533.333 CSS pixels. Temporary
  175% override resets to150%. Nested scrolling moves80 CSS pixels per notch.
- Whole-process and every-thread /proc CPU/RSS sampling at1Hz throughout each
  GUI run. Settled nine-sample intervals: mean0.111–0.333%, peak about1.001%;
  ending RSS173.78–176.87MiB for the primary tests. Maximum sample gap1.003s.
  These short host fixture measurements are NOT Android performance or RAM gains.

Artifacts and monitor rows: build/codex-page-zoom-20261015/{http-run,settings-run,
settings-apply,restart-run,root-scroll-run,root-hit-run}/. The first attempt passed
a plain filesystem path to the browser, which normalized it to HTTPS; it loaded
an error page and is NOT valid test evidence. Subsequent runs use an owned local
HTTP server. Five keyboard steps, not six, reach200% from100%.

## Remaining findings and acceptance

Root scrolling visibly moves120 physical pixels per notch at150%. The fixture's
window scroll listener is not invoked: existing viewport_update filters out
SCROLL-only changes before invoking viewportChanged. Track/fix root scroll event
notification with trusted document/window event semantics and no idle polling.
The fixed-position button also disappears after root scrolling in the fixture;
compare with the baseline and audit layout/paint/hit handling before claiming
fixed-element compatibility. Root-input follow-up evidence is appended below.

Phone untouched during this checkpoint. The separate uncontested-phone-window
question remains pending; do not interrupt user phone use. Installed phone build
remains42af2d... streaming. No new adb reverses. Phone verification must include
zoomed scroll/fling, text selection/editing, keyboard resize, iframes, video
controls and stable CPU/RAM before claiming Android usability. Pinch, per-site
zoom persistence, broader Wasm, YouTube/ChatGPT/Gemini/login and Vulkan performance
acceptance remain open. OpenGL ES and CPU software optimization remain deferred.

Candidate:
`build/codex-page-zoom-20261015/lumen-vulkan-page-zoom-debug.apk`
SHA256: `3b326f001c8773dddc1edfa2ee6fdd54ac1e60450a5dca80a0d7305a32eed6be`.

Root-input follow-up PASS: at150%, root scrollY=80; iframe click after scrolling
still maps to(40,30). root-hit-run/assertions.json records final measured values.
All owned GUI/server/sampler/build/verification sessions finished and joined.

Follow-up: android-scroll-events-20261015.md implements and host-verifies missing
root scroll notifications. The fixed-button observation was fixture overlap with
a later iframe, confirmed on the pre-change binary by changing only z-index.
No generic fixed-content paint defect was reproduced; remove that item from the
open bug list. Device validation remains pending.
