# WebAssembly browser support audit (2026-10-15)

The user explicitly added FULL browser WebAssembly support to the active daily
browser goal. This is not satisfied by an enabled V8 build flag or an arithmetic
smoke test. Vulkan remains the priority backend; this work concerns shared engine
and browser APIs, not a switch to OpenGL ES/software rendering.

Current Android configuration enables V8 WebAssembly (cmake/AndroidV8Prebuilt.cmake).
Phone evidence: tests/js/wasm-browser.html via tests/fetch_preflight_server.py,
build/codex-native-touch-20261015/server.log. The installed native-touch APK passed
7 of13 initial integration checks:

| Capability | Phone result |
| --- | --- |
| Sync module validation/execution | Pass |
| Async compile/instantiate on page | Pass |
| Memory growth and preserved bytes | Pass |
| i64/BigInt boundary | Pass |
| SIMD instruction execution | Pass |
| Bulk memory instruction execution | Pass |
| Runtime traps | Pass |
| compileStreaming(fetch) | PASS after streaming integration |
| instantiateStreaming(fetch) | PASS after streaming integration |
| structuredClone(compiled module) | FAIL: loses module identity |
| Compilation inside Worker | Initially timed out; PASS after worker task wakeup fix |
| Compiled module postMessage to Worker | FAIL: module could not be cloned |
| Shared Wasm memory postMessage to Worker | FAIL: SharedArrayBuffer could not be cloned |

Follow-up: the worker task fix raises this audit to8/13 passing. It registers a
foreground queue before worker isolate initialization, wakes the worker on task
posting, preserves delayed deadlines, and runs microtasks after foreground work.
See android-wasm-worker-20261015.md for implementation and device measurements.

Streaming follow-up raises the audit to10/13. All27 focused streaming checks pass
on the MI8 Vulkan build, including incremental page fetch, MIME/status validation,
body use/locking, cancellation, malformed/truncated bodies, same-origin frame
Responses and worker API calls. Worker fetch remains buffered internally. See
android-wasm-streaming-20261015.md.

Source corroboration: wasm.cc now registers V8 streaming callbacks. worker.cc uses
ValueSerializer/Deserializer without delegates for modules/shared backing stores.
The page structuredClone JS implementation does not handle modules. The former
worker loop was not notified by V8 foreground-task posting and omitted the
post-task microtask checkpoint. The phone now confirms this timeout is fixed.

Next implementation/verification requirements:

1. Extend worker fetch to actual incremental network streaming and broaden
   Response/Streams conformance, including prototype overrides and BYOB. Page
   streaming integration is implemented; focused checks are not full conformance.
2. Worker task wakeups are fixed. Page V8 completion still incurs up to the
   event-loop fallback wait: delayed800ms fetch compiled in1812ms on the phone.
   Connect page foreground tasks to wakeups/deadlines without polling.
3. Structured clone and worker message support for modules/shared backing stores,
   including lifetime and isolation checks. Do not silently convert them to plain
   objects or bypass cross-origin isolation requirements.
4. Broader engine/API conformance: imports/exports, tables/globals, reference types,
   exceptions, GC, supported memory64/multi-memory features, atomic instructions,
   shared-memory threads, invalid binaries and error classes. Compare against
   applicable stable browser behavior; experimental proposals are not implicitly
   required to be enabled.
5. Run representative real Wasm applications on the phone, test worker termination
   and memory release, and measure CPU/RAM under load and after settling. Expand
   the initial13 checks; they are a gap detector, not a full conformance claim.

Status: core V8 Wasm works; full Lumen browser Wasm support is NOT complete.
