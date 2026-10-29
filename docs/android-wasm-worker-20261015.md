# Android Wasm worker scheduling (2026-10-15)

Vulkan remains selected. Core V8 Wasm was enabled already, but asynchronous
compilation in a dedicated worker could stall until an unrelated message/timer
woke it. The worker also omitted a microtask checkpoint after foreground tasks.

The V8 platform now forwards worker foreground tasks to an event-loop queue
registered between isolate allocation and initialization. Posting increments a
condition-variable generation counter, preventing lost notifications. Delayed
tasks supply the next wait deadline; idle workers do not poll. Foreground tasks
run only at the outer loop level, followed by a microtask checkpoint. Queue
shutdown discards pending tasks outside its lock and rejects late posts safely.
Background compilation and page task runners remain on the default V8 platform.
Root page disposal also notifies the default platform to release its task queue.

Validation on MI8/Android15 with the final APK:

- ASan/UBSan host queue tests pass: ordering, delayed deadlines, waking a blocked
  consumer, recursive posting/destruction, and 4,000 posts racing queue shutdown.
- Eight phone integration checks pass: startup compilation/self-close, async
  compile/instantiate without worker timers, rejected compilation/recovery,
  12 pending compilations, four concurrent workers, 20 create/compile/terminate
  cycles, eight termination cycles after scheduling 8,192-function modules,
  and compilation after the worker's idle-GC deadline.
- Example measured round trips: startup16.25ms, ordinary worker7.48ms, after
  idle2.74ms; the worst of20 fresh worker cycles was14.83ms. These are small
  fixture measurements, not general Wasm application performance guarantees.
- The original browser integration audit improves from7/13 to8/13 passing.
  Streaming APIs and compiled-module/shared-memory cloning still fail. This
  change does not establish full browser Wasm conformance.

The first stage named `wasm-worker-settled-idle` was contaminated by a switch to
YouTube; it is explicitly NOT idle evidence. The user subsequently confirmed
phone availability, and the final test was restarted on the Wasm fixture.
The final idle interval contains75 samples: mean CPU0.174%, peak4.04%, RSS
240.64–272.93MiB, ending240.64MiB. The test/load stage contains33 samples,
mean6.61%, peak200.98% during compilation. No comparative RAM gain is claimed.
Closing the fixture tab with its resident worker succeeds without crashing.
Vulkan selection is confirmed in the final app log.

Evidence: `build/codex-wasm-worker-20261015/`, including per-process/all-thread
1Hz samples, server JSON reports, screenshots and APK verification/build output.
Final APK: `lumen-vulkan-wasm-worker-debug.apk`, SHA256
`09eaabc3e581fdcfaeef8266456cf16cb782e8a427d3c56691bb16bd6fd7958a`.
Native layout/dependency and v2 signature verification pass. All compilation
used `tools/codex_compile.py`; the old V8 source build was not resumed.
All553 whole-process/all-thread samples have zero cadence/parse errors. App
stopped, monitor/server stopped and joined, owned reverse ports removed.
