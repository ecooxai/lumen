# Codex Android system-font adapter

Lumen retains its FreeType/HarfBuzz text engine. On Android API 29+ it asks
the platform font matcher for installed font files, collection indices and
variation axes instead of assuming every device has static Roboto files.
Symbols are loaded dynamically from `libandroid.so`; API 28 retains the
existing static-path fallback without requiring newer linker imports.
The platform contract is documented in the
[Android NDK font reference](https://developer.android.com/ndk/reference/group/font).

The adapter copies font metadata before releasing platform objects, bounds
variation axes, rejects invalid Unicode scalars, and encodes supplementary
characters as UTF-16 pairs. Matching and face construction happen under the
engine's existing font lock; a 512-entry fallback memo avoids a platform call
for every repeated character. System font instances preserve axes and apply
the requested CSS weight where the selected font exposes a `wght` axis.
Unknown CSS family names continue to the next family instead of silently
accepting Android's default sans-serif match.

## Device regression gate

```sh
source build/codex-android-toolchain/env.sh
export CMAKE_BUILD_PARALLEL_LEVEL=2
python3 tools/codex_compile.py gradle -p tests/android/fonts \
  -PlumenDepsDir="$PWD/build/codex-android-deps/prefix" \
  --no-daemon --max-workers=2 assembleDebug
```

This builds `build/codex-android-font-apk/outputs/apk/debug/CodexLumenAndroidFontTest-debug.apk`,
an isolated **test app**, not a browser APK. It runs the real engine's
`font_get`, `text_shape` and `font_glyph` code and writes its result to the
test app's private `files/font-report.txt`. Read that report with `adb run-as
org.lumen.codex.fonttest`; inspect `FONT_TEST_EXIT`, not merely successful
installation or absence of a crash. Sample the test process every second from
launch through force-stop. This gate requires API 29+ and does not prove the
API 28 fallback or full browser startup.
The maintained CMake target limits native compile/link concurrency to two jobs;
the shared compilation wrapper separately enforces the aggregate CPU quota.

Run the matcher gate inside this Activity, not an `adb shell` executable.
On the connected API 35 device the standalone matcher crashed inside Minikin
without the app process's system-font initialization. The same adapter runs
inside the test Activity. A successful standalone FreeType-only test is not
equivalent platform-context coverage.

## Current evidence and remaining work

The original device test failed Arabic, Devanagari, Thai and Emoji rendering.
After the matcher adapter, the real Activity renders the three missing scripts.
Phone file probes also confirm the hardcoded Roboto Bold/Italic/BoldItalic and
NotoSans-Regular paths are absent. Evidence is in
`build/codex-android-font-test/PROBE.md` and `apk-run.log`.

**The full font gate is not yet green.** The device's NotoColorEmoji uses COLR
color layers rather than bitmap strikes; the existing raster path returns a
zero-size bitmap for U+1F600. The regression remains enabled. Proper color-layer
rendering is still required; do not substitute a missing-glyph box or claim
emoji support. CSS family-list and variable-weight regressions caught in the
first Activity run now pass a fresh build/install/run. The current report,
`build/codex-android-font-followup/font-report.txt`, has exactly one remaining
failure: Emoji. All 13 other text/style cases, matcher input checks and the
family/style cache checks pass. Actual browser-engine Android objects also
compile; newer font APIs are not strong linker imports.

This is a small script/style smoke test, not a comprehensive shaping, bidi,
grapheme, locale, emoji-sequence, accessibility or top-website compatibility
suite. Test-app CPU/RAM numbers are not browser-versus-Chrome measurements.
The follow-up Activity run sampled 0% idle CPU over 9.01 seconds and
146.8–152.3 MiB RSS; startup's first measurable interval was 38.46% CPU.
See `build/codex-android-font-followup/REPORT.md` for sampling limitations.
The subsequent build-only checkpoint uses the documented Gradle command without
an init script. Both its clean native graph and actual APK graph enforce the
two-job pool; recorded compile/link overlap never exceeds two. This rebuild
does not add device coverage or repair Emoji. Evidence:
`build/codex-android-font-checkpoint/inspection.json`.
