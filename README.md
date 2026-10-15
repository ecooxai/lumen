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

Lumen is not on the App Store. Sideload `Lumen.ipa` with AltStore, Sideloadly or Xcode (free Apple ID works; the
signature lasts 7 days).

```sh
ios/build-deps.sh sim          # also: ios, cat  -> ~/ios-deps/<plat>
# V8 15.6 jitless monolith -> ~/src/v8ios/v8/out/{ios-simulator,ios-device,catalyst} (see CONTINUE-ios.md)
make PLAT=sim app              # build-sim/Lumen.app for the iOS Simulator
make PLAT=ios ipa              # build-ios/Lumen.ipa for iPad / iPhone
make PLAT=cat app              # build-cat/Lumen.app, Mac Catalyst
xcrun simctl install booted build-sim/Lumen.app && xcrun simctl launch booted com.ecooxai.lumen
```

Touch: tap = click, double tap = double click, one-finger drag scrolls with momentum. Hardware keyboards and the
on-screen keyboard work for the URL bar and page text fields.

## Status

Early preview. Expect broken pages and missing features.
