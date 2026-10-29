# Android Vulkan video frame callbacks, 2026-10-15

## Change

The browser exposed `requestVideoFrameCallback` as a no-op returning zero, and
`getVideoPlaybackQuality` always returned zero frame counts. Pages could detect
these APIs and then wait forever for a callback. They now use the native video
presentation pump rather than a timer that invents frames.

The pump records the last selected frame's media PTS, original dimensions,
monotonic presentation timestamp, presentation generation and frame counts.
When several overdue frames are selected in one pump, only the final frame
counts as submitted for composition; earlier selections count as dropped.
Low-memory hidden placeholders count as dropped and do not issue callbacks.
Quality counters reset with a new media load, not a seek. Callback presentation
counts remain increasing across loads for the element.

Callbacks run before RAF, once per new frame, with cancellation supported even
from an earlier callback in the same batch. Reentrant registrations wait for
another frame. First registration on an already paused video, or registration
after a gap without callbacks, also waits for a new frame; the regression test
failed before this correction. Callback errors do not discard subsequent callbacks. Pending
callbacks retain their elements; completed registrations retain only WeakMap
state. Native generation changes schedule delivery, so a pending callback does
not introduce a periodic idle timer. Playback quality returns readonly snapshots.
Optional decode-duration and deprecated corrupted-frame statistics are omitted;
expected display time is a best-effort estimate, not a hardware vsync measurement.

References: [video-frame callback specification](https://wicg.github.io/video-rvfc/)
and [Media Playback Quality](https://w3c.github.io/media-playback-quality/).
This repair does not establish complete media API conformance or YouTube acceptance.

## Validation

- Five focused JS callback tests cover metadata, no-frame waits, cancellation,
  reentrancy, exception handling, element isolation and readonly quality snapshots.
- Native ASan/UBSan test exercises the actual media pump: three overdue frames
  produce one presentation and two drops; pause changes neither counters nor
  generation; seek while paused presents a new frame; hidden placeholders do not
  become callbacks; a new player starts with zero counters.
- Existing SDL lock/device lifecycle test passes after the native changes.
- All 17 focused JS tests pass (callbacks, Request state, preflight streaming,
  base URL cache).
- MI 8 Android 15 Vulkan fixture: old implementation fails with a callback timeout;
  new clean build passes all eleven checks. Includes actual decoded 640x360 video,
  callback metadata, cancellation, pause, seek, quality reset and callback count
  continuity across reload. Fixture remains paused with a pending callback afterward.
- Continuous 1 Hz process/thread sampling: final paused fixture averages 1.4663%
  CPU, peak 7.9208%, over 30 samples; RSS 257.04 MiB. Mean meets the idle target.

Commands:

```sh
node --test tests/test_video_frame_callbacks.js tests/test_request_state.js tests/test_fetch_preflight.js tests/test_document_base_uri.js
python3 tools/codex_compile.py python3 tests/test_media_frame_state.py
python3 tools/codex_compile.py python3 tests/test_media_audio_lock.py
python3 tests/fetch_preflight_server.py
# Reverse device tcp:18765 and open http://127.0.0.1:18765/video-frames.
```

Evidence is under `build/codex-vulkan-playback-20261015/`: `frame-server.jsonl`,
`frame-revised-idle.json`, full CPU samples and native/signature verification.
Clean installed APK: `lumen-vulkan-frame-callback-debug.apk`, 85,240,084 bytes,
SHA256 `f9234ce0f980e4d6ae9812d60d8eb8f71ca60d4a1fa8a86c44b1ea038fd8d41d`.
The native dependency closure and APK v2 signature checks pass. Builds used the
capped wrapper and verified V8 prebuilt. Temporary controls diagnostics are absent
from this source and APK. No credentials were entered.

## Real-site result and remaining work

The first clean callback build still fails YouTube acceptance: Big Buck Bunny
initially plays, then the page displays "Something went wrong". In the settled
error state, 30 one-second samples average 4.8413% CPU (peak 13.1313%), with final
RSS 423.20 MiB. This remains above the 0–3% idle target. Evidence:
`frame-error-final.png`, `frame-error-idle.json`.

A fresh 20-second native profile records 1,477 samples without loss. Concurrent
major GC accounts for 23.22% of sampled CPU, and main-thread incremental GC for
7.45%; JSON parsing/stringification and SDL event pumping also appear. These are
fractions of sampled CPU, not percentages of a full CPU core, and overlapping
call chains must not be added. The symbolized report was generated against the
matching native library before rebuilding: `frame-error-resolved.txt`. This
supports further investigation of JS allocation and scheduling on the error
page rather than a claim that Vulkan composition causes this idle cost.

The final APK additionally corrects stale callback delivery after a registration
gap. Its local fixture is tested separately from the YouTube run above. Neither
the callback repair nor the local idle result proves YouTube playback/controls
fixed. ChatGPT/Gemini, Google login, zoom and full daily feature acceptance remain
unfinished. OpenGL and software-mode optimization remain deferred.
