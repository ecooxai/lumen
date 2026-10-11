# Android port: handoff notes

Branch: `devin/android-port-native-engine`. The engine source in `src/` is shared with desktop. Android-only code is limited to `#ifdef __ANDROID__` adapters and the `android/` project.

## Toolchain (outside the repo)
- SDK/NDK: `~/android-sdk` (NDK 28.2.13676358, platform 35, build-tools 35.0.0, emulator, AVD `lumen` x86_64).
- V8: `~/src/v8/v8` (gclient, target_os android). Build with `~/src/v8/build.sh x64|arm64`, which produces `out/android-<cpu>/obj/libv8_monolith.a`.
- Native deps: `android/build-deps.sh <abi> [deps...]` installs to `~/android-deps/<abi>`. wgpu-native prebuilt v29.0.1.1 comes from GitHub releases.

## Status
- [x] Portability shims: main.c (malloc/mach/dyld/execinfo), font.c (/system/fonts), http.c (Android UA, system CA dir), jsnative.cc (platform).
- [x] Makefile `android` target -> liblumen.so (SDL main), Gradle project in android/ (LumenActivity extends SDLActivity). Maven Central returns 429 here, so build.gradle also points at Google's Maven Central mirror.
- [x] Downloads (src/net/download.c, shared; Android also publishes into MediaStore Downloads), unit test tests/test_download.c
- [x] Host services: N.hostRequest(kind, arg, cb) -> js_host_request hook (main.c) -> android_host_request (permissions, geolocation via LumenActivity.getLocation)
- [x] navigator.geolocation, permissions.query, getUserMedia (SDL camera -> <video>.srcObject, SDL recording -> createMediaStreamSource)
- [x] File upload: SDL picker content:// URIs are copied to the cache dir by LumenActivity.importContent
- Tools: node 22 for prelude syntax checks at ~/src/node/node-v22.11.0-linux-x64/bin/node (cat src/js/prelude/*.js > f.js; node --check f.js). Device test page: tests/android/device.html.
- [ ] Run on emulator; then sites (Google/YouTube/X), device APIs, WebGL/WebGPU/WASM

## Findings for the WebGL/WebGPU branch
- Canvas 2D is only a JS stub (src/js/prelude/07z-extra.js `CanvasRenderingContext2D`, no native drawing; `getContext` in 04-tags.js returns null for webgl/webgpu).
- So WebGL/WebGPU need a native canvas surface first: a per-canvas offscreen texture in src/gpu (wgpu-native), composited like video frames (`media_frame_for`-style lookup in main.c:157). Then WebGPU JS bindings onto wgpu, and WebGL on top (GLES via ANGLE-like translation, or WebGL->wgpu).

## Status when paused (2026-10-11)
- Done: V8 x64 monolith (~/src/v8/v8/out/android-x64/obj/libv8_monolith.a), all x86_64 + arm64 deps in ~/android-deps/<abi>. FFmpeg x86_64 rebuilt with --disable-asm (inline cabac asm is not PIC).
- arm64 V8 build was stopped at 1278/4235 on user request (arm64 later): rerun `~/src/v8/build.sh arm64`.
- `make android ABI=x86_64` now fails only at link, with 4 undefined symbols:
  1. `download_save(...)` C++-mangled: src/net/download.h needs `extern "C"` guards (jsnative.cc includes it).
  2. `__real_getcwd`/`__real_realpath`: from V8's PartitionAlloc allocator_shim_android.o. Either add `-Wl,--wrap=getcwd -Wl,--wrap=realpath` (and the other shim wraps) to the link, or rebuild V8 with `use_partition_alloc_as_malloc=false`. The latter is simpler.
  3. `std::__ndk1::__hash_memory` from allocation-observer.o: V8 built with use_custom_libcxx=false against newer libc++ headers than NDK 28's libc++_static. Try rebuilding with `use_custom_libcxx=false` plus NDK sysroot headers, or provide a small `__hash_memory` shim (murmur2/cityhash like libc++ `__do_string_hash`) in src/platform/android.
- Next: fix the link, copy liblumen.so + libwgpu_native.so to android/app/src/main/jniLibs/x86_64, `./gradlew assembleDebug`, install on emulator-5554, test tests/android/device.html, Google/YouTube/X (CPU monitor per AGENTS.md).
