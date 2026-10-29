# Page V8 task wakeup candidate (2026-10-15)

Status: device latency verification PASS. The isolated Android comparison now
shows cold compilation falling from about 1003–1008 ms to 1.1–4.1 ms, with 6/6
checks passing. See android-device-integrated-20261015.md for evidence and limits.
The historical pending-test notes below are superseded by that report. The full
browser goal remains active.

## Evidence and change

The installed streaming APK's page tasks use the default V8 foreground runner.
The main event loop pumps that runner but receives no wakeup when compilation
posts a completion, and js_next_deadline omits its deadlines. The idle loop can
sleep1000ms. The new timing fixture observed six small unique compilations at
1003–1009ms, plus800ms delayed-response compilations at1818–1886ms.

These baseline timings are provisional: the phone subsequently opened extra
tabs and moved to Settings. The stage named baseline-idle is explicitly INVALID
as idle evidence; measured CPU includes active use. Repeat the baseline with
the phone untouched before presenting an uncontested before/after comparison.
The user has been asked asynchronously for a three-minute untouched window;
no answer yet at this checkpoint. Do not interrupt their current phone use.

The candidate registers a foreground queue before page isolate initialization,
captures only the process-lifetime UI wake callback, includes delayed task
deadlines in js_next_deadline, and drains at the root tick with microtask
checkpoints. Frames share that root queue. The existing worker queue was renamed
JsForegroundTasks and reused; background compilation stays on libplatform.
RunOne rejects nested draining, and Close rejects late tasks without retaining
page pointers. Root shutdown closes the queue and unregisters after Dispose.

Source audit caught an important cleanup constraint: default-platform.cc's
NotifyIsolateShutdown unconditionally dereferences its runner after lookup.
Root isolates now never register there, so the candidate removes that call and
uses its own queue cleanup, matching the existing worker lifecycle.

## Completed verification

- ASan/UBSan queue test passes: deadlines, wakes, ordering, nested-run exclusion,
  recursive task destruction and4000 posts racing shutdown.
- Android build succeeds through codex_compile.py with frozen old source V8
  left untouched. Prebuilt V8 is used, Gradle --no-daemon.
- Native APK ELF/dependency verification passes; signature verification is
  recorded with the build artifact.
- Python fixture server syntax and git diff whitespace checks pass.

Candidate: build/codex-wasm-page-wakeup-20261015/lumen-vulkan-page-wakeup-debug.apk

SHA256:76b1031a246f1f237b9e4d7517d8cf341e6a68e3acccb0d340a159262f1c7be5.
The earlier candidate hash db0d3d... is superseded and was NEVER installed.
The phone still has the prior streaming APK (42af2d...), not this candidate.

## Required next checks

1. Confirm phone is free using the pending answer; start1Hz process/all-thread
   monitoring before testing. Reverse18765/18766 and start the fixture server.
2. Repeat /wasm-page-wakeup on installed old APK in an uncontested run. The
   fixture now marks prompt=false timing checks as failures; the initial
   baseline file recorded them as pass=true with detail.prompt=false. Compare
   actual elapsed values, not just the initial pass flags.
3. Install the corrected candidate and run the same fixture. Six checks include
   six cold compilations (<250ms each), three delayed streams (<1100ms each),
   microtasks, rejection/recovery,24 pending compilations and eight frame cycles.
4. Run streaming27/27 and worker8/8 regressions. Test root tab closure and reopen,
   delayed task wakeup/idle-GC behavior, and record idle CPU/RAM. Confirm Vulkan.
5. Only then describe the performance change as verified and commit the change.

Artifacts include baseline.json, baseline-invalid-idle.txt, baseline-idle.png,
server.log, samples.jsonl, test_foreground_tasks and build.log. Broader module
cloning/shared-memory conformance, Vulkan scrolling, YouTube, login and zoom
requirements remain open.
