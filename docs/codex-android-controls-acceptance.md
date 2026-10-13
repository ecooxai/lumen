# Android scroll and input acceptance

Serve `tests/pages/codex-android-scroll-controls.html` over an owned loopback
HTTP server and Android reverse port. Use the existing engine, Vulkan renderer
and the user's persisted pixel scale. Do not substitute a WebView or change
Android density. Record the installed APK hash and actual BrowserSurface bounds.

## Phone checks

1. Verify Band 1, the green/orange page edges, a single browser toolbar and the
   fixed top label in an actual screenshot. The footer starts hidden so the
   fixed-region union does not span the entire page.
2. Scroll forward far enough to show later bands, then reverse. Fixed labels
   must remain stationary; exposed rows must have the correct band background
   and edges without stale pixels, duplicated text or a shifted toolbar.
   Check paint diagnostics for actual blit frames before claiming scroll-ring
   coverage. Toggle the footer on, then test scrolling again: the top/bottom
   fixed-region union should exercise the full-repaint fallback, not row reuse.
3. Open and close the yellow DOM overlay after scrolling. Verify its actual
   appearance and that closing it restores the underlying page.
4. Position the Band 2 or Band 4 input in the lower part of the visible page.
   Tap it, inspect the keyboard and resized BrowserSurface, then enter ordinary
   text. The focused input and entered text should remain visible above the
   keyboard. Record a failure rather than scrolling it manually into view and
   treating that as automatic visibility.
5. Dismiss the keyboard with Back. Verify that the viewport and fixed bottom
   label recover, then reverse-scroll and reopen an input.
6. Use ordinary address-bar taps and typing to open Google and the Wikipedia
   browser article. Inspect rendered content, not just successful ADB commands.
   A load is not proof that search, links, forms or general site compatibility
   work. Record each attempted interaction and its observed outcome separately.

## Evidence rules

- A fresh test owner holds both existing phone locks. No concurrent phone owner.
- Sample process and live-thread CPU/RSS every second throughout launch,
  interactions, actual image review, formal idle and cleanup. Reuse the strict
  process-identity and cadence validation; never turn missing samples into zero.
- Record interaction and idle phases separately. Stop and investigate sustained
  unexplained idle CPU above 3%; do not conceal it in a whole-run average.
- Preserve data and settings. No accounts, cookie import, root, global security
  changes or global log clearing are needed for these checks.
- Preserve unsuccessful runs and distinguish harness errors from browser bugs.
  Successful activity launch or HTTP response alone is not visual acceptance.
- Stop the app, remove only owned server/reverse/diagnostic resources, join the
  sampler and verify both locks are released before reporting completion.

## Focused idle regression

Keep the fixed header and footer visible when testing a focused text input.
After the keyboard transition settles, record a separate focused-idle interval
with no typing or scrolling. Correlate the process and renderer-thread samples
with complete paint CPU/wall pairs inside that interval. A blinking caret must
not force full-page raster and composition twice per second simply because its
display-list entry appears or disappears before unchanged fixed controls.

Check visible and offscreen caret transitions separately. Hidden focus is an
input-visibility failure even if its CPU use is fixed; do not count a repaint
optimization as repairing keyboard avoidance. Keep the sustained-idle guard
enabled, and do not claim improved phone CPU from host damage-policy or raster
pixel tests alone. The original installed baseline recorded 8.2% process CPU
and 8.0% renderer-thread CPU during the failing five-second hidden-focus window.

This fixture tests controls and rendering transitions. It does not establish
Chrome layout parity, frame rate, WebGL/WebGPU support, real-site performance
or the half-Chrome-RAM target.
