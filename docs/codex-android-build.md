# Codex Android phone build

## Current scope

Android only, per the user's latest instruction. Use the existing Lumen engine;
do not substitute WebView. Branch: `codex/android-phone-engine-port`.
Checkpoint `8cceb05` preserves the previous shared-engine work.

An authorized MI 8 with Android API 35 and arm64-v8a is connected. **No Lumen APK
has been built, installed, or tested on the phone.** Device connection is not
evidence of browser functionality. Do not import personal profiles or log in
until the engine's origin/file-access security blockers are repaired.

## Verified local toolchain

Preparation artifacts are ignored, machine-local files under
`build/codex-android-toolchain`. The test agent verified NDK r28c
(`28.2.13676358`), Gradle 8.13, CMake 3.22.1, SDK platform 36 and OpenJDK 21.0.11.
Java source compatibility remains 17. Existing SDK license acceptance was reused;
no new licenses were accepted blindly. Official archive checksums and resource
measurements are recorded in that directory's `REPORT.md`.

```sh
source build/codex-android-toolchain/env.sh
python3 tools/android/build_deps.py --ndk "$ANDROID_NDK_HOME" --abi arm64-v8a --dry-run
python3 tools/android/build_deps.py --ndk "$ANDROID_NDK_HOME" --abi arm64-v8a --jobs 2 --execute
```

The dry-run command prints a plan without creating files or downloading anything.
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

## Remaining native build inputs

- V8 15.6.75.8, same generation as the existing engine. The source and exact
  dependency revisions are being prepared in `build/codex-android-v8`. Android
  compilation, C++ ABI flags, ICU data packaging and startup remain to verify.
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
