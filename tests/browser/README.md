# Full-browser regression fixtures

These tests exercise the application image pipeline, which the smaller `jsrun` harness does not install. They use a loopback HTTP server, a temporary browser profile, and the offscreen renderer. No existing browser session or external service is accessed.

Run `ctest --test-dir build/codex-native -R browser_image_lifecycle --output-on-failure` after a native build, or:

```sh
python3 tests/browser/run.py --binary build/codex-native/lumen --fixture tests/browser/image-lifecycle.html --marker browser-image-lifecycle --output build/browser-image-lifecycle
```

The runner records CPU usage and resident memory every second in `resources.csv`; `browser.log` contains the exact fixture result. The PNG is the existing 87-byte `images/green.png` from the checked-out web-platform-tests corpus. The fixture covers ordinary URLs, disconnected and connected Blob images, real decode completion, cached reuse, revocation before/after loading, changed sources, invalid bytes, and distinct decode promises. Native screenshot comparison remains separate from these lifecycle assertions.


`control-selection.html` exercises native input and Save after selection before focus, detached selection, pointer caret placement, keyboard selection, focus switches, Unicode, cloning, input-type changes and form reset. `tests/device/editbar/controlled-message.html` uses the existing bundled React16.14.0 to exercise an actual controlled textarea, native input, and post-Save DOM reconciliation. Both tests use `--testdriver`, which enables the browser's native event bridge; they do not inject replacement values from the runner.
