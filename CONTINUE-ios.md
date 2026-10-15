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
- V8 15.6 drumbrake patch (needed to compile the wasm interpreter): in src/debug/debug-wasm-objects.cc add
  `bool DeclaresLocals() override { return false; }` to `DebugWasmInterpreterScopeIterator`.
- Also `v8_use_metagen_instance_types = false` in every out/*/args.gn (metagen libclang pass cannot find libc++ headers with use_custom_libcxx=false).
- out/catalyst = ios-simulator args with target_environment = "catalyst". Chain: sim -> device -> catalyst (logs ~/lumen-ios-logs/v8-*.log).
- wgpu-native for Catalyst: ~/src/wgpu-native @ v29.0.1.1, built with rustup stable (PATH=~/.rustup/toolchains/stable-*/bin,
  RUSTUP_TOOLCHAIN=stable to override its rust-toolchain.toml), target aarch64-apple-ios-macabi.

## Build / run
- `make PLAT=sim app` -> build-sim/Lumen.app ; `xcrun simctl install booted build-sim/Lumen.app && xcrun simctl launch --console booted com.ecooxai.lumen`
- `make PLAT=ios ipa` -> build-ios/Lumen.ipa (ad-hoc signed; re-sign with Sideloadly/AltStore/Xcode for a real iPad)
- `make PLAT=cat app` -> Mac Catalyst build of the same UIKit app.

## Code
- src/app/iosui.m implements the macui.h hooks with UIKit (alerts/action sheets, SF Symbols); macui.m stays for the AppKit build.
- main.c `LUMEN_IOS` (TARGET_OS_IPHONE): touch -> tap/double-tap clicks, one-finger pan scroll with momentum (touch_event/touch_fling),
  background lifecycle (no GPU work when backgrounded, cookies saved), borderless full-screen window, bundled cacert.pem via SSL_CERT_FILE.
- font.c picks the iOS font table at runtime when /System/Library/Fonts/Core/Helvetica.ttc exists.

## Status (iPad Air 11" M4 Simulator, iOS 26.5)
- Works: Wikipedia, Hacker News, link taps, touch scroll + momentum, URL typing (hw keyboard), new tab page, tabs,
  YouTube home/search/watch with ad + skip and MSE playback (software decode: VideoToolbox hwaccel init fails in the
  Simulator only). Google search hits reCAPTCHA from this VM IP (not solved, by design).
- CPU: idle ~1% after blocking in CFRunLoop (ios_wait_event); YouTube 1080p-ish lofi playback ~7-70% in Simulator; RSS ~470-600 MB.
- Long press (>500 ms) = right click. Focused fields call SDL_SetTextInputArea (text_area), so SDL's UIKit view
  controller slides the view up to keep the field above the on-screen keyboard (verified in Simulator).
- Typing: ~0-1% CPU between keys, single-second 25-45% spikes when the keyboard animates in/out.
- TODO: pinch zoom (needs a page zoom factor: layout viewport /zoom, page canvas scale *zoom, input coords /zoom),
  safe-area insets for iPhone notch (iPad full-screen is fine), real-device test of VideoToolbox + perf.
