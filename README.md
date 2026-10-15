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

The same engine runs as a UIKit app on iPadOS (and iPhone). The identical `.ipa` also runs on Apple Silicon Macs as an
"iPad app", and `PLAT=cat` builds a Mac Catalyst app from the same sources. No WebKit is used: HTML/CSS/layout/paint
are Lumen's own, JavaScript is V8 in jitless mode (iOS forbids JIT for normal apps), GPU is Metal via wgpu-native.

Lumen is not on the App Store. Sideload the IPA with AltStore, Sideloadly or Xcode (free Apple ID works; the
signature lasts 7 days).

### JIT and jitless builds

| IPA | JavaScript | When to use |
| --- | --- | --- |
| `Lumen-jitless.ipa` | V8 interpreter only (Ignition + optimised builtins) | Always works, no debugger needed. JS-heavy sites (YouTube, ChatGPT) are 2-5x slower |
| `Lumen-jit.ipa` | V8 with Sparkplug, Maglev and TurboFan | Full speed when iOS allows JIT; otherwise it falls back to jitless by itself |

iOS only gives an app JIT memory while a debugger has attached to it, so with `Lumen-jit.ipa`:

1. Sideload it with a **development** signature (AltStore/SideStore, Sideloadly or Xcode all keep `get-task-allow`).
2. Enable JIT for each launch with one of:
   - **StikDebug** (on the iPad, no computer): install StikDebug + its pairing file, open it, pick *Lumen* under
     *Enable JIT*. Lumen starts with JIT on.
   - **Xcode** (iPad connected to a Mac): *Window → Devices and Simulators*, or `xcrun devicectl device process
     launch --start-stopped --device <id> com.ecooxai.lumen` and attach Xcode's debugger (*Debug → Attach to Process*).
   - **SideStore / AltStore**: long-press Lumen → *Enable JIT* (AltStore needs AltServer running on the same Wi-Fi).
3. Lumen logs `lumen: V8 JIT enabled` or `lumen: V8 JIT unavailable, running jitless` at start-up (Console.app).

On iOS 26 with the newest chips Apple has restricted debugger-enabled JIT further; if StikDebug cannot enable it,
Lumen just runs jitless. The Simulator and the Mac builds always have JIT.

### On a Mac

- **Mac Catalyst** (`make PLAT=cat app`, or `JIT=1`): a native Mac `.app` built from the same sources and UIKit
  shell; JIT works directly (signed with `com.apple.security.cs.allow-jit`). Run it like the desktop build:
  `xattr -dr com.apple.quarantine Lumen.app; open Lumen.app`.
- **The IPA itself** runs on Apple Silicon Macs as an iPad app. macOS only launches iPad apps with a real Apple
  signature: install it from Xcode (*Devices* → your Mac → *+* with the IPA after re-signing it with your Apple ID,
  e.g. via Sideloadly's re-sign), or use **PlayCover** (open the IPA, it re-signs and runs it). JIT works on the Mac
  without a debugger. iPad apps cannot run inside macOS virtual machines.

```sh
ios/build-deps.sh sim          # also: ios, cat  -> ~/ios-deps/<plat>
# V8 15.6 jitless monolith -> ~/src/v8ios/v8/out/{ios-simulator,ios-device,catalyst} (see CONTINUE-ios.md)
# JIT=1 builds use out/<same>-jit: same args.gn minus the v8_jitless/sparkplug/maglev/turbofan lines,
# plus v8_enable_lite_mode = false (V8 forces lite = jitless on iOS otherwise)
make PLAT=sim app              # build-sim/Lumen.app for the iOS Simulator
make PLAT=ios ipa              # build-ios/Lumen-jitless.ipa for iPad / iPhone
make PLAT=ios JIT=1 ipa        # build-ios-jit/Lumen-jit.ipa (needs the -jit V8 builds)
make PLAT=cat app              # build-cat/Lumen.app, Mac Catalyst
xcrun simctl install booted build-sim/Lumen.app && xcrun simctl launch booted com.ecooxai.lumen
```

Touch: tap = click, double tap = double click, one-finger drag scrolls with momentum. Hardware keyboards and the
on-screen keyboard work for the URL bar and page text fields.

## Status

Early preview. Expect broken pages and missing features.
