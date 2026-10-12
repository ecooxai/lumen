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

## System color glyphs

On API 31+, a system color font whose FreeType raster has no pixels falls back
to a small Java/JNI adapter. It preserves the engine's HarfBuzz glyph ID,
advance, font file, collection index and variation axes; it does not reshape
text or replace the engine with a WebView. The adapter uses Android's
[Font.getGlyphBounds](https://developer.android.com/reference/android/graphics/fonts/Font#getGlyphBounds(int,%20android.graphics.Paint,%20android.graphics.RectF))
and [Canvas.drawGlyphs](https://developer.android.com/reference/android/graphics/Canvas#drawGlyphs(int[],%20int,%20float[],%20int,%20int,%20android.graphics.fonts.Font,%20android.graphics.Paint)).
These are API 31 methods, isolated behind a version-checked Java class.
The Activity initializes the JNI bridge before starting the engine.

The bridge rejects invalid font metadata, glyph IDs and non-finite sizes,
bounds raster allocations to 1,048,576 pixels, and leaves output unchanged on
failure. Straight ARGB from Android is converted to the engine's premultiplied
ARGB. A 16-font Java LRU complements the existing native glyph cache; normal
outline/bitmap glyphs retain their FreeType path. API 28–30, downloaded color
webfonts and unsupported/oversized glyphs do not gain this fallback.

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

The device's NotoColorEmoji has COLR/CPAL tables and no bitmap strikes; the
original FreeType path produced a zero-size bitmap for U+1F600. The test-first
size/DPI regression reproduced six failures, including the original Emoji
case: `build/codex-android-color-font-test/red-cycle1/REPORT.md`.
With the color adapter, **25/25 grouped checks pass** on the API 35 phone:
script/style/family matching, invalid-input output preservation, and five color
cases covering 12/24/64 px, 2x DPI, premultiplication and cache identity.
Actual API28 browser-engine font objects and SDK36/SDL Java sources compile;
the JNI export exists and no strong API31 native font imports were added.
Evidence: `build/codex-android-color-font-green/REPORT.md`.

Five additional common emoji-sequence cases (skin tone, flag, ZWJ profession,
VS16 and keycap) found a keycap failure: 29/30 passed, with the 25 earlier
groups still green. Evidence: `build/codex-android-emoji-sequences-red/REPORT.md`.
The ASCII base and combining keycap were eligible for different fonts. The
Android shaping path now checks a bounded base-plus-combining-mark cluster
before splitting font runs and uses a fallback only if it covers the whole
cluster (variation selectors remain shaping inputs, not standalone glyphs).
An ASCII fast path avoids Unicode category queries for ordinary Latin text.
This is not a full grapheme/bidi implementation; the lookahead bounds a group
to 32 scalars. The expanded 36-group gate adds multiple keycap bases, omitted
VS16, surrounding text/font/byte-cluster preservation and a Latin accent.
**All 36 groups now pass** on the API35 phone, including keycap shaping with
and without VS16 and exact surrounding-text byte clusters. Root API28 font
objects and the real SDK36/SDL Java sources compile. Evidence:
`build/codex-android-font-visual-green/REPORT.md` and `font-report.txt`.

The maintained test Activity also saves `files/CodexLumenAndroidFontPreview.png`
and displays it. This 960x1060 chart composites actual `text_shape`/`font_glyph`
buffers, including their offsets and premultiplied color, over baseline guides.
No Java text rendering substitutes for engine glyphs in that image. Read
`FONT_PREVIEW_CLIPPED` in the native report and review the PNG itself; a
status-only screenshot is not visual glyph verification. The chart covers
12/24/48/64 px, 2x DPI, mixed scripts, and the common emoji sequences.
Main-agent visual review of the retrieved engine PNG confirms complete colored
emoji, intact baselines/descenders, legible script samples and combined skin
tone/flag/profession/heart/keycap glyphs; its clipping counter is zero.
The phone screenshot exposed the test Activity's status header under the
system bar. After a test-only window-insets fix, a fresh APK/device run still
passes all 36 groups and the new screenshot shows a fully readable header
below the system bar. The native PNG is byte-identical to the previous
verified chart. Evidence: `build/codex-android-font-insets-green/`. This is
test-Activity validation, not browser UI validation.
That final run recorded 0–2% settled CPU (mean 0.20%) and 161.34–161.47 MiB
settled RSS after a three-second startup/preview grace. Startup reached 103.92%
CPU; later hierarchy/screenshot instrumentation caused separate brief spikes.
Full timing, build quota and unchanged JNI evidence are in its `REPORT.md`.

This is a small script/style smoke test, not a comprehensive shaping, bidi,
grapheme, locale, emoji-sequence, accessibility or top-website compatibility
suite. Test-app CPU/RAM numbers are not browser-versus-Chrome measurements.
The color-adapter Activity runs sampled 0–2.02% settled CPU and
145.26–150.73 MiB RSS; startup's first measurable interval was 36.89% CPU.
See `build/codex-android-color-font-green/REPORT.md` for sampling limitations.
The expanded 36-group run with its larger preview sampled 0–2% settled CPU
and 151.14–168.95 MiB RSS, with an 18-second post-result observation. Its first
measured startup interval was 101.04% CPU while tests/rendering ran. See
`build/codex-android-font-visual-green/REPORT.md`; these separate measurements
must not be presented as browser performance or a Chrome comparison.
The subsequent build-only checkpoint uses the documented Gradle command without
an init script. Both its clean native graph and actual APK graph enforce the
two-job pool; recorded compile/link overlap never exceeds two. This rebuild
did not itself add device coverage or repair Emoji. Evidence:
`build/codex-android-font-checkpoint/inspection.json`.
