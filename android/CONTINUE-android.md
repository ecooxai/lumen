# Android port: handoff notes

Branch: `devin/android-port-native-engine`. The engine source in `src/` is shared with desktop. Android-only code is limited to `#ifdef __ANDROID__` adapters and the `android/` project.

## Toolchain (outside the repo)
- SDK/NDK: `~/android-sdk` (NDK 28.2.13676358, platform 35, build-tools 35.0.0, emulator, AVD `lumen` x86_64).
- V8: `~/src/v8/v8` (gclient, target_os android). Build with `~/src/v8/build.sh x64|arm64`, which produces `out/android-<cpu>/obj/libv8_monolith.a`.
- Native deps: `android/build-deps.sh <abi> [deps...]` installs to `~/android-deps/<abi>`. wgpu-native prebuilt v29.0.1.1 comes from GitHub releases.

## Status
- [x] Portability shims: main.c (malloc/mach/dyld/execinfo), font.c (/system/fonts), http.c (Android UA, system CA dir), jsnative.cc (platform).
- [ ] Makefile `android` target -> liblumen.so (SDL_main), Gradle project in android/app
- [ ] Run on emulator; then sites (Google/YouTube/X), device APIs, WebGL/WebGPU/WASM
