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
