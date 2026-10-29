# Wasm compiled module cloning candidate (2026-10-15)

Update: device checks now recorded in [integrated Android report](android-device-integrated-20261015.md).
That report supersedes the historical installed-build/test-window status below.

Status: host functionality verified; Android build verified; device runtime
verification pending. This candidate also includes the pending page foreground
task wakeup change. The installed phone APK remains the streaming checkpoint.
Full browser Wasm support and the daily-browser goal remain unfinished.

## Implementation

`src/js/clone.cc` adds V8 serializer/deserializer delegates that retain compiled
modules in move-only in-memory messages. Dedicated workers reconstruct distinct
module wrappers using the retained compiled code. Page structuredClone uses a
native module brand check before consulting JavaScript tags/properties, preserving
module aliases in the existing graph traversal. Modules in transfer lists are
rejected with DataCloneError for page/worker structuredClone and Worker messages.

Message buffers and compiled-code references now have automatic ownership.
Failed serialization and abandoned queues release their references. Worker
shutdown releases queued native code outside its mutex. Posting looks up the
worker after serialization because an author getter can terminate the worker;
posting and termination also verify the calling worker owner.

The delegates apply to dedicated-worker channels, not arbitrary cross-origin
window channels or persistent storage. Frame messaging and generic structured
clone/transfer conformance need further work. SharedArrayBuffer/shared Wasm
memory transport is still unsupported; crossOriginIsolated remains false.

## Host results

Prebuilt V8 15.6.75.8, native jsrun, all runs with whole-process and all-thread
sampling at 1 Hz throughout. Each run lasted 19 seconds and recorded 20 samples.

| Fixture | Result | Idle mean CPU | Peak RSS |
| --- | --- | --- | --- |
| wasm-clone | 15/15 pass | 0.778% | 96.16 MiB |
| wasm-streaming | 27/27 pass | 0.444% | 86.46 MiB |
| wasm-worker | 8/8 pass | 0.556% | 171.05 MiB |
| wasm-browser | 12/13 pass | 0.556% | 84.38 MiB |
| worker-ports | 3/3 pass | 0.555% | 81.91 MiB |

Idle figures use nine samples between six and fifteen seconds after process
launch. Peak idle CPU was at most 2.001%; the maximum sample gap was 1.008s.
These are host functional measurements, not Android performance comparisons.
jsrun polls at most every 5ms, so it cannot verify the UI event-loop wakeup fix.
The broad audit's remaining failure is shared Wasm memory to a worker.

The clone fixture covers executable distinct wrappers, metadata/custom sections,
aliases/cycles/Map/Set, native branding, transfer rejection, page-worker round
trips, worker compilation, serialization failure recovery, concurrent workers,
sender termination, abandoned messages, and a getter terminating its destination.
Passing these checks does not establish complete structured-clone conformance.

Streaming negative cases emit handled-rejection diagnostics despite their
expected error assertions passing. The broad audit also logs a terminated worker
prelude diagnostic. No runtime crashed; fixture assertions, not process exit
alone, determine the counts above.

## Android artifact

Candidate:
`build/codex-wasm-clone-20261015/lumen-vulkan-wasm-clone-debug.apk`

SHA256: `ebc5d77a87414133d867ec0756b5677cd75087196e5e1e24672ca88dd052242b`

Android compilation succeeded through codex_compile.py using the verified
prebuilt V8. Native ELF/dependency validation and APK v2 signature verification
passed. This artifact has NOT been installed or tested on Android.

Artifacts in the same directory include android-build.log, per-run stdout/stderr,
process/thread samples, summaries, server.log, and broad/streaming/worker result
JSON. wasm-clone-results.json contains the 15-check result array, also logged in
final-clone.stderr. Result counts were explicitly asserted after runtime tests.
All owned host test/build/server sessions finished and were joined; no phone
reverses were created. The old V8 source build remains frozen.

## Next device verification

The phone changed tabs and opened Settings during the earlier latency baseline.
The separate request for an untouched testing window is still pending. Do not
install/restart the app until that is resolved. Repeat the old APK's timing
baseline first, then test the standalone page-wakeup candidate as described in
android-wasm-page-wakeup-20261015.md. Afterward install this combined candidate
and run /wasm-clone, /wasm-browser, /wasm-streaming, /wasm-worker and worker-ports.
Check root tab closure/reopen, worker termination and settled CPU/RAM on Vulkan,
with whole-process/all-thread 1 Hz sampling throughout.

Then address isolation/shared-memory lifetime, incremental worker fetch, wider
engine conformance and real Wasm applications. Vulkan scrolling, YouTube, login
and Chrome-style daily features remain active work; OpenGL ES and software
optimization remain deferred by the user's priority.
