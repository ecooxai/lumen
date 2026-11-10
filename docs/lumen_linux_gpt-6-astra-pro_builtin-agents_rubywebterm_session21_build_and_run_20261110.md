# Current Linux build and isolated launch guidance — session21

This document describes the existing verified Ruby configuration. It was prepared from current CMake source/cache entries, named installed-package metadata and recorded dependency provenance. **No new configure, build or browser run was performed for this document.** The accepted2104 public source export was verified by source/blob restoration, not independently rebuilt. Each later export requires its own release verification record. A new build is a new executable and must be validated before replacing an accepted binary.

## Distinct tested, public and rebuilt identities

Accepted2104 was tested from canonical engine commit `3edec1c75b9e332f16e2e5b0943eb881c3c00e1f`. Its reviewed public source export is commit `54999c2cfe6b159167b89c1b5b98427e9eaf5370`, tree `29698665cc0ee8211d2490a4e18793ef498ec16c`, on the separate approved public ancestry. These are different commits with different provenance roles. The accepted immutable2104 executable was built from the canonical tested source; the public export was verified by exact allowed source blobs and offline Git restoration, not by a separate rebuild.

Candidate2105 is accepted for the selected work and required preservation gates from canonical engine commit `20a7dd4b54f1a4e587168f10875e43f307a76fb8`, with immutable executable SHA256 `2d38085828206f384aeafc3afc38c63009fda483c55f0fcd057e0df9255ffcb3`. The independent core seal is SHA256 `a6cccfbf608ca25d3f9cb3cd776519ff3ca8fe7a78fc0daa2e2f7c9da7f29186`; the final owned-window seal is SHA256 `442e781d175d99450344cfeff73c33985d8fb15b165c6a750760b7c3d47fc553`, completed2026-11-10T16:24:59.641836UTC and separately bound to root's visual review. This acceptance does not certify all WPT or whole-browser98/100 quality.

For any public source export, use its accompanying release provenance for the separate public commit identity. That identity, and any later documentation commit, must never replace20a7 in this executable's build record. **A newly rebuilt executable is unqualified even when its source matches an accepted commit** until its own source/binary/dependency provenance and required validation are established. This recipe does not promise a byte-identical rebuild.

## What remains current in the old documents

The public `docs/Linux-candidate200.md` and `docs/build-dependencies200.json` are historical records for candidate200; the latter records settings originally captured for186. Their eight listed pkg-config versions, Clang22.1.6, CMake4.3.4 and V8 15.6.75.8 still match current named metadata. Their old source identities, test counts and release results do not describe2104 or2105. They are not a complete current dependency lock.

The canonical October12 `docs/codex-linux-native-dependencies.md` still gives the matching V8/wgpu layout and flags. Its profile instruction suggesting an isolated HOME is superseded: use the supported XDG variables below and leave HOME unchanged. Its early six-CTest result is historical.

Current CMake minimum is3.22; this host uses CMake4.3.4, Clang/Clang++22.1.6 and GNU Make4.4.1. C is11 and C++ is20. The accepted configuration is Release (`-O3 -DNDEBUG`), pointer-compressed V8, with `-DV8_CPPGC_MICROTASK_QUEUE` retained to match the previously verified bottle configuration. No claim that this macro is universally necessary is inferred.

## Prerequisites

Keep the V8 headers and libraries together under `build/codex-deps/v8/bottle/v8/15.6.75.8`. Keep the bottle's sibling private libraries and `libexec/icudtl.dat`; do not substitute Node or another V8 ABI. The existing recorded V8 bottle archive SHA256 is `1dd28bd65bf4bae16e8b78e6f2ad15ffc712041179a5922ca8a32451ef59f968` (historical acquisition pin, not re-downloaded here).

