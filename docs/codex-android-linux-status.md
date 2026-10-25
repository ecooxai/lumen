# Codex Android/Linux portability checkpoint — 2026-10-12

## Release status

**Scope update: Android only for the current task.** Further Linux work is
deferred. The own-engine APK builds and has been installed and tested on the
authorized arm64 MI 8 running API 35. See `../agents/archive/CONTINUE-2026-10-25.md` for the current
artifact hashes and test reports; the prerequisite sections below include
historical milestones, not the latest runtime status.

**Not daily-use ready.** Android packaging passes, and the phone renders the
home page. SDL wait, sensor polling and HUD damage fixes reduce measured home
idle CPU from 101.20% to 2.47% over the latest 60-second HUD-On window
(0–6.93%, RSS229.98–232.11 MiB). This bounded idle gate passes, but
background/resume crashes with SIGABRT and the phone UI remains desktop-sized.
Pixel-equivalence diagnostics, website compatibility, security and Chrome parity
remain unverified; an idle or compilation pass does not establish them.

Current branch: `codex/android-phone-engine-port`; the original portability
branch was `codex/android-linux-daily-browser`. The original clean tree was
checkpointed as `c742180` before the branch was created.

## Implemented scope

- Portable process CPU/RSS and heap statistics replace unconditional Mach calls.
- Linux compilation fixes for signals, clock functions and math linkage.
- Fontconfig family and character-coverage fallback with a bounded 512-slot
  lookup cache; Linux no longer depends on Debian-specific font directories.
- Default workspace icons, including a cached platform-independent grid icon.
- Local file MIME recognition, bounded reads, escaped compact directory pages,
  rejection of remote file hosts, encoded NUL paths and special files.
- Local image/video/audio preview HTML with native playback controls. Images and
  video use discrete 10/25/50/75/100/150/200/300 percent sizes; 100 percent divides
  source dimensions by devicePixelRatio. No polling timer is used.
- Synchronous networking now consumes its request; asynchronous workers retain
  requests until completion. This fixes the sanitizer-detected request leak.
- HTTP redirects now reject non-HTTP(S) targets before following them. A local
  server regression demonstrated private-file disclosure through all five common
  redirect statuses before the fix. This narrow repair is not an origin-policy
  audit: direct subresource/worker/local-file access still requires review.
- Host CMake tests and a shared-engine Android SDLActivity/CMake/Gradle adapter.
- Deterministic embedded prelude/icon generation for out-of-tree builds.

The preview script's unit tests use a mock DOM. Actual default-renderer desktop
and 390px-viewport tests now verify all eight zoom buttons for image and video,
audio/video play and pause, visible glyphs, and audio seeking. Android, high-DPI
output and touch operation remain unverified. Audio has no
intrinsic pixel dimensions; its controls remain responsive rather than zoomed.
Buffered file reads currently reject files larger than 64 MiB. Streaming large
media, OBJ/glTF previews, PDF/document viewers and OS handlers remain work.

## Native prerequisites

C11/C++20 compilers, CMake 3.22+, Python 3, SDL3, V8 with matching headers and
build flags, wgpu-native, FFmpeg, ICU, FreeType, HarfBuzz, OpenSSL, zlib, Brotli,
Zstd, PNG, JPEG, WebP and GIF libraries. Linux also needs Fontconfig and fonts.
The optional viewer-script test requires Node with `node:assert/strict`.

```sh
cmake -S . -B build/codex-native -DCMAKE_BUILD_TYPE=Release
cmake --build build/codex-native -j4
```

Use `-DLUMEN_DEPS_DIR=/absolute/dependency/prefix` for a custom dependency prefix.
`LUMEN_V8_POINTER_COMPRESSION` must match the actual V8 build. This checkout's
initially lacked standalone V8 and wgpu-native development files. Matching
dependencies now live under ignored `build/codex-deps`; the full native CMake
build passes. See `codex-linux-native-dependencies.md` for pinned versions,
archive hashes, exact configuration and runtime ICU setup. Node's older V8
headers are not a supported substitute.

## Android prerequisites and adapter build

JDK 17, Gradle 8.13, Android SDK 36, CMake 3.22.1, NDK r28c
(`28.2.13676358`), and matching SDL3 native/Java sources are required. There is no
checked-in Gradle wrapper yet. The adapter pins AGP 8.13.0, minSdk 28, targetSdk
36, with arm64-v8a by default and optional x86_64 via `lumenAbis`. These are explicit configuration choices, not claims
that the whole engine has been compiled or validated on those targets.

All native dependencies must be built for the target ABI and supplied as:

```text
native-deps/
  arm64-v8a/include/...
  arm64-v8a/lib/...
  x86_64/include/...
  x86_64/lib/...
```

```sh
cd android
gradle -PlumenSdlDir=/absolute/SDL3 \
       -PlumenDepsDir=/absolute/native-deps :app:assembleDebug
```

CMake rejects missing ABI prefixes instead of silently linking host libraries.
No dependencies are downloaded by this adapter. Building shared dependencies
also requires checking their packaging, ABI, runtime search paths and licenses.
The NDK, Gradle and CMake toolchain is now verified under ignored
`build/codex-android-toolchain`; see `codex-android-build.md`. The Android wgpu
dependency and the engine's GPU translation unit compile and link, but the full
engine dependency prefix and APK are not ready. Device/emulator deployment and lifecycle/keyboard/touch tests have not
run. Android Storage Access Framework, URI grants, runtime permissions,
MediaProjection consent/foreground-service handling and activity-result bridges
are not implemented here. Android does not grant unrestricted host-file access.

## Verification evidence

Logs are local ignored artifacts, not committed binary screenshots:

