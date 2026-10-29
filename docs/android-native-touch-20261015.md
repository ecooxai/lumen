# Android native touch delivery (2026-10-15)

Android finger events now reach the page at contact start/move/end rather than
waiting until release and inventing a full mouse gesture. The JS bridge retains
the original contact target and pointer identity, supplies touch lists and touch
pointer metadata, and cancels the pointer stream when native scrolling takes over.
Touch cancellation suppresses native scroll and compatibility clicks; pointerdown
cancellation suppresses compatibility mouse down/up without suppressing click.
Page touch capability is exposed on Android. Compatibility clicks identify their
touch source. The Android YouTube fake mouse-motion tap hook is removed; real
mouse input retains its existing behavior.

This is not full touch conformance. Iframe contacts still use click_frame's mouse
fallback. CSS touch-action, explicit pointer capture, full Touch/TouchList interface
conformance and pinch zoom remain unfinished. Click default actions still use the
existing release-time shell hit test; mutation-induced retargeting needs coverage.
The event model follows the touch/mouse cancellation distinction in
https://www.w3.org/TR/touch-events/ and https://www.w3.org/TR/pointerevents2/.

## Verification

Six JS behavior tests cover delivery before release, immutable touch snapshots,
identity, cancellation, passive listeners, multiple contacts and scroll takeover.
All24 Android gesture/adapter tests pass, including DOM preventDefault versus
native scrolling. Six existing viewport tests and the desktop YouTube visibility
test pass. The obsolete Android fake-mouse-motion assertion was removed.
Android APK compilation, native verification and v2 signing checks pass; all
compilation went through tools/codex_compile.py.

On MI8/Vulkan, tests/js/native-touch.html confirms:

- A real tap delivers pointerdown/touchstart, pointerup/touchend, then exactly one
  compatibility mouse pair and click. Pointer type is touch and mouse source
  capabilities identify touch input. maxTouchPoints16 and window touch handlers.
- Canceled touchstart produces no mouse events or click.
- Canceled touchmove produces no native scroll (scrollY remains0) or click.
- Native scrolling produces one pointercancel, continued noncancelable touchmoves,
  touchend and no click; scrollY135.5 after the sampled swipe.

The local four-gesture input stage used5 CPU samples, mean8.80%, peak25%.
The subsequent fixture-idle label includes the short Wasm suite at its end, so
the combined0.35% mean over125 samples is not a dedicated idle-only measurement.

YouTube: first tap dismissed the unmute overlay. A later off-center tap revealed
controls while playback continued (pause icon, time1:09); controls were visible
in captures ending1.22 and2.24 seconds after the tap, hidden by the capture ending
3.54 seconds. Capture durations prevent assigning an exact hide timestamp.
Later playback hit the pre-existing "Something went wrong" error. The stage
named youtube-touch-idle is an ERROR page, not paused-video idle evidence.
After reload, a fresh early pause remained at0:02; the later screenshot has a
play triangle and visible controls. Long playback, seeking and fullscreen still
need validation/fixes. No overall RAM improvement is claimed.

Confirmed-paused stage:110 samples, CPU3.170% mean,24.51% peak, RSS327.41 MiB.
This is not a clean0–3% idle pass. Profile periodic idle work before declaring the
browser's CPU target met.728 whole-process/all-thread samples across the full run,
zero cadence/parse errors. App force-stopped; monitor/server verified stopped and
joined; owned reverse ports18765/18766 removed. No owned live job remains.

Evidence: build/codex-native-touch-20261015/, including server reports, screenshots,
gesture capture timings and whole-process/all-thread1Hz CPU samples. No user
credentials were entered. See wasm-browser-20261015.md for the additional Wasm audit.

Installed APK: build/codex-native-touch-20261015/lumen-vulkan-native-touch-debug.apk
SHA256:219e9ebaea744aeeb976f92a712961b559d6c820071b7d12306aefd1cde97097.
