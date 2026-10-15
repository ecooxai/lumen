# Android WebAssembly streaming integration (2026-10-15)

The MI8 Vulkan build now supports WebAssembly.compileStreaming and
instantiateStreaming. The broad browser audit improves from8/13 to10/13;
full browser Wasm support remains unfinished.

## Implementation

src/js/wasm.cc connects V8's managed streaming compiler to Response consumption.
Private per-realm inspectors validate genuine Responses (including same-origin
iframe Responses) without trusting overridden public metadata/body getters.
V8 owns promise resolution, compiler lifetime, imports, and compile error types.
The bridge validates application/wasm without parameters, successful status and
response type; it consumes Uint8Array chunks with their offsets and propagates
stream failures/abort reasons. Page fetch feeds chunks as they arrive. Worker
APIs work, but worker fetch still buffers the complete download internally.

Response metadata and body state now use WeakMaps; body identity is stable,
direct reads/cancellation mark disturbance, cloning tees unread streams, and
locked/used bodies reject consumption. Fetch's fully-read body methods retain
their reader lock; Wasm consumption releases it. Body cancellation aborts the
underlying network request.

The tests exposed an existing network bug: native Fetch ignored errors once an
HTTP status existed, and buffered partial bodies could hide truncation entirely.
Incomplete Content-Length now records an error regardless of collected bytes;
page/worker Fetch propagates it and async script loading rejects error responses.

## Verification

Final APK: build/codex-wasm-streaming-20261015/lumen-vulkan-wasm-streaming-debug.apk

SHA256:42af2d0940212a04e9f24e41c726f6887d5b4977be59bf40406c3adf113fa54f.
Built through codex_compile.py using the verified prebuilt V8. Native ELF and
dependency checks pass; APK v2 signature verifies. Installed with adb. Device
log confirms requested Vulkan, active wgpu/vulkan. The frozen V8 source build
was not touched.

- 17 Node tests pass: Response bodies, Request state, Fetch preflight, Blob parts.
- Phone wasm-streaming:27/27PASS. Includes MIME/status checks, invalid binary,
  input promise rejection, forged/Proxy Responses, public getter overrides,
  used/locked/disturbed bodies, null body, clones,42 delayed byte-offset chunks,
  stream errors/invalid chunks, incremental fetch, abort, truncated Wasm and
  ordinary Fetch bodies, iframe Response, opaque response, worker APIs and
  worker truncated-fetch rejection.
- Phone original wasm-browser audit:10/13PASS. Still failing: module
  structuredClone, compiled module postMessage, shared Wasm memory postMessage.
- Phone Fetch regression:6/6PASS. Request state:5/5PASS.
- Phone worker lifecycle/async regression:8/8PASS, including idle-GC wakeup,
  concurrent compilation and termination. Round trip after idle1.67ms.

Evidence: reports.json, server.log, samples.jsonl, build.log and worker-final.png
under build/codex-wasm-streaming-20261015/. Whole-process and all-thread CPU
sampling runs at1Hz throughout device testing. Final measurements are recorded
in cpu-summary.json alongside these artifacts. Final idle82samples (excluding
four boundary samples): mean0.319%, peak5.155%, ending RSS249.78MiB with five
fixture tabs and one quiescent worker. This is not a before/after RAM comparison.
567total samples, zero cadence/parse errors. Closing the worker tab succeeded;
app stopped, sampler/server joined, owned adb reverses removed.

## Remaining work

The slow-response test returned headers in9.63ms but finished compilation in
1811.91ms for an800ms server delay. Fetch-only regression delivered chunks on
time. Source inspection shows page V8 foreground tasks use libplatform without
an event-loop wakeup/deadline; the main loop can wait1000ms. Fix and measure this
separately; this change makes no general performance or RAM improvement claim.

Worker true network streaming, module clone delegates, shared-memory isolation
and lifetime rules, broader stable Wasm feature/WPT coverage, real applications,
and complete Response/Streams conformance remain required. The current Streams
implementation lacks BYOB and uses mutable internal properties; arbitrary
prototype tampering is not comprehensively covered. Daily-browser performance,
YouTube, Google/ChatGPT/Gemini login and zoom work remain active. No account
credentials were used in this test. OpenGL ES/software remain deferred.

Primary API references: https://webassembly.github.io/spec/web-api/,
https://fetch.spec.whatwg.org/#concept-body-fully-read and
https://streams.spec.whatwg.org/#readablestreamdefaultreader-read-all-bytes.
