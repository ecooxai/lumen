# Codex Android phone build

## Current scope

Android only, per the user's latest instruction. Use the existing Lumen engine;
do not substitute WebView. Branch: `codex/android-phone-engine-port`.
Checkpoint `8cceb05` preserves the previous shared-engine work.

An authorized MI 8 with Android API 35 and arm64-v8a is connected. **Home now
renders, but the idle CPU gate fails.** The hidden font JNI entry point is
fixed, with 39 unit tests and 23 APK packaging checks passing. Cycle2 measures
101.20% CPU and 234.19 MiB RSS during five complete idle intervals; the hot
thread is SDLThread. With the SDL wait fix, cycle3 drops to 9.40% CPU and
232.17 MiB RSS, still above the 0–3% target. Controls remain desktop-sized with
status-bar overlap. Fixture and interaction tests were stopped at the idle
gate. See `build/codex-android-browser-phone/cycle3/REPORT.md`. Device connection is not
evidence of browser functionality. Do not import personal profiles or log in
until the engine's origin/file-access security blockers are repaired.

The separate [Android font adapter and device gate](codex-android-fonts.md)
now exercise real engine text on the phone. Only the font-test APK has passed runtime tests;
the browser's native library now links with the verified prebuilt V8, and APK
packaging passes all 23 gates. All 36 grouped font/emoji checks now pass;
see `agents/archive/CONTINUE-2026-10-25.md` for the device evidence and remaining browser gates.

The active route is the [pinned Android prebuilt V8](codex-android-v8-prebuilt.md),
not the frozen source compilation documented historically below. It requires
API29 and arm64-v8a. Gradle bounds Ninja compilation to six jobs, serializes
native links, and limits LLD to two threads; the shared CPU wrapper remains
required because concurrency limits are not CPU quotas.

## Android event-wait regression

The pinned SDL 3.4.10 Android timed-wait loop inserts a poll sentinel on every
iteration. In that version, inserting any event wakes the Android lifecycle
semaphore. The sentinel is then discarded by the event read, but its wake
remains: the supposed blocking wait consumes that wake and immediately loops.
The actual phone's SDLThread runs at approximately one full core on the static
home page. Non-root LLDB attach and one resumed snapshot locate the native
thread in this wait loop's event-read and event-pump paths; these are stack
snapshots, not statistical profiling percentages. Evidence is in
`build/codex-android-idle-profile/cycle1/symbolized-stacks.txt`. The corrected
APK phone gate shows a reduction to 9.40% idle CPU but still fails after five
complete intervals; no 60-second idle pass is claimed. Residual native and
Activity-thread work needs separate profiling.

`cmake/AndroidSdlWait.cmake` generates `codex_android_SDL_events.c` in the build
directory, replacing only the Android blocking loop's sentinel-producing pump
with a normal pump. The initial poll setup, polling batch sentinels, lifecycle
handling and cross-thread wake mechanism are retained. This is a Lumen-local
modification, not an unmodified upstream SDL source. The dependency checkout
and its license notice remain intact. The exact input SHA256 is required;
review the fix explicitly when changing SDL revisions. Only the generated
translation unit gets its original event-header include directory.

`tests/test_android_sdl_wait.py` compiles the actual wait function against a
deterministic Android event/lifecycle model. Before the fix, four blocking-wait
cases fail and four immediate/poll cases pass; the empty timed wait performs
1,000,001 pumps. All eight cases pass with the generated-source fix. This
model is not a phone CPU or lifecycle acceptance test.
`tests/test_android_sdl_patch.py` checks the exact one-site transformation,
unchanged dependency bytes, stable generated timestamps and input rejection.
To test the generated function, set `LUMEN_TEST_SDL_EVENTS` to the generated
source path. All tests/builds still run through `tools/codex_compile.py`.

## Android Back adapter

