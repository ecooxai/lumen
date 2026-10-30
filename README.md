# Lumen browser (early preview)

## Linux source checkpoint205 — 30 October2026

This reviewed source snapshot retains18/18 selected parser-options WPT files (1961/1961 assertions) and140/140 non-URL native tests. A separate ordinary script-lifecycle cohort passes32/35 files and61/64 assertions; current Chromium156 passes35/35 and64/64. Two authored document-write regressions and broader incremental-parsing gaps remain under active work. Full WPT and98/100 whole-browser conformance are not certified. URL conformance work remains deferred.

See [the measured checkpoint](docs/lumen_gpt-6-astra-pro_builtin-rubywebterm_checkpoint205.json) and `EXPORT-QUALIFICATION.json` for exact source/binary identifiers, cohort denominators and limitations. This source export has not been rebuilt independently and makes no portable-binary claim.

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

## Status

Early preview. Expect broken pages and missing features.

## Linux and Android development

Linux runs Lumen's native C/C++ engine with V8 and wgpu-native. In the full
development checkout, current accepted binaries, exact source hashes, measured
compatibility and remaining work are recorded in the dated `AGENTS.md` handoff
and `devsummary.md`. These private task and handoff documents, including
`agent/task.md`, are intentionally omitted from public source snapshots.
The [portability status and test plan](docs/codex-android-linux-status.md)
retains earlier Linux/Android milestones; its historical results are not the
current engine's validation.

### Linux prerequisites and build

The core build requires C11/C++20 compilers, CMake 3.22 or newer, Python 3,
threads, ICU, OpenSSL, zlib, Brotli and Zstandard development libraries.
The native core test suite also uses Git for an isolated local dependency-plan
fixture; it does not download or build Android dependencies.
The full browser additionally requires SDL3, matching V8 headers/libraries and
build flags, wgpu-native, FreeType, HarfBuzz, FFmpeg, PNG/JPEG/WebP/GIF libraries,
EGL/OpenGL ES, Fontconfig, libdatachannel and Opus. Install system fonts covering
the scripts and symbols you use. Optional JavaScript viewer controls need Node.

The commands below use the supplied build-budget wrapper. It requires an active
systemd user manager plus `chrt` and `nice`, shares a CPU quota equal to 60% of
host logical CPUs, and fails closed if that quota cannot be established.
On the managed development host, all compilation must use this wrapper.

Native core tests can run without V8, SDL3 or wgpu-native:

```sh
python3 tools/codex_compile.py cmake -S . -B build/codex-core -DLUMEN_BUILD_BROWSER=OFF
python3 tools/codex_compile.py cmake --build build/codex-core -j2
python3 tools/codex_compile.py ctest --test-dir build/codex-core --output-on-failure -j2
```

For the full browser, provision the external dependencies first, then configure
their prefix and build:

```sh
python3 tools/codex_compile.py cmake -S . -B build/codex-native -DCMAKE_BUILD_TYPE=Release -DLUMEN_DEPS_DIR=/absolute/dependency/prefix
python3 tools/codex_compile.py cmake --build build/codex-native -j2
```

`LUMEN_V8_POINTER_COMPRESSION` must match the supplied V8 build. See
[the native dependency record](docs/codex-linux-native-dependencies.md) for the
retained dependency versions, archive hashes and runtime ICU setup. Prebuilt
V8/wgpu libraries, installed system libraries, and the separately pinned WPT
and Chromium installations are not included in the source snapshot. The source
archive and recorded source/resource/test/build manifest do not constitute a
hermetic build or a compiler-generated transitive dependency closure. The
published source snapshot also omits private validation runners and raw results;
complete standalone WPT reproduction requires the separate validation setup.

### Android adapter

The Android adapter builds the same C/C++ engine through SDL3; it does not embed
Android WebView. The earlier portability document records its separate Android
prerequisites and remaining release gates. Linux results do not establish an
Android release or Chromium-level web-platform compatibility.
