# Linux development build: reproduce and run

The downloadable executable is the exact Linux development build identified in the accompanying measured report and checksum file. It targets Linux x86_64 and retains the library search paths used during validation. Its executable bytes are preserved; the package contains no substituted, stripped or relinked executable.

The recorded environment used Clang 22.1.6, CMake 4.3.4 and glibc 2.43. These are tested environment identities, not a claim that every Linux distribution has compatible libraries. The [runtime identity record](gpt-6-astra-pro-builtin-rubywebterm-linux-runtime.json) lists the actual configuration, package metadata, ELF library names and RUNPATH. The [source manifest](../SOURCE-MANIFEST.json) identifies every exported file. The qualification report maps the public source export to the exact source inputs of the tested binary.

## Recover the source and public history

Extract `src.tar.gz` into a new directory. The archive includes the reviewed engine source, its generators and regression tests, source and qualification manifests, this guide, and the separately downloadable public-history Git bundle. Follow `OFFLINE-RECOVERY.md` at the archive root to clone the included bundle at its recorded branch and verify the source identity. Recovery needs no access to the private development checkout.

## Provision matching dependencies

The full browser requires C11/C++20 compilation support, Python 3, Node.js, CMake, Git, a systemd user manager, `chrt` and `nice`. Install the development headers and libraries for SDL3, ICU, OpenSSL, zlib, Brotli, Zstandard, FreeType, HarfBuzz, Fontconfig, FFmpeg, PNG, JPEG, WebP, GIF, EGL, OpenGL ES, libdatachannel, libpsl and Opus. Install suitable system fonts and an SDL-supported desktop display when running the interactive browser.

The recorded external dependencies are:

| Dependency | Version | Recorded archive SHA-256 |
| --- | --- | --- |
| Homebrew V8 Linux x86_64 bottle | 15.6.75.8 | `1dd28bd65bf4bae16e8b78e6f2ad15ffc712041179a5922ca8a32451ef59f968` |
| gfx-rs wgpu-native Linux x86_64 release | 29.0.1.1 | `95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a` |

These archive identities come from the existing verified dependency record. Dependencies were not downloaded or replaced during this compatibility task, and their archives are not redistributed in this source package. Preserve the V8 bottle's matching headers, library directory, `libexec` directory, private libraries and `icudtl.dat`. The build enables V8 pointer compression and retains `V8_CPPGC_MICROTASK_QUEUE`. Substituting unrelated Node or V8 headers would change the tested configuration.

Extract the matching dependencies beneath the source root so these paths exist:

```text
build/codex-deps/v8/bottle/v8/15.6.75.8/include
build/codex-deps/v8/bottle/v8/15.6.75.8/lib
build/codex-deps/v8/bottle/v8/15.6.75.8/libexec/icudtl.dat
build/codex-deps/wgpu/include/webgpu
build/codex-deps/wgpu/lib/libwgpu_native.so
```

The runtime JSON preserves each package's reported `pkg-config` version. This is useful when reproducing the environment; it is not a dependency installer or a hermetic dependency lock.

## Prepare the pinned external native-test fixtures

The measured native selection contains **147 tests**, with exactly the three URL targets deferred. Reproducing that selection also requires **64 external WPT inputs**: 62 HTML parser data files for the `html5lib` target and two fonts for `font_units`. These are separate from the 650 exported project inputs.

Place the WPT checkout at `../../wpt` relative to the source root **before configuring CMake**. For example, the source may be at `<work>/browser/lumen/lumen` and WPT at `<work>/browser/wpt`. The exact required revision is `0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb`. From the source root, the following creates a fresh sibling checkout; if that directory already exists, verify it instead of replacing it:

```sh
git clone --filter=blob:none --no-checkout \
  https://github.com/web-platform-tests/wpt.git ../../wpt
git -C ../../wpt checkout --detach 0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb
```

The [native replay inventory](gpt-6-astra-pro-builtin-rubywebterm-native147-replay-inputs.json) records all 147 selected names, portable command mappings, the 64 external paths and their hashes, and the generated-input dependencies. Verify the external files before configuration:

```sh
python3 - <<'PY'
from pathlib import Path
import hashlib, json, subprocess

root = Path.cwd()
inventory = json.loads((root / "docs/gpt-6-astra-pro-builtin-rubywebterm-native147-replay-inputs.json").read_text())
wpt = (root / "../../wpt").resolve()
revision = subprocess.check_output(["git", "-C", str(wpt), "rev-parse", "HEAD"], text=True).strip()
assert revision == inventory["external_wpt"]["commit"]
fixtures = inventory["external_wpt"]["fixtures"]
assert len(fixtures) == 64
for item in fixtures:
    path = wpt / item["path"]
    assert path.is_file(), str(path)
    assert hashlib.sha256(path.read_bytes()).hexdigest() == item["sha256"], item["path"]
expected_parser = {item["path"] for item in fixtures if "html5lib" in item["native_test_names"]}
actual_parser = {path.relative_to(wpt).as_posix() for path in (wpt / "html/syntax/parsing/resources").glob("*.dat")}
assert len(expected_parser) == 62 and actual_parser == expected_parser
print("Verified pinned WPT revision, all 64 external hashes and the exact 62 parser datasets.")
PY
```