`LumenActivity` routes API 33+ committed Back gestures through the platform
`OnBackInvokedDispatcher` and legacy Back through the same native request path.
The modern callback registers at default priority and unregisters on activity
destruction. See the [Android dispatcher reference](https://developer.android.com/reference/android/window/OnBackInvokedDispatcher).
This handles committed navigation, not predictive page-preview animations.
SDL hardware Back is trapped and handled without key-repeat navigation.

A bounded, mutex-protected 32-request queue wakes the engine thread without a
polling timer; Java never mutates page state. Before engine readiness or after
shutdown, requests are rejected. Back dismisses the resource popup, cancels
address editing, clears page editing focus, traverses history, then backgrounds
the task at the root rather than destroying its tabs. Android IME activation
tracks focus transitions instead of reopening a dismissed keyboard every frame;
tapping the same focused control explicitly requests the keyboard again.

`tests/test_android_back.c` covers action priority, bounded/reentrant/concurrent
dispatch, shutdown and focus transitions. Actual gesture cancellation, keyboard
dismissal/reopening, same-document history, background/resume and activity
recreation still require APK/device tests. Do not treat these native policy
tests or Java compilation as proof of working phone navigation.

## Verified local toolchain

Latest host allowance (October 12, after the prebuilt smoke test): **all new
builds share 60% of logical CPUs, 480% total on this eight-core host**. The
wrapper's ordinary and compatibility `--v8` modes both use
`codex-lumen-compile.slice` and inherit SCHED_IDLE/nice19. Use at most six
compiler jobs and Gradle `--no-daemon`; fewer jobs remain appropriate for
memory-heavy work. This supersedes the historical 140%/320% split below.
The original V8 source build is still safety-frozen; do not resume it while
integrating the tested prebuilt. Global host quotas and unrelated work are
unchanged.

Preparation artifacts are ignored, machine-local files under
`build/codex-android-toolchain`. The test agent verified NDK r28c
(`28.2.13676358`), Gradle 8.13, CMake 3.22.1, SDK platform 36 and OpenJDK 21.0.11.
Java source compatibility remains 17. Existing SDK license acceptance was reused;
no new licenses were accepted blindly. Official archive checksums and resource
measurements are recorded in that directory's `REPORT.md`.

```sh
source build/codex-android-toolchain/env.sh
python3 tools/android/build_deps.py --ndk "$ANDROID_NDK_HOME" --abi arm64-v8a --dry-run
python3 tools/codex_compile.py python3 tools/android/build_deps.py --ndk "$ANDROID_NDK_HOME" --abi arm64-v8a --jobs 2 --execute
```

The dry-run command prints a plan without creating files or downloading anything.
The following quota/recovery account is historical; use the latest 480% shared
budget above for new builds. Dependency, Gradle and test compilation shared a 140% CPU ceiling (1.4 logical
cores) through `tools/codex_compile.py`. This uses one systemd user slice, not a
separate quota per command. The latest explicit V8 allowance is
**40% of all host logical CPUs (320% total on this host)**. Use
`tools/codex_compile.py --v8` for V8 itself. New launches use a separate slice
capped at 320% on this eight-core host, with at most six compiler jobs, Linux
SCHED_IDLE and nice 19. Other builds retain their original shared 140% cap.
The wrapper uses a relative niceness adjustment of 39 to reach nice 19 even
when launched from a negative-niceness process; children inherit idle scheduling.
It requires Linux cgroup CPU control and a working systemd user manager; quota
setup failure must not fall back to an uncapped build. Scoped commands retain
the caller's environment, working directory and exit status. The quota is
140000 microseconds per 100000-microsecond period for ordinary builds and
320000/100000 for new V8 launches on this host, so very short instantaneous
samples can exceed 140%; monitor aggregate `cpu.stat` deltas over time.
Keep non-V8 compiler concurrency at two or less for bounded memory.
For iteration 7, the tighter quota was applied directly to
`run-p3601367-i28741414.scope`, without restarting Ninja. Its parent retains
560000/100000 because the existing owning monitor checks that value. The child
enforces 320000/100000. Do not launch another V8 wrapper or change that parent
until this build finishes; the old monitor's displayed parent quota is not the
effective limit. The original host idle-priority policy is restored, superseding
the temporary normal-priority experiment. This does not change compiler inputs.
**Live verification interruption (2026-10-12, 13:11:38 PDT):** another host
configuration change reset both Lumen parent quotas to 200%, and the watcher
moved the build out of its scope. Its safety monitor stopped the existing
Ninja/workers at 826/2592 steps. The scope no longer exists; the global
cpulimit parent also has a 200% cap. The 320% wrapper/watcher mock tests pass,
but sustained live acceptance failed. See
`build/codex-android-v8-forty-percent/REPORT.md` and `agents/archive/CONTINUE-2026-10-25.md` before any
recovery. Do not restart the build or simply SIGCONT the guard-latched monitor.
**Historical recovery verified:** the same build resumed in
`/cpulimit.slice/codex-lumen-v8-recovery.scope` with a 320% local ceiling below
the unchanged shared 200% host ceiling. A new guard supervises it; the original
monitor retains child exit-status ownership. See
[build guard and limitations](codex-android-build-guard.md) and
`build/codex-android-v8-recovery-live/REPORT.md` (91 seconds, 10/10 bounded gates).
This supersedes the disappeared user scope described above, not its compiler
inputs. The current host watcher also preserves this exact recovery scope only
when its leaf quota is 320000/100000. Do not raise other host quotas.
Use Gradle `--no-daemon`; a build must not delegate compilation to an existing
daemon outside the capped slice.
On this host, `/usr/local/lib/cpulimit/cpulimit_watcher.py` previously moved
compilers out of explicitly limited scopes into its general-purpose slice.
Its local integration now preserves this project's two slices only when their
ancestor `cpu.max` matches the corresponding wrapper quota; other workload mappings remain
unchanged. The original is backed up alongside it as
`cpulimit_watcher.py.codex-before-lumen-cap`. Verify process cgroup membership
across multiple watcher scans, not just immediately after launch, if that host
service is upgraded or reconfigured. This host adaptation is not APK code.
The latest watcher also recognizes the new 40% V8 parent quota, retaining the
earlier 42% and legacy 70% parents during the child-capped transition. Its
pre-change backup is `cpulimit_watcher.py.codex-before-v8-40`. No other mappings
change.
Execution requires the exact NDK and an executable Android API 28 compiler.
It uses repository-local temporary files, bounded compilation concurrency,
pinned SDL/vcpkg commits, and a release-only Android triplet. Required host tools
include Python, Git, curl, zip, unzip, tar and a C/C++ build toolchain. This host
initially lacked `zip`; the compile agent prepared a verified local distro tool
instead of changing the host installation.

The manifest builds ICU, TLS/compression, text shaping, image codecs and FFmpeg.
FFmpeg is pinned to 8.1.2#3, matching the engine's tested API generation. GPL and
nonfree optional codecs and FFmpeg command-line applications are not requested.
This is **foundation dependency preparation, not an APK build command**.
Static-library licensing/relink requirements must be reviewed before release.

## Source-route build history

- V8 15.6.75.8, same generation as the existing engine. GN generation passes
  but Android compilation is safety-frozen in `build/codex-android-v8`. A complete
  archive, C++ ABI integration, ICU data packaging and startup remain to verify.
- wgpu-native v29.0.1.1 now builds for aarch64-linux-android in
  `build/codex-android-wgpu/prefix`. The actual engine's `gpu.c` cross-compiles and
  links with all 49 imported symbols resolved. Android ELF architecture, system
  dependencies and 16 KiB load-segment alignment pass. This does not yet prove
  Vulkan/GLES presentation or WebGPU functionality on the phone.
- Link the complete native engine against target-ABI dependencies only, package
  all required runtime libraries/data, build the APK, then validate its manifest,
  signature and packaged native symbols before installation.

The dependency recipe exposes `prefix/arm64-v8a` after a successful foundation
build, and preserves the matching SDL Java/native source checkout. It does not
pretend that missing V8 or wgpu artifacts are optional.

### V8 compiler and C++ ABI gates

The NDK-only compiler configuration failed before compilation because V8's
host snapshot tools need a host compiler-rt archive absent from the NDK.
Use V8's pinned Clang `llvmorg-24-init-7747-g62397f8b-29`, including its Android
runtime package. Both archives were checked against the exact SHA256, size
and object generation in V8's DEPS before extraction:

- Linux compiler: `8039b5c9e7375df3a8082761cd1220223f90344b81d829c949f69224350d9d47`.
- Android runtime: `b93725013ccd4f91d5ae40afc1a1b495e07c3be645ddbd7a3b48a7281fcecff8`.

The next actual compile exposed NDK libc++19's missing `std::atomic_ref`.
V8 now builds its exact pinned libc++/libc++abi sources, not a replacement
atomic implementation or mismatched runtime archive. Native GN is pinned to
`127dd2a6d582528d6d61c4d838dc17385ef31abd`; Android-only cpu_features and catapult
resources also match V8 DEPS. Preparation, flags and resource measurements live
under `build/codex-android-v8`, with iteration-numbered reports.

The frozen source configuration uses Android API28, NDK r28c's sysroot, monolithic PIC
V8, embedded startup/ICU data, custom libc++, and
`v8_generate_external_defines_header=true`. Lumen's C++ consumers must use the
same Clang, custom `__Cr` STL configuration, relative-vtable ABI and generated
`v8-gn.h` with `V8_GN_HEADER`. `cmake/android-v8.toolchain.cmake` selects that
compiler while retaining the NDK Android sysroot and target atomic/unwind
libraries. `cmake/AndroidV8.cmake` imports the monolith and matching consumer
configuration, rejects mismatched GN settings, and supplies its ICU to the
Android browser's base library. Full archive linking and device startup remain
pending; compiler configuration is not evidence of a working browser.
Do not link those consumers with NDK STL headers or mix V8's ICU78.2 objects
with the foundation ICU78.3 archive. The V8 sandbox remains disabled in this
development probe; this is not a release security configuration.

The pinned source needs the local build fix
`android/native-deps/patches/v8-15.6.75.8-gc-info-guard.patch` when generated
external definitions are enabled. `gc-info.cc` originally tests
`CPPGC_ENABLE_OBJECT_SECTION_GCINFO` before including the header that defines
it. Host snapshot compilation then sees incompatible declarations and fails
with `EnsureGCInfoIndexTrait` undeclared. The patch only moves that guard below
the includes; host object-section GC and Android table-based GC keep their
existing settings. It does not disable either collector or weaken atomics.
Apply once to the exact pinned source, before compiling:

```sh
V8_SOURCE="$PWD/build/codex-android-v8/v8-15.6.75.8"
patch --directory="$V8_SOURCE" --strip=1 --fuzz=0 --forward \
  < android/native-deps/patches/v8-15.6.75.8-gc-info-guard.patch
```

The original `src/heap/cppgc-internal/gc-info.cc` SHA256 is
`91b4eb02dcb27e38fbdae4941096edf148e9ee15b065b462c5464e7a21690495`;
after applying the patch it is
`dea9529e89597ebf86c61ae8ce7882b0245a3e579e3b20f580f26bdc1e6de870`.
An already-patched tree needs no second application or clean rebuild. Keep
this patch alongside the source pin when reproducing the Android build.

`tests/android/v8_smoke.cc` is the pending target-link/runtime gate: V8 startup,
embedded ICU date/number formatting, Wasm execution, BigInt, microtasks and
teardown. It is a dependency test, not an APK or browser compatibility test.

Build this gate separately from the browser so compiler, STL and startup
failures remain identifiable. Configuration and object compilation can run
while the real V8 archive is still building; the executable link must wait for
that archive, not use a placeholder:

```sh
source build/codex-android-toolchain/env.sh
V8_SOURCE="$PWD/build/codex-android-v8/v8-15.6.75.8"
python3 tools/codex_compile.py cmake -S tests/android/v8 -B build/codex-android-v8-smoke -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/android-v8.toolchain.cmake" \
  -DANDROID_NDK="$ANDROID_NDK_HOME" -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_shared \
  -DLUMEN_V8_DIR="$V8_SOURCE" -DCMAKE_BUILD_TYPE=Release
python3 tools/codex_compile.py cmake --build build/codex-android-v8-smoke --target v8_smoke_objects --parallel 1
python3 tools/codex_compile.py cmake --build build/codex-android-v8-smoke --target codex_android_v8_smoke --parallel 1
```

Gradle requires `-PlumenV8Dir="$V8_SOURCE"` in addition to `lumenSdlDir` and
`lumenDepsDir`. It forwards the custom toolchain automatically. Set
`-PlumenWgpuDir="$PWD/build/codex-android-wgpu/prefix"` for the separately built
GPU library. Each requested ABI needs its own real V8 output under
`out/android-arm64` or `out/android-x64`; supporting an ABI in configuration
does not mean its dependencies or APK are built. Foundation libraries still
use the NDK shared STL behind their C interfaces; coexistence with the
monolith's hidden custom C++ runtime must be verified in the final ELF/APK.

## Native APK inspection before installation

After a real APK exists, inspect its packaged libraries, not just loose build
outputs:

```sh
python3 tools/android/verify_apk.py android/app/build/outputs/apk/debug/app-debug.apk \
  --ndk "$ANDROID_NDK_HOME" --abi arm64-v8a
```

This gate requires `libmain.so`, `libSDL3.so` and `libwgpu_native.so` for every
requested ABI. It checks ELF64 machine/type, 16 KiB LOAD alignment, matching
SONAME, absence of RPATH/RUNPATH/text relocations and recursive packaged-library
dependency closure against the NDK's public system-library list at API28.
For example, a dependency on `libc++_shared.so` must resolve inside the APK,
not on the build host. Missing/extra ABIs, duplicate native entries, malformed
paths and oversized libraries fail. Inspection streams one library at a time
into a temporary directory beside the APK; `--work-dir` overrides this location.
The default readelf is the NDK Linux host tool; `--readelf` permits another
matching LLVM tool path. Repeat `--abi` when checking a multi-ABI package.

This is **not** signature, manifest, ZIP alignment, imported-symbol/API-version
or runtime verification. Keep those as separate required gates, using the
SDK's `apksigner`, `aapt2`, and `zipalign` plus actual installation/startup.
Unit fixture APKs are synthetic test inputs, never distributable artifacts or
evidence that the browser has been packaged successfully.

## Android HTTPS adapter

The prior networking initialization treated Android's certificate directory as a
PEM file. The new platform adapter obtains accepted root certificates from the
application's default `TrustManagerFactory`, exports DER through JNI, and stages
them in an OpenSSL store. It never disables peer or hostname verification and
does not fall back to build-host certificates when the Android callback fails.
An import failure leaves the destination trust store unchanged. Initialization
happens before network workers start; no polling or periodic Java calls are used.

OpenSSL chain verification is still used. Importing accepted roots does not
prove complete equivalence to Android's per-host `checkServerTrusted` policy,
certificate transparency or dynamic trust updates. These remain runtime/security
validation work, not certified features.

## Android link-opening adapter

The activity accepts HTTP/HTTPS `ACTION_VIEW` links on cold launch and queues
warm-launch links for new tabs in the existing native engine. `singleTask`
avoids creating a second SDL runtime. URI validation rejects other schemes,
missing authorities, malformed input and links exceeding 64 KiB; Unicode paths
are ASCII-encoded before JNI transfer. External `file:` and `content:` intents
are deliberately not accepted until a scoped Android document adapter exists.

Warm links use a mutex-protected, bounded 32-entry FIFO and SDL wake events,
not a polling timer or navigation on the Java UI thread. Full queues report
failure rather than silently replacing another link. Native processing is
bounded per event-loop iteration. Actual cold/warm activity lifecycle behavior
still requires APK/device testing.

## Test evidence and next gates

- V8 GC-info build regression: reproduced undeclared `EnsureGCInfoIndexTrait`
  on the host snapshot target, moved its guard below generated-config includes,
  then compiled both real host and Android objects. All 19 checks pass: host
  section-based mode has no table-registration overloads; Android table mode
  retains all four. Exact patch reproduction and unchanged GN/generated-header
  hashes pass. Evidence: `build/codex-android-v8-gcinfo-test/REPORT.md`.
  The targeted GREEN build took 4.007 seconds, with sampled CPU 34.86–94.74%
  and peak tree RSS 131.18 MiB. The six-worker incremental resume remains
  capped at 560%; its first measured 31 seconds averaged 420.60% CPU
  (52.58% of this host), peaking at 948.57 MiB RSS, with no cgroup escapes.
  These are compilation metrics, not phone or browser performance.
- Native APK verifier: missing implementation RED, then genuine LLVM dynamic
  tag parsing RED, followed by 15 unit tests, seven negative CLI cases and
  inspection of the real SDL/wgpu/NDK STL libraries GREEN. Exact dependency
  lists are asserted, not merely nonempty. Evidence:
  `build/codex-android-apk-verifier-test/REPORT.md`. Unit/real-ELF runs used
  82.0%/108.9% command CPU and 21.8/35.8 MiB peak child RSS under the 140%
  quota. Synthetic fixture packages do not prove actual APK dependency closure.
- SDL3 shared-library and engine/base/net archive builds pass, reusing the
  actual configured Android objects. All 44 artifact checks and 67 SDL JNI
  signature matches pass. Strong dynamic imports resolve against API28
  dependency exports: SDL 274/274, wgpu 84/84 and NDK STL 160/160. This is static
  inspection, not a phone loader test. Evidence:
  `build/codex-android-sdl-engine-test/REPORT.md`. The 81.52-second build used
  124.59% command CPU, 212.02 MiB peak sampled tree RSS and the default 140%
  quota, with no sampled membership violations. Final `libmain.so` and APK
  linking remain pending on V8; package the matching NDK `libc++_shared.so`.
- The maintained standalone Android V8 CMake gate passes real C/C++ compiler
  ABI linking and smoke-object compilation with Clang24. The linked ABI probes
  and object are AArch64; generated V8 definitions, custom `__Cr` STL,
  relative-vtable flags and matching ICU headers are verified. Four negative
  configurations reject missing/mismatched inputs. The executable link still
  fails because the real V8 monolith is absent; no startup assertion has run.
  Evidence: `build/codex-android-v8-cmake-test/cycle5/REPORT.md`.
  Object compilation used 27.96% command CPU / 109.71 MiB peak child RSS over
  8.01 seconds, sharing the 140% host build quota; these are not phone metrics.
- The actual root CMake project now configures and compiles all 38 Android
  engine/app objects (34 C, four C++), including JNI, with real target
  OpenSSL/JPEG headers. All 155 graph/header/ABI checks pass. Generated assets,
  custom `__Cr` symbols, monolith-only ICU and target wgpu linkage are verified.
  Evidence: `build/codex-android-engine-cmake-test/cycle1/REPORT.md`.
  Object compilation took 156.30 seconds, 30.95% command CPU and 231.07 MiB
  peak child RSS; the shared compilation slice averaged 139.53% CPU. This
  supersedes earlier partial syntax probes, not the pending final shared-engine
  link or APK gate. The main link stops at the missing real V8 archive.
- All 11 Gradle/AGP configuration cases pass: seven ABI cases, missing/invalid
  V8 source, optional GPU forwarding and actual AGP-to-CMake configuration.
  Generated commands select Clang24 and matching V8/STL/ICU flags. Evidence:
  `build/codex-android-gradle-v8-test/REPORT.md`. Actual native configuration
  took 97.10 seconds, averaging 92.80% shared-slice CPU with 619.57 MiB peak
  process-tree RSS. Every quota sample retained the default 140% limit.
  No APK or Android runtime assertions have run.
- Android links: FIFO ordering/capacity, wakeup, concurrent transfer and malformed
  inputs pass native unit tests, ASan/UBSan and ThreadSanitizer. Java URI tests,
  SDK36/SDL activity compilation, manifest checks and the registered CMake test
  pass. NDK API28 syntax checks pass for the queue and actual main loop; JNI
  syntax still uses copied host OpenSSL headers, not a target TLS link. Evidence:
  `build/codex-android-launch-green`. Host queue test: 91.88% active CPU / 9.51 MiB
  peak RSS over 5.2 ms; sanitized: 87.14% / 13.90 MiB over 23.2 ms. One-second
  sampling plus child resource accounting does not measure Android idle CPU.
- `tests/test_android_deps.py`: missing implementation RED, fresh-clone index
  regression RED, then five tests GREEN. Seventeen CLI validation checks pass.
  Plans cover both supported ABIs; the active phone build targets arm64 only.
- `tests/test_trust.c`: missing implementation RED, then DER import, certificate
  chain, hostname, malformed data and leaf-as-root checks GREEN with sanitizers.
  Java SDK36/SDL compilation passes. JNI syntax passed with copied host OpenSSL
  headers; that is not target-ABI linking or on-device proof.
- `build/codex-android-trust-final`: the strengthened same-host unknown-root
  regression and JNI exception-cleanup revision pass a fresh unit/sanitizer and
  cross-syntax check. Host unit: 53.79% CPU / 9.51 MiB RSS; sanitized: 72.37% /
  18.48 MiB. These are subsecond active tests, not Android browser measurements.
- Real Gradle/AGP configuration reproduced the hard-coded-ABI failure. The app
  now defaults to arm64-v8a; `-PlumenAbis=arm64-v8a,x86_64` requests both targets.
  Empty or unsupported ABI names fail configuration. Real Gradle/AGP checks pass
  for default arm64, explicit arm64, both ABIs, and both invalid-input cases.
  Configuration runs take 20.7–35.1 seconds; peak process-tree RSS is 662.71 MiB.
  These tests do not invoke the native engine build or produce an APK.

Next: complete native dependency builds, correct actual target compile/link
failures, build and install the real APK, and test launch, relaunch, backgrounding,
keyboard, touch, navigation and HTTPS with one-second CPU/RSS monitoring.
Then implement/verify Android permissions, capture, local files and media, major
sites, WebGL/WebGPU/Wasm, and matched Android Chrome workloads. No Android RAM
savings or daily-use readiness claim is justified yet.

Current Android-only self-assessment: **20/100**, a build/integration checkpoint,
not a usable application. The wgpu agent's 100/100 is limited to its compile,
symbol and ELF checks; it does not certify phone rendering or browser APIs.
