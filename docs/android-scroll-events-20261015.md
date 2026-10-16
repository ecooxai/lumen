# Root scroll event candidate (2026-10-15)

Update: device checks now recorded in [integrated Android report](android-device-integrated-20261015.md).
That report supersedes the historical installed-build/test-window status below.

Status: host-tested, Android candidate built; NOT installed. Goal ACTIVE,
Vulkan first. Previous checkpoint added page zoom; this checkpoint fixes missing
root-document scroll notifications and avoids a one-second follow-up delay.

## Change

viewport_update previously consumed SCROLL changes only for visual-generation
tracking and returned before JavaScript notification. It now dispatches a trusted,
non-cancelable, bubbling scroll event at the owning document, reaching that
window through normal event propagation. It does not dispatch a duplicate window
event or a visual-viewport pan event. Resize/media handling remains separate;
scroll-only changes do not force layout or scan media queries.

JsViewportState can inspect changes without consuming them. js_next_deadline
uses that inspection to schedule post-callback/animation-frame viewport changes
at the next16ms boundary, instead of waiting for the main-loop1000ms fallback.
There is no new repeating timer; unchanged viewports add no deadline. Pending
V8 foreground tasks retain their own deadlines. Root scrollTo synchronizes
layout and clamps finite positions before exposing them, and the viewport getter
clamps transient wheel/key positions to document bounds before event delivery.

Semantics reference: [CSSOM View13.2](https://drafts.csswg.org/cssom-view/#scrolling-events), document-targeted bubbling scroll events.
This is not a claim of full CSSOM scroll conformance. Element scroll-event queue
ordering, scrollend, net-zero movement between viewport snapshots, horizontal
root scrolling and visual-viewport pinch behavior still need further work.

## Verification

- Node viewport/event suite8/8 passes: target/currentTarget, trusted/bubbling/
  cancelable flags, capture/bubble order, propagation cancellation, listener
  exceptions, resize/media regressions and no visual-pan event for root scroll.
- Native V8 viewport runtime assertions pass: duplicate suppression across100
  unchanged ticks, coalescing multiple API changes, reentrant scroll notification
  on a later tick, finite follow-up deadline, and no extra layout on scroll.
  Runtime process sampled at1Hz:0.121477 CPU seconds, peakRSS80.39MiB. Its first
  attempt incorrectly required the entire runtime deadline to be infinite; V8
  also owns delayed tasks, so that unrelated assumption was removed. Actual idle
  scheduling is checked by viewport-state assertions and full-browser monitoring.
- Viewport-state assertions,11 fixed-region bound cases and9 fixed hit tests pass.
- Real full browser in isolated Xvfb using wgpu/Vulkan and local HTTP fixtures:
  tests/js/root-scroll.html and root-scroll-frame.html. At150%, one wheel notch
  moves80CSS pixels and delivers exactly one document/window pair; fixed button
  stays atCSS top20 and receives clicks at the same screen position. Negative
  scrollTo clamps to0; three further upward wheel notches at top emit no events.
  Bottom clamps to1562.6666CSS pixels for a2000px document and437.3333px viewport.
  Handler-triggered120→240 scroll produces the follow-up in16.522ms. Child iframe
  receives its own two notifications at75px; parent stays at0 and gets no spurious
  scroll event. Five root document events and five bubbled window deliveries in
  the combined run. Assertions saved in frame150/assertions.json.
- Whole-process and all-thread1Hz monitoring throughout GUI runs. Final23 samples,
  nine settled idle samples: CPUmean0.222%, peak1.000%, RSS176.32MiB;
  max sampling gap1.003s. Prior action-only run mean0.333%, peak1.996%, RSS170.67MiB.
  Baseline150 and initial candidate150 each averaged0.222%. These short host runs
  do not establish Android speed, scrolling frame rate or RAM improvement.
- Native browser and Android Gradle build passed, using shared480% compilation
  quota, nice19/SCHED_IDLE and max6 compiler jobs; no frozen V8 source build.
  APK ELF dependencies and v2 signature verified. No phone interaction this turn;
  separate uncontested-phone-window answer remains pending. No adb reverses added.

Artifacts: build/codex-scroll-events-20261015/. Candidate includes pending V8
page wakeup, module clone, GPU video upload reuse and page zoom changes:
`lumen-vulkan-scroll-events-debug.apk`
SHA256 `0e5dac825a86ac3f741c5a4f0ae57502fcc23057f981b4b3ce21b4c1bae33a33`.
Installed phone remains42af2d... streaming. Credentials remain unused.

Daily-browser acceptance is not complete: Vulkan phone smoothness/RAM, YouTube
controls/playback, ChatGPT/Gemini/Google login, pinch zoom, full Wasm shared-memory
isolation and broader conformance remain outstanding. OpenGL ES and CPU renderer
optimization remain deferred per user priority.

## Fixed-content finding resolved

The previous page-zoom fixture's disappearing button was covered by a later
iframe at the same location after scrolling, with both at automatic z-index.
The pre-change binary keeps an isolated fixed button visible/clickable at both
100% and150%. Repeating the original overlapping fixture on that same binary
with only z-index:1 added to the button also keeps it visible/clickable after
80CSS pixels of root scroll (baseline-zindex screenshot and click log). No
production fixed-layout/paint change was needed; do not carry this as a known
rendering defect. All owned test/server/sampler/build sessions finished and joined.