Use wgpu-native29.0.1.1 under `build/codex-deps/wgpu`, with headers in `include/webgpu` and `lib/libwgpu_native.so`. Recorded release archive SHA256 is `95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a`; the currently checked shared library SHA256 is `be0d8270dd9d78e1039eeb413ee21be8a89776a752e56d6b0f57eefbbc2d749c`. Dependencies and their licenses are external to the source export; no portable dependency bundle is claimed.

Current source requires development headers/libraries for SDL3, ICU, OpenSSL, zlib, Brotli, zstd, FreeType, HarfBuzz, Fontconfig, FFmpeg (`avcodec`, `avformat`, `avutil`, `swscale`, `swresample`), PNG, JPEG, WebP plus demux, GIF, EGL/OpenGL ES, libdatachannel and Opus. Threads/dl/m are system prerequisites. This build also finds libpsl, which enables full public-suffix behavior; omitting it changes behavior and does not reproduce the tested configuration. Python3 generates embedded browser assets. CMake resolves these directly; package names differ between distributions.

Observed versions include SDL3.4.10, ICU78.3, OpenSSL3.6.3, HarfBuzz14.2.1, Fontconfig2.18.1 and libpsl0.21.5. FreeType's pkg-config version string is26.6.20; it is recorded as that module's metadata rather than renamed to a presumed upstream release. The table below records all queried module version strings. libdatachannel has no pkg-config version on this host, but CMake resolves its header/library; GIF is similarly reported by detected header/library rather than an invented version. Historical libcurl metadata still matches8.21.0, but libcurl is not a direct requirement in current CMake's network library list.

The native font tests use the external WPT files `fonts/Ahem.ttf` and `css/css-values/resources/ChTestZeroWidthZero.woff`. They are not included in the public source archive. Preserve the pinned WPT checkout when reproducing the full regression suite.

## Concrete recipe for this host, without replacing an accepted output

Run from the intended source checkout after provisioning the matching dependencies. This example uses a **new build directory**; do not reconfigure the frozen `build/codex-native` while its candidate gates are active. On Ruby, all configure/build work must use the existing shared quota wrapper and at most six jobs. It fails closed when the required quota cannot be established; do not bypass that protection.

```sh
cd /mnt/p3/project/browser/lumen-android/lumen-android
LUMEN_BUILD_V8="$PWD/build/codex-deps/v8/bottle/v8/15.6.75.8"
LUMEN_BUILD_WGPU="$PWD/build/codex-deps/wgpu"
LUMEN_BUILD_WPT="/mnt/p3/project/browser/wpt"
mkdir -p "$PWD/build"
LUMEN_REBUILD_DIR=$(mktemp -d "$PWD/build/codex-native-rebuild-session21.XXXXXX")
python3 tools/codex_compile.py cmake -S . -B "$LUMEN_REBUILD_DIR" \
  -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/usr/bin/clang \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DLUMEN_BUILD_BROWSER=ON -DLUMEN_BUILD_TESTS=ON \
  -DLUMEN_V8_POINTER_COMPRESSION=ON \
  -DLUMEN_DEPS_DIR="$LUMEN_BUILD_V8" \
  -DV8_INCLUDE="$LUMEN_BUILD_V8/include" \
  -DWGPU_INCLUDE="$LUMEN_BUILD_WGPU/include/webgpu" \
  -DNATIVE_wgpu_native="$LUMEN_BUILD_WGPU/lib/libwgpu_native.so" \
  -DSDL3_DIR=/usr/lib/cmake/SDL3 \
  -DCMAKE_CXX_FLAGS=-DV8_CPPGC_MICROTASK_QUEUE \
  -DCMAKE_BUILD_RPATH="$LUMEN_BUILD_V8/libexec;$LUMEN_BUILD_WGPU/lib" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath-link,$LUMEN_BUILD_V8/libexec" \
  -DLUMEN_PSL_INCLUDE_DIR=/usr/include \
  -DLUMEN_PSL_LIBRARY=/usr/lib/libpsl.so \
  -DLUMEN_TEST_AHEM_FONT="$LUMEN_BUILD_WPT/fonts/Ahem.ttf" \
  -DLUMEN_TEST_ZERO_FONT="$LUMEN_BUILD_WPT/css/css-values/resources/ChTestZeroWidthZero.woff"
python3 tools/codex_compile.py cmake --build "$LUMEN_REBUILD_DIR" --parallel 6
```

