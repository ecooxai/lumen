# Isolated current-source iPad build006

Execute only through root's `devinmac3d` control in `/Users/devin/repos/lumen`.
The native source freeze is the recorded `14814f31ee1f7771` generation, including
the parsed-iframe bootstrap fix. Pending Blob/data-URL, interface and encoding
changes must wait until this wrapper finishes. No simulator operation occurs.

Stage `build_current_ipad.py` and `test_build_current_ipad.py` in
`tests/wpt-ipad006/`, preserving the reviewed byte hashes. The previous
`tests/wpt-ipad005/` files and closed005 runtime evidence remain inputs.

Run the five provenance guards first:

```sh
python3.12 tools/monitor_process.py --duration 120 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006/build-wrapper-unit-gates -- python3.12 -m unittest discover -s tests/wpt-ipad006 -p test_build_current_ipad.py -v
```

Require actual `Ran 5 tests`, `OK`, natural exit0 and no monitor/cleanup error.
Then run the wrapper under a separate finite outer monitor:

```sh
python3.12 tools/monitor_process.py --duration 2100 --settle 0 --output output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006/build-outer -- python3.12 tests/wpt-ipad006/build_current_ipad.py
```

The wrapper itself runs exactly:

```sh
make -j3 PLAT=sim BUILD=build-sim-ipad006 app
```

The internal build monitor has a1800-second limit. Outer logs stay beside the
fresh `build-identity-001` directory; do not precreate that directory or
`build-sim-ipad006`. The wrapper refuses nonempty build override environment
variables and preserves any failed output for inspection.

After completion, inspect these compact fields from
`output/wpt/gpt-6-astra-pro-devinmac3d-ipad-20261022-006/build-identity-001/build-identity.json`:

- `status` must be `current_source_signed_simulator_build_verified`.
- `source_inputs_unchanged`, `old_source_and_evidence_unchanged`, and
  `source_signature_verified` must all be true.
- `installed` and `runtime_tests_executed` remain false.
- Retain `signed_source_bundle.executable_sha256`,
  `signed_source_bundle.bundle_contents_sha256`, the source manifest digest,
  and the exact build-monitor path/hash for the next installation binding.

The existing `build-sim/Lumen.app` remains the proven build003 source at its
original path. The next installation and runtime step uses a separately reviewed
generation006 entry point bound to the actual new receipt.
Build telemetry must be labelled as compilation activity, not browser resource
performance. Source edits may resume after the wrapper closes and confirms its
source/old-evidence guards.
