# Positioned controls and viewport geometry, 2026-10-15

The daily-browser goal remains active. Vulkan is still first; OpenGL ES and
software optimization are deferred. No credentials were entered.

## Demonstrated defects and changes

YouTube mobile creates its control overlay under several static wrappers. The
underlying player is positioned. Lumen painted the overlay above the player but
hit testing sorted only immediate siblings, so taps went through to the video.
Hit testing now considers positioned descendants through ordinary wrappers in
the same layer ordering. Reverse tree traversal preserves equal-z document order;
selection needs no heap allocation or sorting per pointer event. Numeric ordering
also applies to negative layers. Existing renderer layer boundaries are retained;
this is not full CSS stacking-context or transform conformance.

Hidden inline boxes also returned hits even though block/text targets checked
visibility. Their own hit rectangles now respect visibility while visible
descendants can still receive input.

The phone regression exposed a separate geometry error: a fixed control received
a real tap after scrolling, but getBoundingClientRect() moved its reported
rectangle by the document scroll distance. The native viewport rectangle helper
now applies document scrolling only if no fixed ancestor stops it. Element scroll
offsets and translations remain applied. The binding reads viewport state from
the element's owning document, and the JS wrapper no longer subtracts it again.

The ordering reference is [CSS 2.2 Appendix E](https://www.w3.org/TR/CSS22/zindex.html).
The geometry/hit API reference is [CSSOM View](https://drafts.csswg.org/cssom-view/#dom-document-elementfrompoint).
These references describe the intended behavior, not a claim of broad conformance.

## Validation

- Native real CSS/layout regression under UBSan: 15 hit scenarios at two document
  scroll positions, plus seven viewport geometry assertions (37 assertions).
  The initial ordering regression failed 12 assertions. The additional hidden
  inline regression failed twice before its fix. All final assertions pass.
- Nine existing native scroll/hit scenarios still pass, including fixed headers,
  footers, horizontal scrolling, nested scrollers and changing scroll positions
  without painting first.
- Phone `/player-overlay`: four geometry/hit checks and two trusted taps, before
  and after scrolling, all pass. The post-scroll check failed before the viewport
  rectangle repair (actual tap reached the control, computed target did not).
- Clean fixture final 30 one-second samples: mean CPU 0.4552%, peak 7.8431%,
  RSS 229.28 MiB; no cadence errors. Peak includes the settling activity.
- Whole-process and per-thread CPU monitored once per second for every runtime
  test. Diagnostic builds were used only to inspect public player state/events.

## Real YouTube evidence and remaining work

On the instrumented Vulkan run, the first center tap removed the unmute prompt.
After the hit fix, a later center tap reached the actual pause control, adding
fadein to the overlay and pausing at 30.218 seconds. Controls were visibly present
in the screenshot, and state stayed paused. A tap on the timeline sought to
79.438 seconds. Resume reached readyState 4 and playback advanced through
148.015 seconds in recorded state samples. This run did not reproduce the prior
UMP rejection, but its seeking/pausing sequence differs from uninterrupted
playback. The earlier streaming failure is not established as fixed.

Control hide duration while playing, touch gestures/drag seeking, fullscreen,
sustained uninterrupted playback and broader page scrolling still require work.
Android still dispatches compatibility mouse events for taps rather than a full
DOM touch/pointer lifecycle. The prior mobile module exception was not observed
in these captured runs; its cause is not established.

The final clean APK also shows the controls at one and eight seconds after
pausing, with the play icon and 0:21 timestamp visible. Its paused-page idle
CPU remains above target; this is not a full performance acceptance pass.
A native sample profile of that clean paused page recorded 839 userspace samples
at 999 Hz over 20.0407 seconds, zero lost. js_tick accounts for 20.26% including
callees (5.60% self), jsg_vrect 4.05% self, and SDL main-loop/wait housekeeping
also appears. Percentages are shares of sampled CPU, not whole-process usage.
The remaining work is mainly the render/JS thread; investigate context ticking,
geometry polling and page callbacks rather than declaring Vulkan presentation
the cause. After profiling stopped, the final 30 one-second idle samples averaged
3.1673% CPU, peak 4.0816%, RSS 348.34 MiB, with no cadence errors. An earlier
settled pre-profile window averaged about 4.0%; no controlled before/after
performance improvement is claimed.

## Artifact

Clean APK: `build/codex-youtube-controls-20261015/lumen-vulkan-controls-debug.apk`

SHA256: `ba443a7e6d04d98858ce2edda5bfb406474d2bd66082691acd585b00b895e67f`

Native ELF/dependency and APK v2 signature verification pass. Compilation used
the shared CPU cap, Gradle without a daemon and the verified V8 prebuilt.
Temporary native logging/preload code was restored; it is absent from this APK.
Evidence (logs, screenshots, raw process/thread samples, sanitizer output and
verification reports) is under `build/codex-youtube-controls-20261015/`.

ChatGPT/Gemini, Google login, zoom and full daily-browser acceptance are unfinished.

Cleanup: app stopped; sampler/server stopped and joined; owned adb reverse
removed; private probe, diagnostic log and profile recordings removed. The clean
verified APK remains installed.
