# Native WebTransport: measured implementation profile

This backend is under integration. A constructor must stay disabled until the
real C ABI, V8 policy/lifecycle and independent browser lab gates pass. Unit
fixtures and a standalone Rust client are separate evidence categories.

The backend uses pinned `wtransport` 0.7.2 with QUIC, rustls and ordinary native
trust roots. The optional certificate-hash mode uses the dependency's bounded
certificate verifier, not an accept-all TLS callback. A trusted C++ boundary
supplies document Origin after native secure-context/ancestor and `connect-src`
checks. Page JSON, prototypes and `toJSON()` cannot replace that Origin.

## Implemented facade paths

- `WebTransport(url, options)`, genuine asynchronous `ready`, and `closed` settled
  after the dedicated native endpoint has stopped.
- Demand-driven datagram reads and writes with the actual negotiated size bound.
- Outgoing bidirectional/unidirectional streams and incoming stream acceptance.
- Reliable writes split into bounded 64KiB native requests, native FIN completion,
  partial reads, actual EOF, STOP_SENDING on read cancellation and RESET_STREAM
  on writer abort. The writable controller's abort signal interrupts a blocked
  send before the normal underlying sink abort callback would run.
- Default stream creation checks actual available QUIC stream credit once;
  `waitUntilAvailable:true` waits for peer credit. Both paths await real stream
  preface I/O after credit is acquired.
- `WebTransportError` application reset codes, certificate-specific security
  failures, and rejection of all outstanding promises on terminal failure.

Datagram and incoming-stream readables each own one native pull credit. Canceling
one of those readables permanently cancels its route, settles its native pending
credit, and leaves unrelated streams and sessions usable. A cancellation race
cannot silently orphan an accepted stream: native publication is serialized
against route cancellation, and a result already published but delivered after
the JS cancellation is explicitly stopped/reset by the facade.

There is no JavaScript polling loop or keepalive timer. The two native workers
sleep while idle and wake the owning browser event loop when work is available.

## Resource accounting

| Resource | Enforced initial budget |
| --- | --- |
| Native workers | Two, plus at most two blocking resolver workers |
| Sessions | Four per document, including unpolled terminal notifications |
| Streams | Thirty-two per session, including reservations |
| Native outstanding requests | 128 per session |
| Individual native read/write buffer | Reads 16KiB; writes 64KiB |
| Copied native event queue | 4MiB and 256 events per document, including metadata |
| Native pending send payloads | 4MiB aggregate per document |
| Facade-owned write snapshot reservations | Separate 4MiB aggregate across all document sessions |
| Per-session QUIC send/receive windows | 1MiB each; 64KiB stream receive window |
| Per-session QUIC datagram queues | 64KiB receive and 64KiB send |
| Endpoint close deadline | One second |
| Document teardown deadline | Two seconds, then owned tasks are aborted |

The facade reserves its shared copy budget before copying a caller's view and
releases the reservation in `finally` on success, error or cancellation. The
normal WritableStream queue still holds caller-owned chunks when a caller keeps
writing despite backpressure. Those references are distinct from owned snapshots.
Transient 64KiB chunk buffers and native payload copies are additional memory;
the snapshot reservation is not a total memory limit. A failed reliable sink
write sends one native reset while preserving its original JavaScript rejection.
Protocol allocations, V8 objects, allocator retention and thread stacks are also
outside the payload budgets; process-tree CPU/RSS is measured independently.

Terminal events survive event-queue overflow. Queued metadata counts toward
memory accounting, zero-byte datagrams remain valid data, and retired session
handles keep their quota until the corresponding terminal event is polled.

## Explicit gaps

This is an initial datagram/reliable-stream profile, not full Safari or current
WebTransport specification conformance. BYOB byte readers are unavailable.
Connection pooling, custom headers, application protocol negotiation, stream
priority/send groups, non-default congestion policies, keying-material export,
atomic/committed writes and complete public `getStats()` are not implemented.
Unsupported non-default options reject rather than silently changing their
meaning. Public `getStats()` and `draining` remain absent; native diagnostic
statistics retain distinct names for actual UDP bytes and application payloads.

The dependency's close API closes this dedicated QUIC connection. It does not
yet implement interoperable graceful CLOSE_WEBTRANSPORT_SESSION metadata.
`closed` verifies actual local endpoint cleanup; it is not proof that the peer
received the requested application close code or reason. Remote failures reject
the lifecycle promise. Full graceful peer-close interoperability remains gated.

The immutable Lumen descriptor exposes `webTransportBackend`,
`webTransportBYOB:false` and `webTransportGracefulRemoteClose:false`, in addition
to the real linked `webTransport` Boolean. A build with the backend disabled
does not create a public constructor.

## Verification layers

The JS semantic suite runs against both Node's reference streams and the actual
repaired Lume stream source. Its native callbacks are deliberate fixtures: they
verify promise ownership, queue pressure, cancellation, errors and lifecycle,
not HTTP/3. The Rust unit suite similarly separates control-plane fixtures from
network tests.

The C ABI example uses only exported functions against an independently
implemented HTTP/3-only aioquic server. Acceptance requires fresh datagram and
fragmented reliable-stream echoes through EOF, cancellation and independent
document continuation, wrong-owner rejection, a wrong certificate pin and normal
trust-root rejection of the self-signed endpoint, and all actual counters zero
after teardown. A separate real V8 gate tests native policy and copied buffers.

The final browser gate runs the actual lab in an owned Lume process, checks the
exact same application payloads at the independent server, awaits stream EOF and
transport closure, and records page cleanup plus native resource counters.
Linux component controls do not establish macOS/iOS/iPad browser acceptance.

## Primary references

- [WebTransport working draft](https://w3c.github.io/webtransport/) — accessed
  2026-10-21, draft dated 2026-10-19; API lifecycle, close-reason truncation and
  application error-code semantics.
- [WHATWG Streams](https://streams.spec.whatwg.org/) — writable backpressure,
  underlying controller abort signal and readable pull/cancel semantics.
- [Pinned wtransport source](https://crates.io/crates/wtransport/0.7.2)
  — actual open-stream cancellation points, verified TLS modes and close limits.
- [HTTP/3 WebTransport protocol](https://datatracker.ietf.org/doc/draft-ietf-webtrans-http3/)
  — protocol context; implementation claims are based on actual independent
  transport gates, not constructor existence.
