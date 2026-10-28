# iPad005: explicit simulator platform termination

This source package is pending independent review and Mac gates. No local tests
have been executed. Root alone runs these commands through devinmac3d in
`/Users/devin/repos/lumen`.

## Scope and retained evidence

The target remains only iPad `A77F68E4-972B-472B-80B8-F65E71D361E5`, bundle
`com.ecooxai.lumen`. The signed source and installed app remain the proven
build003 executable `b6bfb5ffa7ec86c144e4c49842ac911b60f40631d8a06fa87147e20333c0cd97`.
No build, installation, simulator creation/boot/shutdown/erase, other-device
action, or app launch other than the selected WPT cases is performed here.

The original iPad004 HTTP invocation passed its first 21 assertions and then
failed its Unix-signal cleanup. That exact row and closed invocation are pinned
and stay failed. Its separately recorded recovery used `simctl terminate`,
returned naturally with code 0 and verified the exact app gone. The new
`ipad_lifecycle.py` binds those actual facts before each launch or report.

The new adapter requests platform termination as its primary teardown contract.
It does not first try a Unix signal and then relabel that failed attempt. The
exact current device, installed registration, sole app PID, start time and
executable path are checked immediately before the command. The same-device
runner lock remains in force. A replacement app or changed registration stops
the run. The platform check and request cannot be atomic with an external user
launch; this limit remains recorded in the adapter metadata.

The CLI has a 20-second deadline, a separate owned process group, raw log and
one-second telemetry. Any nonzero exit, watchdog timeout, collector issue, forced
CLI signal, or surviving group remains a failure. App telemetry continues until
teardown completes. Its original identity must be absent and no replacement app
may remain before the owned test profile is removed. The failed iPad004 profile
is never removed. Platform cleanup does not establish natural or graceful app
shutdown, and every report retains that distinction.

## Stage sources and run isolated unit gates

Stage the manifest's exact files into `tests/wpt-ipad005`, copying unchanged
files from `tests/wpt-ipad004` only after verifying their listed hashes. The Mac
runner and all previous iPad sources/cohorts remain unchanged.

Use fresh output paths under
`output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-005`.

```sh
PYTHONPATH=tests/wpt-ipad005 python3.12 tools/monitor_process.py --duration 120 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-005/unit-gates -- python3.12 -m unittest discover -s tests/wpt-ipad005 -p 'test_*.py' -v
```

Require the actual **56 tests** to pass, natural exit 0, and no collector or
owned-cleanup error. Tests are authored for ownership guards, platform request
failures/timeouts, app telemetry order, retained failure evidence, the unchanged
signed-bundle/runner checks and the exact-device generation proof.

## Fresh HTTP, then HTTPS

Do not precreate `runtime-smoke-001`; the launcher creates it exclusively. Keep
outer monitor files beside it. The same installation proof is reused after
full signature/content/identity verification; no reinstall is needed.

```sh
python3.12 tools/monitor_process.py --duration 1200 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-005/http-outer -- python3.12 tests/wpt-ipad005/launch_ipad_smoke.py http
```

Inspect the actual HTTP `run.json`, six ordered rows and app/CLI cleanup before
launching HTTPS. Failed browser assertions remain failures and do not by
themselves invalidate safe execution. Any infrastructure, collector, profile or
termination issue stops this fresh cohort and preserves its evidence.

```sh
python3.12 tools/monitor_process.py --duration 300 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-005/https-outer -- python3.12 tests/wpt-ipad005/launch_ipad_smoke.py https
python3.12 tools/monitor_process.py --duration 60 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-005/report-outer -- python3.12 tests/wpt-ipad005/launch_ipad_smoke.py report
```

The HTTPS launcher independently requires a closed, complete and safe HTTP
stage. Its one case is an actual HTTPS secure-context WPT from the same pinned
inventory. The final standalone report keeps separate app and termination-CLI
measurements. It makes no Chromium comparison, whole-suite pass-rate claim,
physical-iPad performance claim, same-device-IPA claim or graceful-shutdown pass.
