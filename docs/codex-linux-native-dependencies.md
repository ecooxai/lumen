# Codex Linux native dependency record — 2026-10-12

The first complete Linux build uses the following pinned, matching headers and
libraries. These are development dependencies inside ignored `build/`, not a
redistributable bundle or an Android toolchain. Preserve license notices when
packaging dependencies; redistribution has not been audited.

| Dependency | Version | Archive SHA-256 |
| --- | --- | --- |
| Homebrew V8 Linux x86_64 bottle | 15.6.75.8 | `1dd28bd65bf4bae16e8b78e6f2ad15ffc712041179a5922ca8a32451ef59f968` |
| gfx-rs wgpu-native Linux x86_64 release | v29.0.1.1 | `95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a` |

Local provenance: `build/codex-deps/v8/brew.json`, `formula.rb`, `bottle.tar.gz`,
and `build/codex-deps/wgpu/HANDOFF.md` with release metadata and digest checks.
Do not mix other V8 headers, libraries or configuration flags into these paths.
The old Node headers do not match this V8 build. The inspected Deno rusty_v8
archive uses a different C++ standard-library ABI and was not linked. The tested
wgpu v27 candidate lacks initializer macros used by this engine and was rejected.

## Reproduce the verified configuration

From the repository root, after extracting the pinned dependencies to the recorded
paths and installing the system dependencies listed in `codex-android-linux-status.md`:

```sh
V8_PREFIX="$PWD/build/codex-deps/v8/bottle/v8/15.6.75.8"
WGPU_PREFIX="$PWD/build/codex-deps/wgpu"
cmake -S . -B build/codex-native \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DLUMEN_DEPS_DIR="$V8_PREFIX" \
  -DV8_INCLUDE="$V8_PREFIX/include" \
  -DWGPU_INCLUDE="$WGPU_PREFIX/include/webgpu" \
  -DNATIVE_wgpu_native="$WGPU_PREFIX/lib/libwgpu_native.so" \
  -DCMAKE_CXX_FLAGS=-DV8_CPPGC_MICROTASK_QUEUE \
  -DCMAKE_BUILD_RPATH="$V8_PREFIX/libexec;$WGPU_PREFIX/lib" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath-link,$V8_PREFIX/libexec"
cmake --build build/codex-native -j2
ctest --test-dir build/codex-native --output-on-failure
export LUMEN_ICU_DATA="$V8_PREFIX/libexec/icudtl.dat"
```

CMake enables `V8_COMPRESS_POINTERS`. The extra microtask macro follows the
inspected bottle formula; the smoke did not establish that it is necessary for
these installed headers. Retain the bottle's sibling private libraries and
existing library RPATHs. Both executables have resolved shared-library imports
on this host. Absolute build RPATHs are development-only, not a portable install.

Use an isolated test HOME/profile and the repository's one-second CPU/RSS
monitoring requirement for browser runs. Do not use personal accounts until
the origin, local-file and permission security gates are resolved.

## Evidence

`build/codex-v8-probe/REPORT.md` records a full native Release build (`lumen`,
`jsrun`), six passing CTests at that checkpoint, and a standalone V8 smoke:
initialization, JavaScript, Wasm returning 42, ICU and Promise microtasks.
The smoke averaged 1.03% CPU and 36.1 MiB RSS over approximately five seconds;
this is not a loaded browser or website benchmark. The two-job build averaged
177.5% CPU and peaked at 485.3 MiB sampled process-tree RSS.

No Android binary, browser WebGPU/WebGL conformance, site compatibility, or
Chrome memory advantage is implied by compiling the native dependencies.
