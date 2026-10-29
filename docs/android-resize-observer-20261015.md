# Layout-driven resize observation on Android, 2026-10-15

## Change

The YouTube V8 profile exposed repeated ResizeObserver checks and native geometry
reads. Every live observer previously scanned all its targets every 100 ms,
including on unchanged pages. Resize observation now waits for completed layout
or a newly observed target. Disconnecting the last observer removes the native
callback immediately. There is no repeating resize-observation timer.

An atomic generation counter advances after each completed layout. It covers all
documents so a parent observer can watch targets inside an iframe. A context with
observers schedules delivery only when this generation changes or registration
requests an initial delivery. Geometry is flushed before the callback. Changes
made during callbacks remain pending, with a 16 ms minimum between deliveries to
bound feedback loops. Native callback handles are released on context teardown.

The first device run exposed stale zero-sized iframe observations because the
old timer had masked a missing layout flush. Native rectangle reads now flush
the target's owning document, including when the observer belongs to its parent.
The final device run reports the correct initial iframe size.

[Resize Observer processing](https://drafts.csswg.org/resize-observer/#processing-model)
associates notification with layout. This change removes the old polling; it does
not implement the specification's full depth-based notification loop or improve
existing content/border/device-pixel box distinctions. Those remain limitations.

## Validation

- 21 focused host tests pass: resize observation, Blob allocation, Request state,
  preflight streaming and video frame callbacks.
- Four resize tests cover asynchronous initial delivery without polling,
  changed/unchanged geometry, cancellation, reentrant registration and exceptions.
  All four reject the previous polling implementation as a negative control.
- The MI 8 Android 15 Vulkan fixture passes all 13 checks: initial size, parent
  resize, RAF resize, stylesheet changes, display none/restore, detach/reattach,
  unobserve/reobserve, callback-driven resize and iframe initial/update sizing.
- The fixture then leaves 500 elements observed and unchanged. Continuous 1 Hz
  process/thread sampling covers the whole run. Each settled comparison uses 30
  samples starting five seconds after the result report.

| Settled fixture measurement | Before | After |
| --- | ---: | ---: |
| Mean process CPU | 2.8674% | 0.3371% |
| Peak sampled CPU | 5.0000% | 6.1224% |
| Final RSS | 234.07 MiB | 227.17 MiB |

Average idle CPU falls about 88% for this fixture. This is not a claim of an 88%
whole-browser or YouTube improvement; the isolated peak also did not decrease.

Builds use the required capped wrapper and verified V8 prebuilt. Native dependency
closure and APK signature verification pass. Clean installed artifact:
`build/codex-vulkan-resize-20261015/lumen-vulkan-resize-debug.apk`, 85,240,084 bytes.
SHA256: `e2aa988ba16551e81191bf989e2f8b28ba35f8d7586387547539da04e2969168`.
The subsequent source edit only corrects nearby comments, with no behavior change.

Evidence directory: `build/codex-vulkan-resize-20261015/`, including
`baseline-final.json`, `optimized-final.json`, `server.jsonl`, whole-run CPU/thread
`samples.jsonl`, verification outputs and actual-site screenshots.

## Remaining acceptance

The actual YouTube run initially plays (`youtube-1.png`), then displays
"Something went wrong" by the second capture; the fourth capture confirms the
error remains. The final 60 one-second samples in the settled error state average
4.2698% CPU (peak 10.1010%), with final RSS 434.70 MiB. This is still above the
0–3% idle target and does not demonstrate a meaningful whole-site improvement
over the preceding run. `youtube-final-idle.json` and `youtube-logcat.txt` retain
that evidence. Fresh profiling of remaining error-page work and the player failure
is the next step; no YouTube acceptance is claimed.

The app was stopped after testing. The owned sampler and server were stopped and
joined, and temporary reverse port 18765 was removed. The clean APK remains
installed. No temporary profiling code was added.

The full daily-browser goal remains active and Vulkan remains first. Playback/controls,
ChatGPT/Gemini/Google login, zoom and full daily-browser features remain unfinished.
OpenGL ES and software-mode optimization are deferred. No credentials were entered.
