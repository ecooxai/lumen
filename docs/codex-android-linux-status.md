# Codex Android/Linux portability checkpoint — 2026-10-12

## Release status

**Not daily-use ready. No Android APK or full Linux browser was produced in this
checkpoint.** The existing engine remains in place. Building individual engine
components does not establish website compatibility, security, or Chrome parity.

Branch: `codex/android-linux-daily-browser`. The original clean tree was
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
- Host CMake tests and a shared-engine Android SDLActivity/CMake/Gradle adapter.
- Deterministic embedded prelude/icon generation for out-of-tree builds.

The preview script's unit tests use a mock DOM. Playback, zoom, high-DPI output
and touch operation still require real Lumen runtime verification. Audio has no
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
host is missing standalone V8 and wgpu-native development files; Node's older V8
headers are not a supported substitute. Full build failures are recorded, not
converted into passing runtime results.

## Android prerequisites and adapter build

JDK 17, Gradle 8.13, Android SDK 36, CMake 3.22.1, NDK r28c
(`28.2.13676358`), and matching SDL3 native/Java sources are required. There is no
checked-in Gradle wrapper yet. The adapter pins AGP 8.13.0, minSdk 28, targetSdk
36, and arm64-v8a/x86_64. These are explicit configuration choices, not claims
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
The current host has SDK tools but no installed NDK or Android engine dependency
prefixes. Device/emulator deployment and lifecycle/keyboard/touch tests have not
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
  observed failing before the fix and passing afterward. Main's object compiles;
  the full build still fails for missing `v8.h` and `webgpu.h`.
- Final directory screenshots were inspected at desktop and mobile sizes: the
  missing folder glyphs are repaired and no clipping was observed. Final render
  CPU/RSS: desktop 90.2% / 51.6 MiB; mobile 98.8% / 30.8 MiB, each about 0.22 s.
  Those figures are short CPU raster/PNG workloads, not browser idle benchmarks.

Checkpoint self-assessment: **35/100 toward the complete daily-browser goal**.
The final testing agent scored its bounded five-group assignment 80/100 because
the full browser build remains blocked. Neither score certifies a release.

## Unpassed release gates

| Gate | Current evidence |
| --- | --- |
| Complete native Linux build and GUI | Blocked by missing V8/wgpu development dependencies |
| Android build, install, relaunch, pause/resume | Not tested; dependency/toolchain work remains |
| YouTube, Facebook, ChatGPT, Claude, Gemini, X | Not runtime-tested in this checkpoint |
| Google, Bing, Gmail, Wikipedia | Not runtime-tested in this checkpoint |
| Login, cookies, navigation, forms, downloads | Not certified; no Chrome cookies imported |
| WebGL, WebGPU, Wasm conformance and performance | Not certified by presenter/engine compilation |
| Camera, mic, location, uploads, display capture | Platform integration/permission tests outstanding |
| Site isolation, origin/file-access policy, TLS/security audit | Release blockers; do not use personal accounts yet |
| Idle 0–3% CPU, ChatGPT typing below 10% | Unmeasured without a running full browser |
| At most 50% Chrome RAM and lower CPU | No matched benchmark; no savings claim |

For a valid comparison, use isolated disposable profiles, identical viewport,
device scale, tabs/content, cache state and interaction scripts. Measure the
whole process tree, report both RSS and proportional memory where available,
include browser/service/GPU children, and distinguish loading, idle, typing,
scrolling and video. Capture both browsers' screenshots at matching milestones.
Do not compare a static raster harness to Chrome's complete runtime.

Next priorities: obtain matching native dependencies; complete the Linux runtime
build; audit origin/local-file access before any login testing; implement Android
platform bridges with tests; then run real site/graphics/media and matched Chrome
benchmarks. The user goal remains active; a 95/100 release score is not justified.
