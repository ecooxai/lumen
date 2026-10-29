# Android Vulkan media notifications and replay, 2026-10-15

The full daily-browser goal remains active, with Vulkan first. These changes
remove background media polling and repair an end-of-video replay race. They
are shared media/runtime changes tested with the Vulkan renderer; they do not
establish complete YouTube, authentication or Chrome-feature acceptance.

## Changes

Previously, attaching any media element started a 50 ms JavaScript interval
which polled all attached players and allocated state objects even when every
player was paused. Native media state now has a main-thread generation, updated
after the media pump when readiness, pause/end/seek/wait/error state, duration or
size changes. Decoder notifications and media commands wake the native pump.
JavaScript observes that generation and retains its existing 50 ms position
sampling only while an element is playing. A paused player still receives
loading, error, duration and seek-completion events without a periodic JS timer.
The last detach removes the subscription. NaN duration does not create repeated
notifications. A seek invalidates the snapshot even when it completes within a
single pump, so paused seek completion is not lost.

The device regression fixture exposed a separate existing replay failure:
`play()` after `ended` could immediately end again. Stream EOF from the previous
seek epoch was treated as current. EOF is now valid only for its decoder epoch.
The decoder also rechecks its epoch after demux teardown, preventing an old EOF
from being published and waiting indefinitely on a newer seek. The native replay
regression failed before this fix; the phone replay check changed from failure
to success.

Position updates retain the previous cadence; this is not a claim of full HTML
media event conformance. The [HTML media standard](https://html.spec.whatwg.org/multipage/media.html)
distinguishes periodic playback time updates from state-change events.

## Validation

- 21 focused host JS tests pass: media events, video-frame callbacks,
  ResizeObserver, Request state and fetch preflight.
- Native ASan/UBSan presentation/state regression passes, including unchanged
  paused state, duration changes, same-pump paused seek, stale EOF on replay,
  errors, hidden frames and frame accounting.
- Native SDL lock/device lifecycle regression passes. Its synthetic EOF now
  explicitly distinguishes the old seek epoch from current decoder completion.
- MI 8, Android 15, Vulkan: all eight media-event checks pass. Includes paused
  load readiness, playback time updates, pause idle, paused seek, ended, replay,
  paused network error and paused MediaSource duration changes.
- All eleven existing real-decoded-video frame callback checks pass on the final
  APK, including pause, seek, cancellation, reload and presentation counts.
- Every runtime run sampled process and thread CPU once per second.

Controlled fixture: one paused video and 24 attached idle MediaSources. Final
30 settled samples average **0.2017% CPU**, peak 2.0202%, RSS 245.37 MiB. The
previous clean build averages **1.8632%**, peak 8.9109%, RSS 255.29 MiB. Mean CPU
falls about 89% in this fixture; this is not a whole-site or whole-browser gain.
The separate frame callback fixture averages 0.3680% CPU over 30 settled samples,
peak 4%, RSS 248.81 MiB. Final measurement windows have no cadence errors.

Clean APK: `build/codex-vulkan-failure-20261015/lumen-vulkan-media-events-debug.apk`.
SHA256: `93812de67b6b515e61704256139d028e7f2801dcfc5b8490524e8691d7a9d14d`.
Native ELF/dependency and APK v2 signature verification pass. Build used the
capped wrapper and verified V8 prebuilt. Temporary native profiling and preload
injection were removed before building this APK.

Evidence: `build/codex-vulkan-failure-20261015/`, including `media-server.jsonl`,
`media-before-idle.json`, `media-after-idle.json`, `frame-regression-idle.json`,
`host-js-tests.txt`, APK checks, screenshots and complete CPU/thread samples.

## Earlier diagnostic evidence

The initial temporary diagnostic run reproduced YouTube error 5 at about 59 s.
The JavaScript stack shows YouTube's own error handler calling `stopVideo` and
then `HTMLMediaElement.load`; that reset is a consequence, not proof of the
original cause. A later run with a detailed-error listener did not reproduce the
error and played past 1:53. A pause then left controls visible at 3 and 7 seconds.
This does not establish prompt control response or reliable long playback.

A deliberately paused run confirmed mediaPoll and its property allocations in
the V8 CPU profile. YouTube's own recursive object copying, JSON processing and
GC remain larger contributors. The profiler itself adds substantial CPU during
sampling/dumping; those periods are not valid idle acceptance measurements.
`first-profile.log` is incomplete because the first diagnostic used buffered
stderr. `second-profile.log` is a playback/mixed-state profile, not an error-idle
profile. `paused-profile.log`/`paused-profile-final.log` contain the explicit pause
and complete later paused profile. None of these diagnostic APKs is the final
installed build.

## Final real-site result and next work

The clean final build plays YouTube initially (`youtube-final-45.png`) but shows
"Something went wrong" by the 90-second screenshot. Final 30 settled error-page
samples average **6.2643% CPU**, peak **87%**, RSS **423.42 MiB**. The spike is
included, not discarded. This remains above the idle target, and no real-site
CPU improvement is claimed. The `youtube-controls-*` captures were taken after
the error and are not evidence of working player controls.

Next capture the detailed YouTube error on a failing run, and profile the
remaining allocation/JSON work without including profiler overhead in idle
measurements. Earlier detailed-error runs happened to keep playing; they did
not reveal the cause. The final media change removes one demonstrated idle
cost but does not fix all page activity.

App stopped, owned sampler/server joined and the test reverse port removed.
Temporary private preload/profile files were removed. The clean APK remains
installed. No credentials entered. ChatGPT/Gemini, Google login, zoom and full
daily-browser feature acceptance remain unfinished; OpenGL/software deferred.