- `build/codex-verification`: initial red tests and ASan's 814-byte request leak.
- `build/codex-verification2`: core Make tests, Python embedding tests and
  five pre-viewer CTest cases pass; file/workspace ASan+UBSan pass after the fix.
- Own-engine directory raster tests at 1280×800 and 390×844 completed in
  approximately 150 ms and 120 ms. Their average CPU was 93.4% and 96.4%, and
  peak RSS was 51.24 MiB and 30.25 MiB respectively. This is short rendering
  work, **not idle browser usage**. Initial screenshots exposed missing fallback
  glyphs, which became the next regression target.
- Test processes were sampled at startup, each second while alive and completion;
  `wait4` CPU time/maximum RSS supplements subsecond runs that interval sampling
  cannot resolve. Compile-resource measurements are not browser measurements.
- `build/codex-final-test`: Make tests, all six CTest cases, viewer mock-DOM tests,
  and ASan/UBSan file/viewer tests pass. The new mixed-script font assertion was
  observed failing before the fix and passing afterward. That checkpoint's full
  build was blocked by missing `v8.h` and `webgpu.h`, since resolved below.
- Final directory screenshots were inspected at desktop and mobile sizes: the
  missing folder glyphs are repaired and no clipping was observed. Final render
  CPU/RSS: desktop 90.2% / 51.6 MiB; mobile 98.8% / 30.8 MiB, each about 0.22 s.
  Those figures are short CPU raster/PNG workloads, not browser idle benchmarks.

Initial checkpoint self-assessment: **35/100 toward the complete daily-browser goal**.
The final testing agent scored its bounded five-group assignment 80/100 because
the full browser build remains blocked. Neither score certifies a release.

- `build/codex-v8-probe`: subsequent complete Linux CMake build passes, including
  `lumen` and `jsrun`; six CTests pass. Missing `<algorithm>` includes in two C++
  translation units were repaired after failing syntax probes. A matching V8
  smoke verifies JS, Wasm, ICU, microtasks and teardown. Its scoped test score
  is 95/100, not a daily-browser readiness rating. GUI testing is in progress.
- `build/codex-redirect-test`: observed RED private-file disclosure via
  301/302/303/307/308, then GREEN rejection with empty response bodies. Explicit
  local-file reads and relative HTTP redirects remain working. Seven core CTests
  and file/redirect ASan+UBSan checks pass. The active redirect test took 7.6 ms,
  87.4% average CPU and 13.6 MiB peak RSS; the sanitized run took 25.9 ms,
  101.2% CPU and 25.2 MiB. These subsecond values are not idle measurements.
- `build/codex-media-controls-test`: nine CTests, integrated Node viewer tests,
  and scoped icon/viewer ASan+UBSan checks pass.
- `build/codex-media-gui/patched-default`: 32 actual image/video zoom cases pass
  at desktop and 390px content width. Paused media averages 2.05–2.19% CPU;
  the session's peak RSS is 268.65 MiB. Dummy audio does not certify sound output.
  Residual defects: controls vanish on the 64px-wide video at 10%; the separate
  CPU-renderer run hangs after audio starts and requires forced cleanup.
  Scoped agent score 85/100; not an Android assessment.
- `build/codex-local-chrome-benchmark/REPORT.md`: interrupted for Android-only
  priority after one Chrome and two Lumen offline runs. The matched static page
  uses a 1200×800 content viewport at DPR 1. Chrome mean tree PSS is 428.05 MiB;
  Lumen is 142.85 and 195.44 MiB. Idle CPU is 0.00% versus 0.11% and 0.21%.
  The fixture meets the half-RAM target, but lower CPU is not demonstrated.
  Unequal warm-up, different graphics paths, missing second Chrome run and no
  GPU-memory measurement limit this evidence. No Android/top-site savings claim.
  Scoped agent score 80/100. Overall daily-use assessment remains about 40/100;
  Android readiness is substantially lower and unverified.

## Unpassed release gates

| Gate | Current evidence |
| --- | --- |
| Complete native Linux build and GUI | Default-renderer local checks pass; CPU fallback audio hang; further Linux work deferred |
| Android build, install, relaunch, pause/resume | Not tested; dependency/toolchain work remains |
| YouTube, Facebook, ChatGPT, Claude, Gemini, X | Not runtime-tested in this checkpoint |
| Google, Bing, Gmail, Wikipedia | Not runtime-tested in this checkpoint |
| Login, cookies, navigation, forms, downloads | Not certified; no Chrome cookies imported |
| WebGL, WebGPU, Wasm conformance and performance | Not certified by presenter/engine compilation |
| Camera, mic, location, uploads, display capture | Platform integration/permission tests outstanding |
| Site isolation, origin/file-access policy, TLS/security audit | Release blockers; do not use personal accounts yet |
| Idle 0–3% CPU, ChatGPT typing below 10% | Local desktop idle passes; Android/ChatGPT unmeasured |
| At most 50% Chrome RAM and lower CPU | Limited offline desktop memory result only; Android/top sites and lower CPU unproven |

For a valid comparison, use isolated disposable profiles, identical viewport,
device scale, tabs/content, cache state and interaction scripts. Measure the
whole process tree, report both RSS and proportional memory where available,
include browser/service/GPU children, and distinguish loading, idle, typing,
scrolling and video. Capture both browsers' screenshots at matching milestones.
Do not compare a static raster harness to Chrome's complete runtime.

Next priorities: produce the real same-engine arm64 Android build, install on the
connected phone, and test lifecycle, keyboard, touch and secure networking. Audit
origin/local-file access before any login testing; implement Android platform
bridges and run real site/graphics/media and Android Chrome comparisons. The user
goal remains active; a 95/100 release score is not justified.
