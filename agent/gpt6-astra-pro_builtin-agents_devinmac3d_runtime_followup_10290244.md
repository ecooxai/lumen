# Runtime follow-up for task 10290244

Prepared against the frozen native artifact `1b9d43ed19cd616b0029e34fde97050b86c9acbdbda16a0dca5892922df704e9`, checkpoint `436916ceb3b32630ed251bb666b02fa39a3cd0c8`. This is a next-session design note, not an implemented fix or additional conformance credit. Final resource measurements must finish before another build starts.

## First fix: real ArrayBuffer transfer through MessagePort

The two remaining upstream `encodeInto()` detached-output failures use `new MessageChannel().port1.postMessage(buffer, [buffer])`. The current window `07b-net.js` MessagePort path filters the transfer list to ports and calls `structuredClone(d)` without its transfer list. The worker MessagePort path likewise filters to ports and calls `W.clone(d)`. Neither path detaches the supplied ArrayBuffer. The encoder therefore sees a live destination and writes four bytes. The new codec's separate genuine-detachment controls pass; changing the encoder to pretend these live buffers are detached would conceal the actual defect.

### Existing native machinery to inspect and reuse

`worker.cc` already has V8 serializer/deserializer helpers, `Buf` ownership, and a `Side` object retaining shared backing stores and compiled WebAssembly modules. `WFN(clone)` currently uses those helpers without a transfer list. `JsCtx::alloc` is already shared because transferred stores may outlive their source isolate. First read the current `src/js/windows.cc` directed-message transfer implementation and its tests: that implementation was not included in this lane's final source mirrors, so its reusable transfer support must be verified before creating another serializer. `frames.cc` also contains a serializer-based same-isolate message path. Preserve the existing `JsgCloneDelegate` platform-brand rejection behavior.

Proposed implementation: one native structured-clone envelope containing serialized bytes, transfer IDs, retained ArrayBuffer backing stores, and transferred endpoint ownership. Use the installed V8 serializer/deserializer transfer APIs after checking their exact signatures in the pinned V8 headers. Transfer metadata must preserve shared references between a transferred buffer, every typed-array/DataView view of it, and repeated payload references. Reconstruct values inside the recipient context; never pass V8 local handles across worker threads. Extend the existing worker sidecar rather than copying raw buffer pointers into queues.

Normalize both legacy transfer-list and options overloads without silently discarding invalid entries. Follow the HTML algorithm's validation, serialization, and transfer ordering; synchronous sender detachment and recipient reconstruction are separate operations. Duplicate/invalid transfer items and serialization failures need their specified exceptions and side-effect ordering. SharedArrayBuffer remains shared rather than transferable. Do not promise rollback of an irreversible transfer merely because later work fails. The authoritative ordering is [HTML StructuredSerializeWithTransfer](https://html.spec.whatwg.org/multipage/structured-data.html#structuredserializewithtransfer), with recipient reconstruction in [StructuredDeserializeWithTransfer](https://html.spec.whatwg.org/multipage/structured-data.html#structureddeserializewithtransfer).

### RED tests before edits

1. Local channel: sender buffer and all sender views become detached before `postMessage` returns; asynchronously delivered bytes and view offsets are correct. Include a listed buffer absent from the payload.
2. Identity: two payload references and multiple overlapping views share the same reconstructed recipient buffer; cyclic containers remain cyclic.
3. Failure order: duplicate buffers, SharedArrayBuffer in the transfer list, nontransferable values, already-detached buffers, and a function in the payload. Test exact exceptions and sender state after each failure against the primary algorithm; include a payload getter with side effects.
4. Dedicated worker in both directions and a port transferred to a worker: retain the backing store until delivery or queue disposal; a terminated worker releases undelivered envelopes. No source-isolate allocator or context may be freed while a retained store still depends on it.
5. Same-origin iframe realm, frame removal/navigation before delivery, close/start behavior, and port entanglement after transfer. Capture callback realm, prototype identity, and owned endpoint lifecycle.
6. Re-run the two unchanged upstream encoding variants, the 170 codec checks, all existing clone-brand and window-message/worker tests, and a bounded 1 Hz navigation/worker termination soak. Keep SharedWorker explicitly unsupported until that host is implemented; do not create a stub to pass exposure checks.

