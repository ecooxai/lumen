# Lumen on iOS / iPadOS (+ Mac from the same UIKit build) — handoff notes

Branch `devin/ios-ipados-port` (base: `devin/linux-x11-vulkan-port` v0.2.0 + main's bundled-ICU fix).
Goal (user): run the from-scratch Lumen engine (not WebKit) on iPad, fast for daily use; same build on Mac.
App Store acceptance is not a goal (sideload / Xcode run).

## Toolchain (outside the repo)
- Dep sources: ~/src/deps (openssl 3.5.4, brotli 1.1.0, zstd 1.5.7, libpng 1.6.50, libjpeg-turbo 3.1.1, libwebp 1.6.0,
  giflib 5.2.2, freetype 2.13.3, harfbuzz 11.2.1, dav1d 1.5.1, ffmpeg 9.0.2, SDL3 3.4.16).
- `ios/build-deps.sh ios|sim|cat [dep...]` -> ~/ios-deps/<plat>/{include,lib} (static libs). wgpu-native iOS prebuilts
  (v29.0.1.1 release zips) unzipped in ~/src/wgpu-ios.
- V8 15.6.75.16 (same as the Homebrew bottle the desktop build uses): ~/src/v8ios/v8 (gclient, target_os ios+mac),
  out/ios-simulator and out/ios-device, args: jitless (iOS forbids JIT), drumbrake wasm interpreter,
  pointer compression, no sandbox, use_custom_libcxx=false, monolith -> obj/libv8_monolith.a.
  Build: `third_party/ninja/ninja -C out/ios-simulator v8_monolith` (depot_tools in ~/src/depot_tools on PATH for gn).
  gn gotcha: with v8_jitless you must also set v8_enable_sparkplug/maglev/turbofan=false explicitly.
- brew: cmake ninja meson nasm pkgconf (plus desktop deps v8 wgpu-native webp ...).
