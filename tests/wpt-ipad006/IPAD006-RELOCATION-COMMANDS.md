# iPad006 profile relocation verification and runtime

Run only on the authorized Mac through `devinmac3d`, in `/Users/devin/repos/lumen`.
The original006 installer is **not rerun**. Its stopped001 receipt and the failed004
runtime/recovery evidence remain unchanged. The source and installed app are the
already observed build006 executable `351a9f393b76d3f6793409a4033dd121441f68714d02729056d8919bc800d539`.

Stage only the three new Python files and this command sheet into `tests/wpt-ipad006`
after checking `relocation-manifest.json`. Existing006 files stay unchanged.
The ten new tests supplement the already executed56 unchanged iPad gates.

```sh
python3.12 tools/monitor_process.py --duration 120 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006/relocation-unit-gates -- python3.12 -m unittest discover -s tests/wpt-ipad006 -p 'test_ipad_relocation.py' -v
```

Require all ten tests and a natural0 monitor completion with no collector,
cleanup, or violation entries. Then run the verifier:

```sh
python3.12 tools/monitor_process.py --duration 600 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006/relocation-verification-monitor -- python3.12 tests/wpt-ipad006/verify_ipad006_relocation.py
```

It creates the fresh `install-relocation-002` directory. Do not precreate it.
It performs bounded read-only device/registration/signature checks, uses the
same exact-device lock, and copies only the two found failed004 profile files
and their empty directories to `install-relocation-002/retained-failed004-profile`.
The source profile remains at the observed `BB2BD9D4-CFEC-4ADD-8770-5C2CBC873896`
data container. All six filesystem identities must exactly match the original
receipt, including integer nanosecond timestamps. Archive/source bytes must
match the hashes first observed **after relocation**. No earlier content-hash
claim is made.

Require the new receipt status `installed_and_verified_after_profile_relocation`
and clean natural0 outer completion. The receipt deliberately keeps
`original_install_gate_passed: false`. Then run the new entry point:

```sh
python3.12 tools/monitor_process.py --duration 900 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006/runtime-http-monitor -- python3.12 tests/wpt-ipad006/launch_ipad_smoke_relocated.py http
```

Do not precreate `runtime-smoke-001`; the launcher owns its exclusive creation.
After HTTP closes safely, including verified exact-device platform cleanup:

```sh
python3.12 tools/monitor_process.py --duration 300 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006/runtime-https-monitor -- python3.12 tests/wpt-ipad006/launch_ipad_smoke_relocated.py https
python3.12 tools/monitor_process.py --duration 120 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006/runtime-report-monitor -- python3.12 tests/wpt-ipad006/launch_ipad_smoke_relocated.py report
```

When detaching a monitor, redirect its own stdout to a distinct `*.launcher.log`;
never use the monitor's own `<prefix>.log` command-log path for that redirection.
The runtime retains the original HTTP6/HTTPS1 schedule, signed-bundle and exact
fixture checks, platform termination policy, per-case CPU/RSS, and real failing
assertions. The report separately validates the retained profile/archive and
records any preservation error without dropping completed test outcomes.
No Chromium comparison, physical-device result, or same-IPA claim is implied.

Only a read-only verifier and exclusive project archive are added here. There is
no install, app launch, simulator boot/shutdown, erase, deletion, or service reset
in the verifier; actual runtime app launches occur only in the reviewed entry
point after its new explicit verification preflight.