Without the parser data directory, CMake omits `html5lib`, leaving a different test selection. Missing font fixtures can cause `font_units` to skip. A smaller suite or a skipped font test does not reproduce the measured 147/147 result.

## Configure and build

Run these commands from the extracted source root. The supplied budget wrapper uses an active systemd user manager and shared CPU quota, with low-priority scheduling. It fails if it cannot establish that budget. On the development host, compilation and test execution use this wrapper.

```sh
lumen_v8_prefix="$PWD/build/codex-deps/v8/bottle/v8/15.6.75.8"
lumen_wgpu_prefix="$PWD/build/codex-deps/wgpu"

python3 tools/codex_compile.py cmake -S . -B build/codex-native \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DLUMEN_BUILD_BROWSER=ON \
  -DLUMEN_DEPS_DIR="$lumen_v8_prefix" \
  -DLUMEN_V8_POINTER_COMPRESSION=ON \
  -DV8_INCLUDE="$lumen_v8_prefix/include" \
  -DWGPU_INCLUDE="$lumen_wgpu_prefix/include/webgpu" \
  -DNATIVE_wgpu_native="$lumen_wgpu_prefix/lib/libwgpu_native.so" \
  -DCMAKE_CXX_FLAGS=-DV8_CPPGC_MICROTASK_QUEUE \
  -DCMAKE_BUILD_RPATH="$lumen_v8_prefix/libexec;$lumen_wgpu_prefix/lib" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath-link,$lumen_v8_prefix/libexec"

python3 tools/codex_compile.py cmake --build build/codex-native -j2
```

This reproduces the recorded configuration after dependencies are provisioned. A fresh source-export rebuild and byte-for-byte reproducible compilation are separate claims from verifying that the exported inputs match the qualified build; consult the qualification report for the checks actually performed.

## Run the browser

Set the V8 ICU data location before launching either the browser or its JavaScript test executable:

```sh
export LUMEN_ICU_DATA="$lumen_v8_prefix/libexec/icudtl.dat"
```

For a newly compiled browser, the build's RUNPATH points to the configured dependency directories. For the downloaded tested executable, the recorded absolute RUNPATH belongs to the validation host. If the matching libraries are installed elsewhere, provide their directories in the normal dynamic loader environment:

```sh
export LD_LIBRARY_PATH="$lumen_v8_prefix/libexec:$lumen_v8_prefix/lib:$lumen_wgpu_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

Inspect the supplied checksum file before running the downloaded executable. In an SDL-supported desktop session, launch the rebuilt browser with:

```sh
./build/codex-native/lumen
```

The separately packaged executable can be launched by its supplied filename with the same matching runtime. It is a development build, not a standalone portable application. The dependency record includes `libicuuc.so.78`, V8 and wgpu libraries, SDL3, FFmpeg 62/60/9/6 component ABIs and the other exact ELF imports; missing or ABI-incompatible imports must be resolved before launch.

## Reproduce the included native regression suite

The task defers exactly `url`, `url_host_nul` and `data_url`. After the pinned external files are in place and the build is configured, verify the complete selected test identity set and the two resolved font paths before running:

```sh
python3 - <<'PY'
from pathlib import Path
import hashlib, json, subprocess

root = Path.cwd()
inventory = json.loads((root / "docs/gpt-6-astra-pro-builtin-rubywebterm-native147-replay-inputs.json").read_text())
plan = json.loads(subprocess.check_output([
    "ctest", "--test-dir", "build/codex-native", "--show-only=json-v1",
    "-E", "^(url|url_host_nul|data_url)$"
], text=True))
names = [test["name"] for test in plan["tests"]]
assert len(names) == 147 and sorted(names) == sorted(inventory["selected_native_test_names"])
cache = {}
for line in (root / "build/codex-native/CMakeCache.txt").read_text().splitlines():
    if line and not line.startswith(("#", "//")) and "=" in line and ":" in line.split("=", 1)[0]:
        key, value = line.split("=", 1)
        cache[key.split(":", 1)[0]] = value
for key, basename in (("LUMEN_TEST_AHEM_FONT", "Ahem.ttf"), ("LUMEN_TEST_ZERO_FONT", "ChTestZeroWidthZero.woff")):
    value = cache.get(key, "")
    assert value and not value.endswith("-NOTFOUND") and Path(value).is_file(), key
    expected = [item["sha256"] for item in inventory["external_wpt"]["fixtures"] if Path(item["path"]).name == basename]
    assert len(expected) == 1 and hashlib.sha256(Path(value).read_bytes()).hexdigest() == expected[0], key
