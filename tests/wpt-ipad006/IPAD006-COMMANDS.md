# Signed iPad006 install and standalone runtime checks

Execute only through root's Mac `devinmac3d` connection, with working directory
`/Users/devin/repos/lumen`. Do not run any commands or tests on the relay machine.

## Frozen inputs

The actual isolated build is already complete. Do not rebuild it. Its receipt is
`output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006/build-identity-001/build-identity.json`,
SHA256 `f660fae6cbc10298b9efd24b9a32e64317261adecf30dc245716bb516fdf7ae9`.

The app is `build-sim-ipad006/Lumen.app`, executable SHA256
`351a9f393b76d3f6793409a4033dd121441f68714d02729056d8919bc800d539`,
complete signed-bundle contents digest
`9eb2aa287b8ebe73c51193c2ce659a19313a81b3c646df0aeb902b7e1d8cb479`.
The sole target remains iPad `A77F68E4-972B-472B-80B8-F65E71D361E5`,
bundle `com.ecooxai.lumen`. Earlier source bundle003 and closed004/005 evidence
are retained. No iPhone, other device, boot, erase or service reset is used here.

## Staging

Stage this package at `tests/wpt-ipad006`, checking every file against
`manifest.json`. Six files can be copied from the already gated005 directory
after verifying their unchanged hashes: `full_runner.py`, `ipad_adapter.py`,
`ipad_lifecycle.py`, `testharnessreport.js`, `test_ipad_adapter.py`, and
`test_ipad_lifecycle.py`. The new entry point changes only the source app path.
Its test fixture makes the new build directory explicitly. The generation,
installation and runtime wrappers bind the actual006 receipt and preserve the
earlier profile whose Unix-signal cleanup failed.

The already executed build wrapper and its five build tests remain unchanged.
They are separate from the following56 runtime/provenance unit tests.

## Unit gate

```sh
python3.12 tools/monitor_process.py --duration 120 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006/isolated-unit-gates -- python3.12 -m unittest discover -s tests/wpt-ipad006 -p 'test_ipad_*.py' -v
```

Require all56 tests, monitor natural exit0, no collector or owned-cleanup error
and no violations before proceeding. No browser is launched by this gate.

## Signed installation

The fresh evidence directory must not already exist. Keep outer-monitor files
at the generation root, outside the directory owned by the installation helper.

```sh
python3.12 tools/monitor_process.py --duration 900 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006/isolated-install-monitor -- python3.12 tests/wpt-ipad006/install_verify_ipad.py --root /Users/devin/repos/lumen --build-report output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006/build-identity-001/simulator-build.json --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006/install-identity-001
```

Require `install-verify.json` status `installed_and_verified`, matching351a9f
executable and9eb2aa bundle digests, and
`retained_failure_profile_unchanged: true`. The helper checks the previous
installed003 seal, exact-device inactivity, new source seal, complete bundle
identity and the retained004 failure profile before and after the platform
update. Any failure stops; preserve its evidence and inspect before retrying.

## HTTP then HTTPS

Do not precreate `runtime-smoke-001`: the first runtime stage owns its exclusive
creation. Outer-monitor logs again belong at the generation root.

```sh
python3.12 tools/monitor_process.py --duration 900 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006/http-runtime-monitor -- python3.12 tests/wpt-ipad006/launch_ipad_smoke.py http
```

The wrapper selects the same six real HTTP URL variants as005. Require all six
rows and safe closed platform termination before launching HTTPS; failed
browser assertions remain failures and do not become infrastructure errors.

```sh
python3.12 tools/monitor_process.py --duration 300 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006/https-runtime-monitor -- python3.12 tests/wpt-ipad006/launch_ipad_smoke.py https
```

The HTTPS stage selects the same real secure-context worker test as005 and
independently inspects HTTP closure. The prior genuine nested-worker and data-URL
worker failures remain in005; only this actual new run can establish a change.

## Report

```sh
python3.12 tools/monitor_process.py --duration 120 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006/runtime-report-monitor -- python3.12 tests/wpt-ipad006/launch_ipad_smoke.py report
```

Report exact attempted/scheduled files, all assertion outcomes, separate app and
termination-CLI CPU/RSS observations, sample coverage and lifecycle evidence.
This is standalone iPad simulator testing; it makes no Chromium comparison,
global WPT rate, sustained performance qualification, same-IPA or physical-device
claim. Platform-request termination is explicit and is not claimed to prove a
natural or graceful app shutdown. Preserve all earlier failed and successful
generations unchanged.
