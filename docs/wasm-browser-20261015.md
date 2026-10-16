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

Host-only cloning follow-up: the uninstalled candidate passes12/13 broad checks,
15/15 focused module cloning checks,27/27 streaming and8/8 worker checks.
clone.cc retains compiled modules through dedicated-worker serialization and
provides native module cloning to the page prelude. Shared memory transport still
fails. See android-wasm-clone-20261015.md. The installed phone remains at10/13;
do not report the host result as Android verification.

Source corroboration: wasm.cc registers V8 streaming callbacks. The former
worker loop lacked V8 foreground-task wakeups and post-task microtask checkpoints;
the phone confirms that timeout is fixed. Page wakeup/deadline integration is
also implemented in the current candidate, but Android timing verification is
pending an uncontested testing window.

Next implementation/verification requirements:

1. Extend worker fetch to actual incremental network streaming and broaden
   Response/Streams conformance, including prototype overrides and BYOB. Page
   streaming integration is implemented; focused checks are not full conformance.
2. Verify the candidate's page foreground wakeups/deadlines on Android. The old
   event-loop fallback can delay completion by1000ms; repeat the uncontested
   baseline before claiming a measured improvement.
3. Verify implemented module cloning on Android, extend frame/transfer conformance,
   and implement shared backing stores with lifetime and isolation checks. Do not
   silently convert them to plain objects or bypass cross-origin isolation.
4. Broader engine/API conformance: imports/exports, tables/globals, reference types,
   exceptions, GC, supported memory64/multi-memory features, atomic instructions,
   shared-memory threads, invalid binaries and error classes. Compare against
   applicable stable browser behavior; experimental proposals are not implicitly
   required to be enabled.
5. Run representative real Wasm applications on the phone, test worker termination
   and memory release, and measure CPU/RAM under load and after settling. Expand
   the initial13 checks; they are a gap detector, not a full conformance claim.

Status: core V8 Wasm works; full Lumen browser Wasm support is NOT complete.