print("Verified all 147 selected identities and both required font fixtures.")
PY

python3 tools/codex_compile.py ctest --test-dir build/codex-native \
  -j1 --output-on-failure -E '^(url|url_host_nul|data_url)$' \
  --output-junit "$PWD/build/codex-native/native-results.xml" &&
python3 - <<'PY'
from pathlib import Path
import json, xml.etree.ElementTree as ET

root = Path.cwd()
inventory = json.loads((root / "docs/gpt-6-astra-pro-builtin-rubywebterm-native147-replay-inputs.json").read_text())
cases = ET.parse(root / "build/codex-native/native-results.xml").getroot().findall(".//testcase")
assert len(cases) == 147
assert all(case.attrib.get("status") == "run" for case in cases)
assert sorted(case.attrib["name"] for case in cases) == sorted(inventory["selected_native_test_names"])
assert all(all(case.find(tag) is None for tag in ("failure", "error", "skipped")) for case in cases)
print("All 147 selected tests passed without failures, errors or skips.")
PY
```

The included source also contains the strict raster sanitizer harness at `tests/test_raster_layers.py`, CSS containment native controls, and the authored observer/retention integration fixtures. Their precise tested identities and results are recorded in the measured report. Runtime sanitizer configuration and the selected target list are evidence for the runs actually performed, rather than an assertion that every possible build configuration was tested.

The WPT checkout, pinned Chromium installation and capture automation are separate validation prerequisites. The fixed 640-file non-URL sample, selected containment scope, native tests and authored controls have separate denominators. The public report enumerates the selected upstream paths and records failures, timeouts, source identities and schedule hashes. It makes no full-WPT or whole-browser Chromium-parity claim.

## Check real test-host completion status

The lifecycle integration target opts into an explicit completion-attribute value and uses the test process status. A matching output string alone does not override a failing process. The two included negative controls exercise a missing completion attribute and an explicit `FAIL` value. Each should return status `1` after normal cleanup:

```sh
for lumen_control in missing-completion failed-completion; do
  if env JSRUN_LAYOUT=1 LUMEN_CONSOLE=0 \
    JSRUN_UNTIL_ATTR=data-document-lifecycle-complete \
    JSRUN_EXPECT_ATTR_VALUE=PASS \
    python3 tools/codex_compile.py ./build/codex-native/jsrun \
      "tests/jsrun-completion/$lumen_control.html" 20; then
    lumen_control_status=0
  else
    lumen_control_status=$?
  fi
  test "$lumen_control_status" -eq 1 || exit 1
done
```

The positive lifecycle fixture is `tests/js/document-lifecycle.html`; its registered CTest target requires `PASS` in the same completion attribute and a successful process exit. The final qualification report distinguishes the original sanitizer failure, the matched test-host pin-release diagnosis and the corrected host's actual positive/negative exits. No sanitizer suppression or changed lifecycle oracle was used.

## Reproduce the separate slot-accessor controls

The additional `tests/js/checkvisibility-slot-accessors.html` fixture contains two separately named cases, `localName` and `namespaceURI`. Each verifies a real assigned slot and layout box, installs one throwing accessor temporarily, and requires `checkVisibility({opacityProperty:true})` to return `false` without reading that accessor. These two controls have their own denominator; they are separate from the unchanged 49-check option fixture and from upstream WPT.

```sh
env JSRUN_LAYOUT=1 LUMEN_CONSOLE=0 \
  JSRUN_UNTIL_ATTR=data-checkvisibility-slot-accessors-complete \
  JSRUN_EXPECT_ATTR_VALUE=PASS \
  python3 tools/codex_compile.py ./build/codex-native/jsrun \
    tests/js/checkvisibility-slot-accessors.html 10000
```

The expected successful status is `0`, with two `SLOT_ACCESSOR_ROW` records, each reporting zero accessor reads, no error, a real assigned box and a `false` visibility result. The final report records the native and pinned Chromium reference measurements separately.

Prepared by GPT-6 Astra Pro with built-in agents and RubyWebterm, 2026-10-31 UTC.

## Run the authored interactive demonstration

The interactive page runs in the browser opening it. Open it in Lumen to exercise the Lumen engine. Recorded native234 validation is separate from the live values shown in any other browser. This authored demonstration is outside WPT scoring. Its separately recorded native validation verifies actual API readings and genuine document lifecycle events; it is not a remote Lumen session.

After satisfying the runtime prerequisites, open the included file in Lumen:

```sh
./build/codex-native/lumen "file://$PWD/examples/gpt-6-astra-pro-builtin-rubywebterm-lumen-live-demo.html"
```

The original measured HTML is included byte-for-byte. See `docs/gpt-6-astra-pro-builtin-rubywebterm-live-demo-evidence.json` for its hash and recorded native observations.
