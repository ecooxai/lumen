# Android V8 prebuilt evaluation

The user requested trying existing binaries on October 12, 2026. A source
build is not inherently required: a compatible, verifiable Android library
can replace it while retaining Lumen's engine. Do not substitute WebView.

The existing local `build/codex-deps/v8/libv8_monolith.a` contains x86-64 ELF
objects, not Android ARM64 objects. It cannot serve the phone build. The Android
source build remains intact but safety-frozen at 891/2592 while alternatives
are tested; its guard double-counted a Clang driver and its cc1 child as separate
jobs. Do not discard its outputs, change hashed inputs or automatically resume
it. See `CONTINUE.md` for the actual scope/guard state.

## Downloaded candidates

### haroel V8 14.3.92

[Release v14.3.92-15](https://github.com/haroel/v8-build/releases/tag/v14.3.92-15)
provides Android ARM64 static library and matching public headers. Downloaded
ZIPs match the GitHub release asset SHA-256 values:

- Library: `efff3aabe98d8350ed543750c3c6baddbb513d3985ce3718d29ca25420188e5f`.
- Headers: `b29196492799c368a15b873537c9520e942b5d3cce93860a9239666a9d879400`.

Its packaged `args.gn` explicitly disables internationalization, pointer
compression, sandbox and Temporal; it enables Wasm and custom libc++.
Internationalization is required by Lumen's existing Android smoke test, so
this is not a suitable browser replacement as packaged. Independent inspection
confirms ARM64 objects, V8 14.3.92.0 and an API29 build setting. The unchanged
Android smoke source compiles against its headers with NDK r28, but linking
fails: consumer `std::__ndk1` and the archive's required `std::__Cr` runtime do
not match, and the package lacks the matching custom C++ runtime. This is a
real failed link, not a device/runtime test. Supplying a matching runtime would
still not repair the disabled internationalization. Evidence and exact commands:
`build/codex-android-v8-prebuilt-probe/test-haroel/REPORT.md`.
Probe CPU samples were 0.09–95.38%, peak RSS 139.17 MiB, under the shared 140%
compilation quota. No browser or phone execution was performed.

### Rusty V8 150.4.0 / V8 15.0.245.2

[Android archive release](https://github.com/DioNanos/codex-termux/releases/tag/rusty-v8-v150.4.0)
provides a pointer-compressed, sandbox-enabled Android ARM64 archive. The
downloaded gzip matches release asset SHA-256
`6fb02f1669dbb85f7265a1cba05ac31b683295be7db9651f1db2b7b082794a09`.
The pinned upstream Rusty V8 tag references V8 commit
`ac1e23989121713ca642f6650b34deff7b686896`, whose version header is 15.0.245.2.

Matching public and custom libc++ headers were fetched from that tag's exact
submodule revisions into an isolated SDK under
`build/codex-android-v8-prebuilt-probe/rusty-sdk`. Its upstream `.gn` requests
embedded ICU/startup data and disables relative C++ vtables, unlike the current
15.6 source-build configuration. Therefore swapping only the archive under the
existing CMake output is unsafe. It needs a matching consumer configuration,
actual link checks, complete dependency checks, and Android runtime evidence.

The independent probe compiled the unchanged smoke source successfully with
the matching headers/STL. The actual link then reported 258 unresolved symbols,
all `temporal_rs_*`; no missing V8, ICU or custom C++ runtime symbols remained
after explicitly supplying NDK atomic/unwind archives. Actual objects include
embedded ICU77 data. Evidence and complete commands:
`build/codex-android-v8-prebuilt-probe/test-rusty/REPORT.md`.

The same probe found one Lumen consumer error: this V8 version's
`Module::Evaluate` returns `MaybeLocal<Value>` rather than
`MaybeLocal<Promise>`. Main added checked conversion to preserve the promise
contract across both API versions. Independent verification passes both the
candidate's syntax check and the real existing 15.6 CMake object compilation.
Main also prepared
an isolated static support crate for the exact upstream `temporal_capi=0.2.3`
with `zoneinfo64`, seeded from the upstream lockfile. Building this supporting
Rust dependency is not rebuilding V8. No missing functions are stubbed and no
smoke requirements are removed. The support build completed in 66.214 seconds;
the unchanged smoke object then linked in 1.005 seconds, with all 258 missing
symbols resolved. All 45 selected registry package versions and checksums match
the upstream lockfile; their downloaded crate checksums were independently
verified. The executable is AArch64 PIE with 16 KiB LOAD alignment and only
Android system-library dependencies. Evidence:
`build/codex-android-v8-prebuilt-probe/test-rusty-support/REPORT.md`.
Compilation averaged 120.50% CPU under the shared 140% quota. Observed peak
RSS was 234.99 MiB, but incomplete early process enumeration means this is not
a certified whole-build peak. No V8 source compilation was needed.

The first actual phone run failed before V8 initialization: Bionic rejected
the executable's TLS alignment of 8 instead of 64. This was a separate ELF TLS
requirement, not the already-passing 16 KiB LOAD alignment. Exit was 134/SIGABRT;
none of the feature checks ran. The 84 ms failed process measured 23.792% CPU
and 4,504 KiB peak RSS; these are loader-failure measurements, not V8 performance.
Evidence: `build/codex-android-v8-prebuilt-probe/phone-smoke/runtime-20261012-IiuNVx34/REPORT.md`.

Main traced this to the API28 startup object: unlike API29's `crtbegin_dynamic.o`,
it has no 64-byte-aligned TLS section. The prebuilt contains native ELF TLS,
which [Android supports only from API29](https://android.googlesource.com/platform/bionic/+/HEAD/android-changes-for-ndk-developers.md#elf-tls-available-for-api-level-29).
The isolated retry therefore links for API29 with its real NDK startup object;
it does not patch the executable or fake an alignment section. This candidate
cannot retain the current Android API28 minimum:
adopting it requires an explicit, consistent API29 minimum across native and
Gradle configuration. The maintained toolchain minimum is not changed by this
experiment.

The corrected API29 executable passes **three out of three actual runs** on
the authorized MI 8 (Android API35): V8 initialization, both ICU assertions,
Wasm execution, BigInt, promise microtasks and teardown. TLS alignment is 64,
LOAD alignment remains 16 KiB, and all 337 strong dynamic imports resolve
against API29 system libraries. Binary SHA-256:
`9eac9646053ba3be8e2f70f68d86f9854b577b6b397088cdad735357868c25de`.
Evidence: `build/codex-android-v8-prebuilt-probe/phone-api29/REPORT.md`.

Runs lasted 0.097–0.106 seconds, with wait4-measured mean CPU 91.38–94.43%
(one core is 100%) and peak RSS 24.20–24.96 MiB. The monitor sampled before
exec and before reaping, with one-second sampling enabled throughout; no run
lasted a full second. These are active initialization/smoke measurements, not
idle, browser-tab or Chrome comparisons. The corrected relink took 2.07 seconds
inside the shared 140% compile quota. No source V8 compilation, APK installation,
account access or website testing occurred. All seven frozen fallback input
hashes remain intact. Main reviewed the raw exits, assertion output and ELF
report; bounded prebuilt-smoke milestone score: **95/100**, not release readiness.

Its newer version and advertised features alone do not establish compatibility,
security maintenance, or release readiness. It is not yet selected by the
maintained Android toolchain. Only the standalone test executable was deployed
on the phone, not a browser APK; daily-browser readiness remains **20/100**.

## Acceptance gates

1. Verify publisher metadata, checksum, actual target architecture and matching
   source/header/build configuration. A matching download checksum alone is not
   a supply-chain audit.
2. Compile and link the existing `tests/android/v8_smoke.cc` without weakening
   its ICU, Wasm, BigInt, microtask and teardown requirements.
3. Check real Lumen C++ consumers, native library dependencies and 16 KiB ELF
   segment alignment; rebuild consumers for the candidate's actual ABI.
4. Run the smoke executable on the authorized phone offline, sampling CPU/RSS
   throughout. Then verify the complete browser link/APK and browser startup.
5. Only after compatibility is proven decide whether to replace the ongoing
   source build. A startup probe is not a top-site/security/Chrome benchmark.

## Selecting the prebuilt SDK

`LUMEN_V8_PREBUILT_DIR` selects a separate import in
`cmake/AndroidV8Prebuilt.cmake`; omitting it retains the source-build import.
The prebuilt import accepts only arm64-v8a, API29+ and `ANDROID_STL=none`.
It uses the archive's custom STL, absolute C++ vtables, embedded ICU77 and
Temporal support rather than mixing the original source build's ABI.
SDL's Android HID implementation separately requires C++ allocation/exception
runtime support. Its private target uses the NDK headers and imported
`libc++_shared.so`; these headers/options do not propagate into the V8 consumers.
The NDK shared runtime must be packaged alongside SDL and verified in APK
dependency closure. V8's own static C++ runtime is hidden with `--exclude-libs,ALL`.

`android/native-deps/v8-prebuilt.json` pins both archives, every public header
tree and the tested compiler binary. `tools/android/v8_prebuilt.py` verifies
the SDK at configuration time and rejects changed, missing, added/shadowing
headers, symlinks, wrong ABI and too-low API. This verifies the recorded bytes,
not the publisher's security posture. The SDK is a local dependency input,
not a bundled repository binary or automatically downloaded release.

The prepared local SDK is `build/codex-android-v8-prebuilt-sdk`. Its layout is
`librusty_v8.a`, `libtemporal_support.a`, `include/`, `libcxx/config/`,
`libcxx/include/`, `libcxxabi/include/` and `icu/unicode/`.
ICU77.1 headers come from the Rusty tag's exact ICU submodule revision
`ee5f27adc28bd3f15b2c293f726d14d2e336cbd5`; the downloaded `source/common/unicode`
archive SHA-256 is
`b2b9802a08c3203d2887d4647478803bdec09a280c11203a0f3f147a0bfba191`.
The support archive is the independently checked Temporal build described
above. Rebuilding/replacing this pinned dependency requires checking its new
bytes and repeating compatibility gates, not merely changing a checksum.

```sh
source build/codex-android-toolchain/env.sh
export LUMEN_V8_PREBUILT_DIR="$PWD/build/codex-android-v8-prebuilt-sdk"
export LUMEN_V8_COMPILER_DIR="$PWD/build/codex-android-v8/v8-15.6.75.8/third_party/llvm-build/Release+Asserts/bin"
python3 tools/android/v8_prebuilt.py --sdk "$LUMEN_V8_PREBUILT_DIR" --abi arm64-v8a --api 29
python3 tools/codex_compile.py cmake -S tests/android/v8 -B build/codex-android-prebuilt-smoke -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/android-v8.toolchain.cmake" \
  -DANDROID_NDK="$ANDROID_NDK_HOME" -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-29 -DANDROID_STL=none -DCMAKE_BUILD_TYPE=Release \
  -DLUMEN_V8_PREBUILT_DIR="$LUMEN_V8_PREBUILT_DIR" \
  -DLUMEN_V8_COMPILER_DIR="$LUMEN_V8_COMPILER_DIR"
python3 tools/codex_compile.py cmake --build build/codex-android-prebuilt-smoke --parallel 6
```

Gradle accepts the equivalent properties `lumenV8PrebuiltDir` and
`lumenV8CompilerDir` (or those environment variables) alongside the existing
SDL/dependency/wgpu properties. It selects minSdk29 and rejects x86_64 for
this SDK; the source route retains minSdk28. No V8 source checkout is required
by the prebuilt import, though the existing checkout currently supplies the
tested compiler executable. APK validation must use `--min-api 29`; the native
verifier now rejects nonempty ELF TLS when asked to validate API28.

The 13 SDK, 16 APK and 8 shared compile-limit unit tests pass. The maintained
CMake smoke, actual Gradle prebuilt configuration and full browser native link
now pass. SDL keeps its NDK shared C++ runtime private, while the V8 custom
runtime is local to libmain; Android BZip2 and MediaCodec dependencies are
explicitly linked. Cycle5 verifies 24 static gates, including all 989 strong
imports across main/SDL/wgpu/libc++, API29 provider versions, 16 KiB LOAD
alignment, and custom-runtime isolation. Evidence:
`build/codex-android-prebuilt-integration/cycle5/REPORT.md`.
This is not APK packaging or browser runtime evidence. The earlier hand-linked
phone smoke also remains a separate, narrower gate.

## Full-browser verification still required

The existing `cmake/AndroidV8.cmake` deliberately validates the source build's
GN arguments and generated public configuration. It is not bypassed with
invented GN output for this archive. The separate prebuilt import supplies
the matching configuration described above. Existing 15.6 consumer objects
are not reusable. Rebuild and test all consumers,
the complete native shared library, APK dependency closure and Activity startup
before treating this as a usable browser build.

The probes above used the former ordinary 140% quota and at most two jobs.
The user subsequently authorized all new builds to share 480% total, with
lowest priority and at most six jobs. Keep the existing V8 source build frozen
at its unchanged 320% ceiling and stricter shared parent limit. All downloaded/probe artifacts are
isolated under `build/codex-android-v8-prebuilt-probe/` and the prepared SDK;
the prebuilt route is opt-in and does not overwrite the source dependency.
