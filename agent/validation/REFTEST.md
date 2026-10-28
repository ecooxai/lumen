# Native viewport reftest pilot

`reftest_pilot.py` evaluates each native test against its own native reference. Chromium is measured separately through the pinned official WPT runner in `official_reftest.py`. Native pixels are never compared directly to Chromium pixels as the pass oracle.

## Capture contract

The Linux executable exposes an opt-in test-only hook: `LUMEN_WPT_CAPTURE` selects an output path and requires `LUMEN_TESTDRIVER`. SIGUSR1 only sets an atomic signal flag. The next main-loop iteration forces a non-deferred fresh render and writes the actual page rectangle from the composed presented frame, excluding toolbar/sidebar. Bounds, stride, row offset, and write failures fail closed. The log includes actual URL, viewport dimensions, scale, crop rectangle, and frame timing. The default production path and existing timer-based screenshot feature are unchanged.

The harness installs the pinned WPT `test-wait.js` readiness algorithm with its normal `reftest-wait` class substitution. It waits for upstream load, fonts, paint, `reftest-wait`, and `TestRendered` behavior before signaling capture. It verifies the current URL, 800x600 CSS viewport, DPR/scale 1, loaded fonts, PNG dimensions, and capture metadata. A fixed elapsed delay is not used as the readiness oracle.

## Running

Use the WPT virtual environment with Pillow and the pinned wptrunner modules available. For a JSON list of URL strings:

```
python3 agent/validation/reftest_pilot.py --binary build/codex-native/lumen-astra-i114 --urls-file URLS.json --output EVIDENCE/native
python3 agent/validation/official_reftest.py --urls-file URLS.json --output EVIDENCE/chromium --jobs 2
```

The native helper accepts `--wpt-root`, `--manifest`, `--timeout`, and `--long-timeout`. It uses fresh profiles, preserves screenshots and their SHA-256 digests, and samples process resources every second. Use the shared project CPU wrapper and project-local temporary storage. Run separate output directories for each frozen executable; never reuse another build's reference images.

## Oracle and limits

The comparison calls the pinned official `RefTestImplementation` for exact and fuzzy pixel decisions. Multiple equality references use any-match semantics; every inequality must succeed. Metadata, raw outcomes, images, dimensions, and difference counts are retained. Recursive reference graphs, unsupported testdriver actions, non-HTTP references, and viewport/DPR overrides outside this pilot's supported contract are reported as unsupported rather than silently changed.

The accepted infrastructure gate used 11 unchanged upstream controls. Its raw outcomes were 8 PASS and 3 deliberate FAIL, with all 11 agreeing with their intended control behavior and with Chromium. The negative controls include genuinely different green/red captures and deliberately identical inequality captures. The fuzzy edge differs in exactly 100 pixels at maximum channel difference 255. The PM inspected a montage of actual nonblank viewport captures. These are control results, not 11 passing conformance tests.

The first adapter attempt incorrectly initialized the official comparator lifecycle. Its errors and PNGs remain preserved. The corrected run initializes the comparator's message list as the upstream runner does. Capture-only candidate114 is the accepted baseline. Candidate113's background-position work is a separate, initially rejected semantic change; it is not part of this capture acceptance.

This is bounded infrastructure coverage at 800x600 and DPR1, not evidence that all 28,654 reftest URLs are supported or passing. See the iteration6 acceptance record and ignored session evidence for exact binary, readiness, source, and screenshot hashes.