## Second fix: stream interfaces and legacy decoder state

Current window `TextDecoderStream` subclasses `TransformStream`, ignores the decoder options argument, calls one-shot `decode(c)` independently for each chunk, and has no final flush. Dedicated workers do not expose the equivalent complete interfaces. The first 28 completed encoding variants contain 24 stream-interface/IDL failures and eight explicitly unsupported ISO-2022-JP streaming failures. The remaining 23 legacy form/query encoder failures are a separate submission-encoding path.

Implement private stream state around an internal TransformStream, expose the specified readable/writable and codec getters with receiver checks, and share the implementation with workers. Decode chunks through a persistent decoder and flush once on closure. Preserve pending UTF-8/UTF-16 state and BOM/error options; an encoder stream also needs a pending leading surrogate across chunks. Add bounded native state for legacy multibyte encodings, especially ISO-2022-JP shift/escape state, before claiming those streams are supported. Keep input queues bounded during normal operation; do not accumulate the entire stream. These requirements follow the [Encoding Standard stream interfaces](https://encoding.spec.whatwg.org/#interface-textdecoderstream).

RED gates should cover every byte split of a multibyte scalar and BOM, surrogate pairs split between string chunks, incomplete final input under fatal/nonfatal modes, invalid brands, options and cross-realm getters, worker exposure, backpressure/error/cancel behavior, and decoder reset after final flush. Retain the current one-shot legacy decoder behavior and all 89 native encoding checks. The existing four narrow Chrome codec discrepancies remain documented; they are not authority to weaken current standard-based expectations.

## Apple artifact boundary

The recovered, intact CLT compiler and linker produced actual arm64 UIKit smoke executables from the installed SDKs. Their measured Mach-O build platform values were **2 for iOS device** and **7 for iOS Simulator**. These were SDK smoke programs, not Lume builds. Matching arm64 CPU architecture alone therefore does not make the simulator artifact the device IPA. The next attempt needs platform-matched dependencies before linking Lume: all six declared iOS/simulator/Catalyst prefixes and all 21 required archives were absent, and the corresponding Apple V8 variants were absent. Existing Homebrew V8 15.6.75.16 supports the measured native Mac path; it has not been demonstrated to close the iOS dependency graph.

No signing identity, existing Lume IPA, or installed task-iPad Lume app was recovered. A genuine same-IPA claim requires the same signed iOS-device artifact hash to launch on an eligible Apple-silicon Mac and the intended iPad/iPhone. A separately built native Mac executable, Catalyst target, or simulator executable does not provide that evidence. Preserve the successful SDK receipts and recover a verified platform-specific prebuilt dependency bundle before considering a long V8 build.

## Final task-owned process cleanup

Do this only after the final WPT/resource runs and evidence saves finish. Keep the user-requested progress server **PID 48803** and OpenTunnel **PID 31096** alive. The WPT server last reported as **PID 23670** is a candidate for cleanup, not an authorization to signal whichever process later reuses that PID.

For the WPT server and each task-browser child, capture current PID, PPID, start time, executable/arguments, and working directory. Compare these with this task's launch receipt and profile/output paths; confirm the exact process is still owned by this task. Signal only verified PIDs with SIGTERM and wait a bounded five seconds for exit. A remaining verified process can be reported for deliberate follow-up; uncertain ownership means leave it running and document why. Do not use killall/pkill, broad process-name matches, simulator resets, or global service cleanup. Do not stop the live progress/tunnel descendants. Record before/after identities and each signal in a cleanup receipt.

The final resource runner already records natural app/sampler exit and raw sample hashes. Any extra process cleanup is a separate post-test action and must not retroactively convert a forced or incomplete measurement into a passing gate.
