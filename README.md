# Lumen browser (early preview)

## Linux measured source

The current [Linux measured report](docs/gpt-6-astra-pro-builtin-rubywebterm-linux-report.md) records the selected WPT scope, fixed 640-file non-URL comparison, native regression and rendering gates, source and binary identities, and resource measurements. Each suite retains its own denominator. URL work remains deferred; full WPT and whole-browser Chromium parity remain unverified.

The source distribution includes the reviewed public-history Git bundle, generators and regression tests. `SOURCE-MANIFEST.json` and `EXPORT-QUALIFICATION.json` bind the public export to the tested source inputs. `OFFLINE-RECOVERY.md` explains recovery from the included bundle. The separately packaged executable is the exact tested Linux development build and requires its matching runtime.

See the [tested Linux build and run guide](docs/gpt-6-astra-pro-builtin-rubywebterm-linux-build.md) and [runtime identity record](docs/gpt-6-astra-pro-builtin-rubywebterm-linux-runtime.json) before building or launching it.

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
The current public Linux report carries the measured validation for this source
export. Android has separate build and release gates; Linux measurements do not
establish an Android release.

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
python3 tools/codex_compile.py ctest --test-dir build/codex-core --output-on-failure -j2 -E '^(url|url_host_nul|data_url)$'
```

For the full browser, use the exact compiler, V8/wgpu include and library paths,
pointer-compression setting, runtime search paths and ICU setup in the
[tested Linux build and run guide](docs/gpt-6-astra-pro-builtin-rubywebterm-linux-build.md).
The [runtime identity record](docs/gpt-6-astra-pro-builtin-rubywebterm-linux-runtime.json)
lists the actual tested toolchain, external dependency identities and ELF imports. Prebuilt
V8/wgpu libraries, installed system libraries, and the separately pinned WPT
and Chromium installations are not included in the source snapshot. The source
archive and recorded source/resource/test/build manifest do not constitute a
hermetic build or a compiler-generated transitive dependency closure. The
published source snapshot also omits private validation runners and raw results;
complete standalone WPT reproduction requires the separate validation setup.

### Android adapter

The Android adapter builds the same C/C++ engine through SDL3; it does not embed
Android WebView. Android prerequisites and release gates remain separate. Linux results do not
establish an Android release or Chromium-level web-platform compatibility.

## Authored interactive demonstration

[Open the included demonstration](examples/gpt-6-astra-pro-builtin-rubywebterm-lumen-live-demo.html). The interactive page runs in the browser opening it. Open it in Lumen to exercise the Lumen engine. Recorded native234 validation is separate from the live values shown in any other browser. This is authored content outside WPT scoring, with separately recorded native234 observations in [the demo evidence](docs/gpt-6-astra-pro-builtin-rubywebterm-live-demo-evidence.json).
