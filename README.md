# Lumen browser (early preview)

## Install on macOS

1. Download `Lumen-0.1.0-preview.dmg` from the [Releases page](https://github.com/ecooxai/lumen/releases).
2. Open the DMG and drag **Lumen** onto **Applications**.
3. Lumen is **not signed with an Apple Developer ID** (it uses a local debug signature), so macOS Gatekeeper will block the first launch with "Lumen can't be opened" or "Lumen is damaged". Remove the download quarantine flag once in Terminal:

   ```sh
   xattr -dr com.apple.quarantine /Applications/Lumen.app
   ```

   Then open Lumen normally. Alternatively: try to open it once, then go to **System Settings → Privacy & Security**, scroll down and click **Open Anyway**.

Requirements: macOS 13 or newer on Apple Silicon (arm64).

---

Lumen is a browser engine written from scratch in C. It is not a wrapper around WebKit, Chromium/Blink or Gecko: it has its own HTTP/1.1 + TLS stack, HTML and CSS parsers, DOM, layout engine and GPU renderer (wgpu). JavaScript runs on V8 through a small custom bridge, and media plays through FFmpeg with VideoToolbox hardware decoding.

Goals: high performance, low RAM use, robustness.

## Features

- Real tabs in one process, Chrome-style tab strip in the title bar
- Workspaces in a left vertical bar, each with its own tabs, icons and menu
- Built-in new tab page with Bookmarks, History and Settings
- **Lite mode** (off by default, global or per tab): releases off-screen images, video frames and audio and reloads them when scrolled into view; text and layout stay
- Per-tab CPU limiter: a tab above 80% CPU for 60 s is limited to 40%, with a top bar to lift the limit for 1, 4 or 10 hours
- Always-visible video controls (global or per tab)

- `file://` URLs open host files and folders: folders as a compact list; images, video, audio and OBJ models in a built-in viewer (images/video at 1:1 device pixels, zoom steps 10/25/50/75/100/150/200/300% via buttons, `+`/`-`/`0` or Ctrl/Cmd+wheel; drag to rotate OBJ models).
- Screen recording: `navigator.mediaDevices.getDisplayMedia()` captures the current tab (permission prompt per site) and `MediaRecorder` records it, or a camera track, to `video/mp4` (H.264 via VideoToolbox, mpeg4 fallback). Same code on macOS, iPadOS, Linux and Android. Only the tab contents are captured, not other apps or the system screen.
- New workspaces get a default icon (change it from the workspace menu).

## Build from source

```sh
brew install v8 sdl3 wgpu-native ffmpeg icu4c freetype harfbuzz openssl brotli zstd libpng jpeg-turbo webp giflib
make -j8
./build/lumen https://www.youtube.com
```

Package a DMG: `python3 tools/mkdmg.py 0.1.0-preview` (output in `dist/`).

## iPad and Mac (iOS/iPadOS port)

The same engine runs as a UIKit app on iPadOS and iPhone. Apple's
[Designed for iPad on Mac](https://developer.apple.com/documentation/apple-silicon/running-your-ios-apps-in-macos)
support provides an installation route for the same appropriately signed device IPA on Apple Silicon Macs.
`PLAT=cat` builds a separate Mac Catalyst app. HTML/CSS/layout/paint are Lumen's own, JavaScript runs in V8,
and the GPU backend uses Metal via wgpu-native.

Lumen is not on the App Store. The default `make PLAT=ios ipa` produces a **local, unprovisioned package**;
it does not supply an Apple development identity or make the app installable on a device or Mac.
Use the explicit Xcode signing/export options below for a provisioned IPA.

### JIT and jitless builds

| IPA | JavaScript | When to use |
| --- | --- | --- |
| `Lumen-jitless.ipa` | V8 interpreter only (Ignition + optimised builtins) | Default device build; does not need debugger-enabled JIT |
| `Lumen-jit.ipa` | V8 with Sparkplug, Maglev and TurboFan | Optional development build; attempts JIT and falls back to jitless if unavailable |

The optional device `JIT=1` export requires a development profile authorizing `get-task-allow`.
Use Xcode's device debugger for development; for example, launch the installed app with
`xcrun devicectl device process launch --start-stopped --device <id> com.ecooxai.lumen`, then attach Xcode's
debugger (*Debug → Attach to Process*). Lumen checks JIT availability at startup and logs either
`lumen: V8 JIT enabled` or `lumen: V8 JIT unavailable, running jitless`. A development signature does not
guarantee JIT availability on every OS/device. Simulator and Mac JIT builds also require the matching V8 variant.

### WebAssembly

WebAssembly is on in every build (same V8, no WebKit). JIT builds compile wasm to native code (Liftoff +
TurboFan); jitless builds run it in V8's DrumBrake interpreter, so it works without JIT, only slower
(3M JS->wasm calls in the iPad Simulator: 185 ms JIT, 565 ms jitless).

Tested on Mac and the iPad Simulator (JIT and jitless): `validate`, `Module`/`Instance`, `compile`/`instantiate`,
`compileStreaming`/`instantiateStreaming` (needs `application/wasm` over http(s)), `Memory`/`Table`/`Global`/
`Tag`/`Exception`, JSPI, SIMD + relaxed SIMD, threads/shared memory + Atomics, exceptions, GC, tail calls,
memory64, bulk memory, and wasm in workers (posting a `WebAssembly.Module` or `SharedArrayBuffer` to a worker).
Streaming compiles after the whole response has downloaded. `crossOriginIsolated` is `true` (page and its
workers) when a secure page is served with `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp` (or `credentialless`); `SharedArrayBuffer` is available either way
and cross-origin subresources are not yet blocked under COEP.
`LUMEN_NO_WASM_TRAP=1` turns off the wasm guard-region trap handler (jitless builds then cannot run wasm).

### On a Mac

- **Mac Catalyst** (`make PLAT=cat app`, or `JIT=1`): a native Mac `.app` built from the same sources and UIKit
  shell; JIT works directly (signed with `com.apple.security.cs.allow-jit`). Run it like the desktop build:
  `open build-cat/Lumen.app`.
  To try jitless mode with a JIT build, start it with `LUMEN_V8_FLAGS="--jitless --wasm-jitless"`; it then
  logs `lumen: V8 jitless (LUMEN_V8_FLAGS)`.
- **The same device IPA** uses the Designed for iPad on Mac route. For development or device distribution,
  include the Mac's **Provisioning UDID** in the profile, alongside the intended iPhone/iPad devices.
  Apple documents [where to find and register that UDID](https://developer.apple.com/help/account/devices/register-a-single-device/).
  Export the provisioned IPA with Xcode as below, then double-click the IPA on the Mac; Apple's installer handles
  its app wrapper ([Apple DTS guidance](https://developer.apple.com/forums/thread/766744)). Opening the raw
  `build-ios/Lumen.app` is not the IPA installation workflow. This project does not construct wrappers or rewrite
  Mach-O platform headers. Signed installation must be tested on the intended Mac, including a VM if applicable.

```sh
ios/build-deps.sh sim          # also: ios, cat  -> ~/ios-deps/<plat>
# V8 15.6 jitless monolith -> ~/src/v8ios/v8/out/{ios-simulator,ios-device,catalyst} (see CONTINUE-ios.md)
# JIT=1 builds use out/<same>-jit: same args.gn minus the v8_jitless/sparkplug/maglev/turbofan lines,
# plus v8_enable_lite_mode = false (V8 forces lite = jitless on iOS otherwise)
make PLAT=sim app              # build-sim/Lumen.app for the iOS Simulator
make PLAT=ios ipa              # local unprovisioned build-ios/Lumen-jitless.ipa
make PLAT=ios JIT=1 ipa        # local unprovisioned build-ios-jit/Lumen-jit.ipa (needs -jit V8)
make PLAT=cat app              # build-cat/Lumen.app, Mac Catalyst
make PLAT=cat JIT=1 app        # build-cat-jit/Lumen.app, Mac Catalyst with JIT
xcrun simctl install booted build-sim/Lumen.app && xcrun simctl launch booted com.ecooxai.lumen
```

### Sign and export one device IPA with Xcode

Create/import a valid Apple signing identity and a matching iOS provisioning profile in Xcode. The profile must
authorize `com.ecooxai.lumen`, include that signing certificate, and cover each intended device. Import the downloaded
profile into Xcode before exporting. Find the identity's fingerprint with `security find-identity -v -p codesigning`.

```sh
make PLAT=ios ipa \
  IOS_SIGNING_IDENTITY='<40-character certificate SHA-1>' \
  IOS_PROVISIONING_PROFILE="$HOME/Downloads/Lumen_Development.mobileprovision" \
  IOS_DEVICE_UDID='<optional Mac Provisioning UDID or iPhone/iPad UDID>'
```

Omit `IOS_DEVICE_UDID` to skip the optional single-device membership check. Both signing options are required together;
an exact, unambiguous identity name is also accepted. `ios/package-ipa.py` checks the actual arm64 iOS executable,
profile app identifier/prefix, team, expiry, certificate and optional UDID. It then archives the existing executable
and resources through `ios/signing/Lumen.xcodeproj` and runs Xcode's export operation. Xcode performs signing and
entitlement authorization. The helper does not import credentials or manually re-sign an iOS app.

The export method follows the profile: `debugging`, `release-testing`, or `enterprise`. App Store profiles use Xcode's
separate App Store distribution workflow. Export uses `thinning=<none>` to retain one universal iPhone/iPad device IPA.
The prior IPA is replaced only after a successful export; complete Xcode logs and export options remain under
`build-ios/signing/` (or `build-ios-jit/signing/`). Temporary archives and DerivedData are removed after each attempt.
Run the helper with the same options plus `--check-only` to inspect metadata without signing/exporting.

The unsigned Xcode archive path has been validated. The development Mac used for this work had no valid Apple signing
identity, so signed export and installation of the same IPA on a physical iPhone/iPad or Apple Silicon Mac remain
unverified. Metadata preflight alone does not establish successful signing or installation.

Touch: tap = click, double tap = double click, one-finger drag scrolls with momentum. Hardware keyboards and the
on-screen keyboard work for the URL bar and page text fields.

## Status

Early preview. Expect broken pages and missing features.
