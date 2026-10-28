# Native WebTransport integration contract

Status: implementation in progress. The standalone dependency gate passed real HTTP/3 datagram and reliable stream echoes, negative TLS controls and endpoint shutdown. It has not established browser support. Build with `WT=1` only for owned gates until the combined browser, certificate, origin and lifecycle checks pass.

## Ownership split and native primitives

Rust owns the actual wtransport 0.7.2/QUIC I/O, one shared two-worker runtime, document/session/stream handles and bounded event buffers. C++ owns per-JsCtx callback maps, immutable document Origin, secure-context and `connect-src` checks, V8 allocation and teardown. The JS facade owns promises and standard stream adapters. None of the Rust workers may enter V8 or reference a JsCtx.

Place the Rust crate at `src/net/webtransport-native`, with the C header in its `include` directory. C++ primitives follow this fixed shape:

- `N.wtNativeAvailable()` returns a real linked-backend Boolean.
- `N.wtCreate(configJSON, eventCallback)` returns a session ID or throws. Page configuration contains `url`, `certificateHashes` and `requireUnreliable`; C++ replaces/adds `origin` from the actual document after checking native policy. C++ creates one Rust owner lazily per JsCtx.
- `N.wtCommand(sessionId, streamId, operation, requestId, code, payload)` maps the operation constants in `lumen_wt.h`. A missing payload is zero length; an ArrayBuffer is copied before return. Synchronous invalid-state/quota/policy errors throw; I/O completion is asynchronous.
- `N.wtResourceCounts()` is an internal test hook exposing actual owner resource counts. It is not WebTransport network stats.

C++ drains `lume_wt_poll(owner)` from `js_tick` after a global thread-safe wake. It copies the JSON fields and, if present, payload into an event object's `data` ArrayBuffer, invokes the registered session callback only in the owning V8 context, and frees the Rust event exactly once. Remove a session callback after its terminal `closed` event. On navigation/free, invalidate callback maps first and then drop the Rust owner. A global wake callback has no document pointer and remains valid for the process lifetime.

## Event envelope

Every event includes `sessionId`. All asynchronous operation results/errors include their original `requestId`.

- `ready`: `{type:"ready",sessionId,maxDatagramSize,alpn:"h3",reliability:"supports-unreliable"|"reliable-only"}`. Emit only after a real successful handshake and session acceptance.
- `result`: `{type:"result",sessionId,requestId,operation,streamId?,direction?,done?}`. A received payload is the separate binary event buffer; `done:true` means actual receive EOF. A create/accept result supplies the real stream handle and direction `"bidirectional"`, `"send"` or `"receive"`.
- `error`: `{type:"error",sessionId,requestId,name,message}`. Messages are bounded generic categories, without certificate chains, payloads, credentials or URLs. The associated operation rejects; transport termination is represented separately.
- `stats`: `{type:"stats",sessionId,requestId,udpBytesSent,udpBytesReceived,datagramPayloadBytesSent,datagramPayloadBytesReceived,streamPayloadBytesSent,streamPayloadBytesReceived}`. Names preserve the distinction between actual UDP counters and application payload counters.
- `closed`: `{type:"closed",sessionId,clean,localCloseRequested,closeCode,reason,errorName?,errorMessage?}`. Emit only after the dedicated endpoint and stream jobs have closed. An unexpected remote/transport failure rejects the JS `closed` promise. A local close resolves it after cleanup. This first backend uses one dedicated QUIC connection per session; graceful peer CLOSE_WEBTRANSPORT_SESSION-code interoperability is a separate explicit limitation until implemented and tested.

No process/backend resource may be marked closed merely because a JS cleanup callback ran.

## Bounds and cancellation

The initial budgets are four sessions per document, thirty-two streams per session, 128 streams per document, a 64KiB read/write chunk, a 4MiB aggregate pending send payload budget per document, and a separate 4MiB/256-event copied event queue. Command queues and outstanding pull credits are bounded. A full copied event queue closes that owner's transports and emits bounded terminal errors after discarding queued payloads; it cannot grow without limit or leave operation promises pending forever. Protocol/library allocations remain distinct and must be measured through RSS.

Readable adapters issue one native read only when their `pull()` is called. They do not run a continuous JavaScript polling loop. Writable sink promises settle only after native I/O has accepted the bytes, and queue pressure uses the repaired WritableStream semantics. Read cancellation sends STOP_SENDING; write abort sends RESET_STREAM; writer close sends FIN and waits for its actual completion. Document cancellation signals all jobs, closes endpoints, suppresses page events, and waits at most two seconds before force-aborting remaining owned jobs. Global workers sleep while idle.

## Security and explicit gaps

The native document policy must run even if a page tampers with JS globals. Rust also validates URL/config shape and uses either normal native roots or the constrained SHA-256 verifier that checks certificate validity, ECDSA P-256, and the maximum fourteen-day period. No trust-store change or insecure verifier is used. Origin is immutable native input; arbitrary page headers and cookie import are not part of this protocol.

BYOB byte-stream behavior is currently unsupported in the existing ReadableStream layer and must be reported separately. Pooling, non-default congestion policies, custom application protocols, graceful remote session close metadata and complete aggregate WebIDL statistics remain explicit gaps unless corresponding code and independent tests are added. These gaps must not be described as Safari parity. Neither a linkable static library nor a passing facade mock test authorizes exposing a public constructor by itself.
