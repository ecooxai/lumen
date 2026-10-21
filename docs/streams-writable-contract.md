# Writable stream lifecycle repair

The targeted implementation in `src/js/prelude/07-web.js` follows the WHATWG Streams writable-stream state transitions for queue sizing, writer backpressure, writer ownership, and close/error/abort sequencing. This is a focused repair; it is not a claim of complete Streams conformance.

## Changed behavior

- The supplied queue `size` function contributes its actual finite, nonnegative size. `desiredSize` can be negative while the queue is over its high water mark; bytes in a pending sink write remain counted until the sink resolves it.
- A writer retains the same `ready` promise while its state is unchanged. Crossing into backpressure creates a pending promise; draining below the high water mark resolves it. A zero high water mark starts with backpressure. Closing clears a pending `ready` promise without declaring the underlying sink closed early.
- Releasing a writer rejects its current ownership promises, invalidates its operations and desired-size getter, and permits acquisition of a new writer. Previously queued sink operations still complete; releasing ownership does not cancel them.
- An explicit stream state distinguishes an error whose reason is `undefined` from a stream with no error. Queue/sink failures stop later work and preserve the original reason.
- Abort signals the controller immediately, waits for an existing sink operation to finish, skips queued writes, and calls the sink abort callback once. Close waits for pending writes and the actual sink close promise.
- The existing `pipeTo()` abort path invokes the writer that owns the destination lock. Calling the destination stream's public `abort()` while locked correctly rejects.

Apart from correcting the existing pipe abort route, the repair leaves readable queue/reader behavior, URL/fetch implementations, transforms, and byte-stream/BYOB availability unchanged. The current readable implementation explicitly rejects a BYOB reader. Readable queue sizing and cancellation/error semantics need their own conformance work before a claim of complete byte-stream behavior.

## Evidence gates

`node tests/test_writable_streams.mjs` runs thirteen equivalent cases against Node's built-in reference implementation and the actual Lumen writable prelude source evaluated in a separate V8 context. Before the repair, all thirteen reference cases pass and twelve Lumen cases fail. The repaired source passes all twenty-six runs. This test does not claim actual browser networking or native process resource measurements.

`tests/js/writable-backpressure.html` exercises the installed browser globals and the existing readable-to-writable pipe path. Run it with the native JS runner and browser after rebuilding the prelude. The existing `tests/js/streams.html` fixture remains a required regression gate for pipes, transforms, pull sources, tee, text streams and response bodies.

The actual Mac `build/jsrun` run passed all five new lifecycle cases and all seven existing stream cases after both `build/jsrun` and `build/lumen` were rebuilt with `RTC=1`. The first evidence runner omitted the native console opt-in, so it correctly failed its completion gate even though the page had set its completion attribute. That failed report is preserved; the corrected runner sets `LUMEN_CONSOLE=1` and still requires explicit result counts. A zero process exit alone never passes this gate.

Run `python3 tests/test_writable_native.py` for a fresh two-target build and native verification. The optional `--reuse-built-report <path>` avoids a redundant build only when the recorded build succeeded and all source, fixture, and executable fingerprints still match exactly. Each rerun archives the previous report and its owned logs under `out/lab-streams/history/`. All test processes are bounded and sampled once per second for CPU/RSS.

No browser WebTransport constructor is exposed by this repair. A future native transport still needs actual HTTP/3 echo evidence, bounds, per-document cancellation, certificate verification and Apple target validation.

## Normative reference

WHATWG Streams Living Standard, retrieved 2026-10-21:

- [Writable streams](https://streams.spec.whatwg.org/#ws-class)
- [Writer ownership and promises](https://streams.spec.whatwg.org/#ws-default-writer-class)
- [Writable stream algorithms](https://streams.spec.whatwg.org/#ws-abstract-ops)
- [Queue sizing](https://streams.spec.whatwg.org/#queue-with-sizes)
