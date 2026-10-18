# Apple browser compatibility checkpoint

Model: GPT-6 Astra Pro. Coordinator: ChatGPT built-in agents and devinmac3d native Mac tools.
Branch: `gpt-6-astra-chatgpt-apple-compat-20261018`.
Date: 2026-10-18. This is an intermediate verified checkpoint, not a release or a Safari-equivalence claim.
Current overall assessment: 65/100. The requested 98/100 goal has not been reached.

## Architecture and delivery

The C/C++/V8/SDL3/wgpu/FFmpeg engine remains intact. WebKit was used only in an isolated, nonpersistent reference utility to compare CSS geometry. Device and simulator builds use pinned, jitless V8. The device IPA is built, but currently ad-hoc signed and stale relative to the newest fixes. The Mac has no valid Apple signing identity; physical installation and the same IPA on Apple Silicon Mac remain unverified. Simulator and Catalyst binaries are not substitutes for the device IPA.

## Verified production changes

- Correct Fetch/Request credential propagation, CORS preflight session isolation, redirect credential behavior, and synchronous request ownership cleanup.
- Thread-safe socket cancellation detaches ownership before pooling/closing; cancellation cannot shut down a connection reused by another request. Cancellation interrupts header, body and TLS stalls. System DNS resolution remains a native cancellation limit.
- Worker termination stays asynchronous for ordinary page actions. Process shutdown counts startup and all worker cleanup, cancels held network requests, and drains before other subsystems are destroyed. A five-second failed drain logs and exits with failure before unsafe static destruction.
- Independent CSS translate/rotate/scale composition, percentage translation, neutral X/Y rotations, overflow-axis normalization, and generic sticky positioning shared by paint, hit testing, DOM geometry and selection.
- Sticky paint damage includes shadows and glyph overhang. Fast scroll blits match a full repaint in the regression fixture.
- DOM scroll offsets clamp synchronously, with deferred/coalesced scroll events and no fractional-deadline busy loop.
- Storage proxy enumeration follows its configurable backing-property invariants. YouTube no longer throws the previous missing `_n` ownKeys error.
- Promise rejection reporting waits for completed task checkpoints, cancels reports when handlers attach, avoids user getters during diagnostics, and releases retained promise/reason handles.
- Transient Metal drawable failures retry with a bounded backoff and cached pixels, pausing in the background. Rotation screenshot orientation is normalized in the XCTest harness.
- A reproducible V8 static-link helper preserves original archives and resolves the V8/wgpu Rust personality collision without broad symbol replacement.

## Native test evidence

The full `make -j3 test` suite passed on the remote Mac after the final worker review patch. Native application and `jsrun` builds also passed.

| Gate | Result |
| --- | --- |
| Socket cancellation | 32/32; 12 failures on the old I/O path |
| Worker lifecycle | 29/29, including held-open Fetch, importScripts and module requests |
| Worker process-exit observer | Pass; observer sees completed cleanup |
| Repeated worker exits | 30 alternating lifecycle/exit runs, no failures before the final coverage-only review |
| Independent CSS transforms | 282 checks |
| Sticky position and repaint | 125 checks |
| App viewport / touch contacts / presentation retry | 34 / 29 / 31 checks |
| Media pause/resume/end | 12 checks |
| Promise native retention | 16/16, including collection of a 32 MiB rejection reason |
| Fetch credentials after cancellation changes | 60/60 |
| Existing URL, network, CSS viewport and overflow tests | Pass |

Additional previously verified fixtures: Storage38/38; DOM scroll32/32; public Promise reporting6/6 with exactly three genuine reports; V8-link Python9/9; UI server Python5/5; resource monitor Python3/3.

Reproduce core gates with `make -j3 test` and `python3 tests/test_fetch_credentials.py --jsrun build/jsrun`.
Ignored evidence under `out/apple-compat/` includes `native-suite-shutdown-fixed.log`, `net-cancel-native-red.log`, `net-cancel-native-green.log`, `worker-shutdown-repeat.log` and `fetch-after-net-cancel.log`.

## Observed website behavior and resources

These are isolated native desktop engine runs at phone-sized windows, not proof of iOS/jitless performance or completed account login. CPU is per-process ps CPU; memory is RSS, not physical footprint. System services on this VM can create unrelated CPU load.

- ChatGPT: horizontal composer geometry is inside a 430px viewport. A clean settled sample averaged0.371% CPU, with213.219 MiB peak RSS. Vertical grid row sizing is still incorrect and has a confirmed generic regression in progress. Current end-to-end typing and response are not yet verified.
- Google: public sign-in form renders, with a settled sample averaging0.557% CPU and246.312 MiB peak RSS. No account credentials were supplied or entered; login completion is not verified.
- YouTube: an official public Big Buck Bunny watch page played, paused, sought forward10 seconds and resumed. Playback time advanced through48 seconds with readyState4 and no media error. This used programmatic page/media controls, not native touch input. RSS rose from roughly520 MiB to929 MiB before falling after seeking; investigation is ongoing. This memory behavior is not accepted as finished performance work.
- YouTube homepage idle behavior varied between runs; one high-CPU result remains documented. A later settled run averaged1.286% CPU. Deferred Promise logging removed misleading reports for handled cache-miss promises, but that alone is not proof that every CPU spike is resolved.

## Native Apple UI evidence and remaining gates

The actual custom iPhone browser passed portrait safe-area geometry, real taps, drag scrolling, fixed-element taps, landscape viewport and pixel placement, a real landscape tap, restoration to portrait, and opening the native software keyboard. XCTest `.typeText` cannot target SDL's hidden text field, so the harness now taps visible software keyboard keys and checks the exact resulting DOM value. That new gate has not completed because later simulator runs stalled before test execution or installation.

The harness uses an independent UIKit host to measure native safe insets, loopback DOM telemetry, orientation-correct screenshots and one-second CPU/RSS recording of the installed browser. The Test action now runs without LLDB to isolate debugger startup stalls; system security settings were not changed.

Remaining: complete iPhone and iPad keyboard, dismissal, rotation and page interaction runs; host-derived screen/touch/pointer capabilities; flexible grid row sizing; investigate video memory growth; current account/session flows; updated device and simulator artifacts; valid Apple signing and same-IPA Mac installation. Service Worker, popup/opener, IndexedDB and broader browser API gaps remain outside any equivalence claim.

No push, deployment, final AGENTS.md rewrite, or release completion is claimed at this checkpoint.
