# Isolated iPad generation004

Execute these commands only on `/Users/devin/repos/lumen` through the root
agent's `devinmac3d` connection. The reviewed files in this directory deploy
to **`tests/wpt-ipad004`**, a sibling of the active Mac WPT directory.
Keep its `full_runner.py`, `testharnessreport.js`, adapter, entry point, and
generation module unchanged throughout this iPad smoke.

The only permitted target is **A77F68E4-972B-472B-80B8-F65E71D361E5**, the
fresh iPad Air 11-inch M4 using iOS 26.5 build23F73. The original device and
all failed install/restart evidence remain preserved. None of the following
commands creates, boots, erases, deletes, or selects another device.

The app is the already successful signed **build003** bundle, executable
SHA256 `b6bfb5ffa7ec86c144e4c49842ac911b60f40631d8a06fa87147e20333c0cd97`.
Later Mac-only worker/Beacon changes are not represented as installed iPad
changes. Rebuilding the simulator app invalidates these exact input pins;
first finish this smoke and retain its results.

## 1. Isolated Mac unit gate

Expected count: **39** (14 adapter, 18 entry point, 7 generation identity).
The new source was statically inspected and parsed; no tests were executed
locally. Actual Mac results must be recorded before installation.

```sh
python3.12 tools/monitor_process.py --duration 120 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-004/isolated-unit-gates -- python3.12 -m unittest discover -s tests/wpt-ipad004 -p 'test_ipad*.py' -v
```

## 2. Verify, install, and verify again

Use a detached outer controller with a **600-second** ceiling and regular
status polling. The installer owns its bounded 120-second `simctl install`
and retains signature, registration, idle-device, and full bundle evidence.
Proceed only from actual `installed_and_verified` with source and installed
binary/bundle identity matching. Preserve any failed output directory.

```sh
python3.12 tests/wpt-ipad004/install_verify_ipad.py --root /Users/devin/repos/lumen --build-report output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-003/simulator-build.json --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-004/install-identity-001
```

Read the resulting
`output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-004/install-identity-001/install-verify.json`.
The launcher's successful status binds the actual provisioning receipt
SHA256 `0415de2f2c054cd7558b650ad6d99e5276d28d9c28ca14bf8779a21dccf732bf`,
the exact build003 monitor report, and both verified signed app bundles.

## 3. HTTP six-file smoke

The runner uses the official synchronous WPT reporter, exact loopback host
map, per-file timeout, one-second process telemetry, and identity-checked app
cleanup. It does not inject a prefile. Keep a detached outer **900-second**
ceiling and poll its run metadata. This is a functional smoke; concurrent
Mac tests prevent an isolated hardware performance comparison.

```sh
python3.12 tests/wpt-ipad004/launch_ipad_smoke.py http
```

The actual output directory is
`output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-004/runtime-smoke-001/http`.
Failed assertions remain failures; any infrastructure, collector, or owned
app cleanup issue blocks the following stage. A very short or empty telemetry
sample does not establish acceptable sustained CPU/RAM use.

## 4. HTTPS one-file smoke

This stage independently rechecks the exact generation and live v36 fixture,
then requires the HTTP invocation to be closed, complete, and safe. It selects
a real HTTPS variant from the pinned official `/secure-contexts/` inventory.
Use a detached outer **360-second** ceiling with regular polling.

```sh
python3.12 tests/wpt-ipad004/launch_ipad_smoke.py https
```

## 5. Retain the standalone report

```sh
python3.12 tests/wpt-ipad004/launch_ipad_smoke.py report
```

The report is
`output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-004/runtime-smoke-001/smoke-report.json`.
It labels iPad Simulator process-tree telemetry separately, preserves actual
case outcomes and partial evidence, and makes no Chromium-control, physical
iPad-performance, full-WPT, or same-IPA-on-Mac claim.

## Compact polling

For install: status, phase, error, source/installed executable SHA256, matching
bundle SHA256, and each step's natural exit/monitor/cleanup status.
For a running smoke: scheduled files, recorded files, invocation-open state,
stopped-for fields, current last result status/assertion counts, and telemetry
errors. Do not print full nested process inventories or large binary data.