These are host-specific development RPATHs, not a portable install recipe. Other hosts must supply matching ABI dependencies and adapt installation paths. The test suite must then run through the project's supported monitored gate, preserving all222 registered identities and all prior named passes; running a bare CTest count does not reproduce that acceptance evidence. Supplemental sanitizer targets are explicitly excluded from the ordinary build and retain their scoped instrumentation limits.

## Run the accepted2105 immutable executable with a fresh profile

Use `build/codex-native/lumen-astra-i2105`, SHA256 `2d38085828206f384aeafc3afc38c63009fda483c55f0fcd057e0df9255ffcb3`. Accepted2104 remains separately preserved at its own immutable path and SHA256 `0e7f3d63259731afedafe2fd717394b49e4fe525d34d9c12b1c437dd5e6789d9`. The following manual example was not executed by this task:

```sh
cd /mnt/p3/project/browser/lumen-android/lumen-android
mkdir -p "$PWD/build"
lumen_2105_profile="$(mktemp -d "$PWD/build/lumen-2105-profile.XXXXXXXX")"
mkdir -m 700 "$lumen_2105_profile/config" "$lumen_2105_profile/data" "$lumen_2105_profile/cache" "$lumen_2105_profile/downloads" "$lumen_2105_profile/tmp"
env XDG_CONFIG_HOME="$lumen_2105_profile/config" \
    XDG_DATA_HOME="$lumen_2105_profile/data" \
    XDG_CACHE_HOME="$lumen_2105_profile/cache" \
    XDG_DOWNLOAD_DIR="$lumen_2105_profile/downloads" \
    TMPDIR="$lumen_2105_profile/tmp" \
    LUMEN_NO_SESSION=1 \
    LUMEN_ICU_DATA="$PWD/build/codex-deps/v8/bottle/v8/15.6.75.8/libexec/icudtl.dat" \
    ./build/codex-native/lumen-astra-i2105 about:blank
```

ICU data is10822000 bytes, SHA256 `319501bd0fdf9c9cb9926c0d0fd64ed95961118af700fbb4342981f088e73e8c`. No unsupported --profile flag, HOME override, captured process environment or guessed existing profile is used. The XDG mechanism is source/library-derived; the deleted test profiles did not retain a child-path inventory. Leave the new profile for the user to retain or remove after closing the browser.

The existing display0 process resolves to an out-of-project sibling release and remains untouched. This manual example neither terminates nor repoints it. The accepted isolated Xvfb captures are separate from any user desktop/account workflow.

## Named installed-package metadata at this checkpoint

These are queried module version strings, not a portable dependency lock or a guarantee that an arbitrary distribution packages the same ABI.

| pkg-config module | Observed version |
|---|---|
| sdl3 | 3.4.10 |
| openssl | 3.6.3 |
| icu-uc | 78.3 |
| freetype2 | 26.6.20 |
| harfbuzz | 14.2.1 |
| fontconfig | 2.18.1 |
| libcurl | 8.21.0 |
| libpsl | 0.21.5 |
| zlib | 1.3.1.zlib-ng |
| libbrotlidec | 1.2.0 |
| libbrotlicommon | 1.2.0 |
| libzstd | 1.5.7 |
| libavcodec | 62.28.102 |
| libavformat | 62.12.102 |
| libavutil | 60.26.102 |
| libswscale | 9.5.102 |
| libswresample | 6.3.102 |
| libpng | 1.6.58 |
| libjpeg | 3.1.4.1 |
| libwebp | 1.6.0 |
| libwebpdemux | 1.6.0 |
| opus | 1.6.1 |
| egl | 1.5 |
| glesv2 | 3.2 |

