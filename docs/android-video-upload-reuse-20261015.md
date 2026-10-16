# Vulkan video upload reuse candidate (2026-10-15)

Update: device checks now recorded in [integrated Android report](android-device-integrated-20261015.md).
That report supersedes the historical installed-build/test-window status below.

Status: implemented, host tests pass, Android artifact built and verified.
Phone runtime and performance verification are pending. The daily-browser goal
remains active; Vulkan remains first.

## Change and scope

The GPU presenter previously uploaded all video pixels on every repaint,
including scrolling around a paused video and display refreshes between decoded
frames. It now retains the serial of the frame uploaded to its existing RGBA and
NV12 textures. A repeat serial skips pixel uploads while still updating position,
size, color conversion uniforms and draw commands.

The media pump already assigns globally increasing presentation serials.
media_image_serial exposes the serial of the currently presented visible image;
unknown and hidden placeholder images return zero. This avoids relying on image
addresses, which allocators may reuse after seeking or changing videos. Both
the video-only and ordinary render paths supply this identity to the presenter.

Zero serials preserve unconditional uploads. Texture replacement invalidates the
stored serial. RGBA and NV12 textures track identities separately. No additional
pixel copy, frame retention or texture allocation is introduced; the presenter
stores two uint64_t values. This is a reduction in redundant upload work, not
evidence of a measured device CPU, FPS or RAM improvement yet.

## Verification

All compilation used tools/codex_compile.py and the shared host quota. The old
V8 source build remained frozen.

- GPU upload suite: 14/14 pass under UBSan. The new test invokes the actual
  gpu_present_frame through GPU API stubs for both RGBA and NV12. Each format
  performs 60 scrolling repaints with zero additional video uploads after the
  initial upload. Geometry uniforms and video-before-page draw order still update;
  page ring pixels remain correct. New serials at reused addresses upload changed
  bytes. Zero serials upload each time; odd-size texture replacement invalidates
  reuse. Color matrix changes update uniforms without recopying planes.
- Media presentation test: ASan/UBSan passes. Checks stable identity while paused,
  new identity on seek, rejection of retired/hidden images, and new identities
  after player destruction/recreation, alongside existing presentation/drop,
  replay and state-notification checks.
- Android page-transfer suite: 19/19 pass under UBSan. Its video case initially
  failed because it still expected the pre-01be9b2 fallback. The extracted render
  transfer block was verified identical to HEAD, so this was a stale expectation,
  not caused by the new change. The test now checks direct upload, preserved page
  pixels and forwarding the video to the presenter.
- Android build, native ELF/dependency validation, v2 signature validation and
  git diff whitespace checks pass.

These are unit-level host checks using API stubs and the actual media pump;
they are not browser runtime or GPU-driver benchmarks. No phone interaction
occurred and no Android CPU/RAM measurement was made this turn.

## Artifact and next test

Candidate:
`build/codex-video-upload-reuse-20261015/lumen-vulkan-video-reuse-debug.apk`

SHA256: `c1bf28eb2f039c1f0fb5a0d5eae938148e42bb5cbd665ba687ff82ee537bdea9`

The artifact includes the pending page V8 wakeup and Wasm module-clone changes.
It has NOT been installed. The phone still has the streaming checkpoint
42af2d0940212a04e9f24e41c726f6887d5b4977be59bf40406c3adf113fa54f.
Artifacts include build.log, page-transfer-tests.log, apk-native-verification.json
and apk-signature.txt. All owned test/build sessions finished and were joined;
no owned runtime monitor/server or adb reverse was created this turn.

The separate untouched-phone window request remains pending after the phone
changed tabs during an earlier measurement. Once resolved, finish the page-wakeup
and module-clone device checks first. Use the clone candidate ebc5d77a... as the
baseline for this GPU change, since it includes the same pending JS changes.
Compare identical local paused-video scroll gestures, then playing video with
seeks, source changes, fullscreen and app background/resume. Verify both NV12 and
RGBA, visual video/page alignment, Vulkan selection, and whole-process/all-thread
CPU sampling at 1 Hz throughout. Measure settled idle and actual presentation
intervals before claiming a phone performance improvement.

YouTube controls/playback acceptance, ChatGPT/Gemini/Google login, zoom, memory
reduction and full browser Wasm conformance remain unfinished. OpenGL ES and CPU
software optimization remain deferred according to the user's priority.
