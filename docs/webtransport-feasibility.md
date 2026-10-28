# WebTransport / HTTP3 / QUIC implementation assessment

Date: 2026-10-21. This is an implementation assessment, not a support declaration.

## Observed state

Lumen is a custom C/C++/V8 browser. Its immutable capability descriptor currently reports `webTransport: false`. The inspected native network sources and JavaScript bindings contain no WebTransport/HTTP3 client implementation, and the current target dependency directories contain no quiche, ngtcp2, nghttp3, or wtransport build. The Python aioquic process installed for the capability lab is an independent HTTP3-only test server; it does not add a browser client backend.

The development Mac has Rust/Cargo 1.99, CMake 4.4.4, and OpenSSL 3.6.5. Only the `aarch64-apple-darwin` Rust target is currently installed. The app's iPhone/iPad and simulator targets therefore require additional cross-target builds and integration verification. These are observations from the read-only transport-prerequisite inventory, not assumptions about available Apple packaging.

A datagram echo through HTTP3 can establish real QUIC transport evidence. It cannot establish support for the full WebTransport API, general HTTP3 page loading, media WebRTC, or Safari parity. The lab keeps those claims separate.

## Candidate route

A pinned Rust `wtransport` client behind a small C ABI is a plausible bounded route. Its published 0.7.2 API already includes WebTransport session transport, native certificate-store configuration, and explicit SHA-256 certificate-hash configuration with certificate constraints. This avoids implementing QUIC, HTTP3 session negotiation, and TLS in the browser project itself. It still requires a browser-specific boundary; the upstream library is not a drop-in implementation of the JavaScript API. [1][2]

The first experiment should be a standalone native client, without exposing `window.WebTransport`. Build a pinned static library with a lockfile, run a fresh nonce exchange against the existing aioquic HTTP3-only endpoint, and obtain the matching server-side HTTP3/QUIC telemetry. Exercise both datagrams and a reliable stream. A certificate-hash mismatch must fail. An endpoint without the correct WebTransport negotiation must fail. This narrow experiment answers whether the pinned protocol versions interoperate before the browser gains another native dependency.

The example client upstream uses an optional certificate-verification bypass. That example configuration is unsuitable here. Use normal platform roots for ordinary HTTPS endpoints and the constrained hash mode only when the calling API supplies valid certificate hashes. The documented hash mode checks the certificate validity interval, a lifetime no longer than two weeks, and an accepted ECDSA key algorithm. [1]

## Browser work required after the native experiment

1. **Session and policy boundary.** Validate HTTPS URLs and constructor options, require a secure context, enforce the document's applicable connection policy, send the actual origin, and keep credentials omitted. Ordinary trust failures and invalid certificate pins must remain failures. WebTransport sessions are HTTP sessions with their own policy and termination rules; a raw QUIC socket is insufficient. [3]
2. **Document ownership and cancellation.** Assign sessions to their creating document and use bounded copied events to reach V8 on its owning thread. Close must cancel pending operations, reject or settle promises appropriately, release streams and sockets, and ignore stale callbacks. Reuse the reviewed RTC bridge's ownership pattern, not its transport or statistics.
3. **Streams and backpressure.** Bind datagram readable/writable sides plus incoming/outgoing unidirectional and bidirectional streams to the browser's actual stream implementation. Apply aggregate byte limits, respect writable backpressure, support cancellation/reset/finish semantics, and reject unsupported options explicitly. Do not report a datagram-only object as complete WebTransport coverage.
4. **Apple targets and acceptance.** Build the pinned Rust dependency for macOS, iOS device, and the actual simulator architecture; link it into each real app target and verify signed-app networking. A single iOS IPA's ability to run on a Mac is a packaging/runtime gate, separate from a macOS static-library build. Run positive and negative transport cases, two-document teardown, repeated open/close, idle CPU, peak RSS, and post-close resource checks on the actual targets.

No continuous polling or keep-alive loop is needed when no session exists. Native waits should be event driven. Session limits, stream limits, buffered-byte limits, and timeouts must be explicit; a small JavaScript queue alone does not bound native transport memory.

## Bounded implementation decision

The next useful investment is one standalone macOS client spike with three outcomes: it builds from pinned sources, exchanges nonce data with the independent HTTP3 server, and rejects a wrong pin. A short build/run time cap should limit the spike while preserving its diagnostic output. Successful completion would justify the browser C/V8/Streams work; a failed spike would leave the capability explicitly unsupported with a concrete protocol, dependency, or trust failure recorded.

Engineering estimate: the standalone proof is a small experiment; the browser boundary and lifecycle/backpressure work are a multi-file feature comparable to, and broader than, the new RTC data-channel bridge. Apple target builds and full JavaScript conformance add separate work. This is not a safe single-function patch or a score-toggle change. The current build should continue to report WebTransport unsupported until a compiled backend passes independent network and lifecycle gates.

## Primary references

1. [wtransport 0.7.2 client configuration](https://docs.rs/wtransport/0.7.2/wtransport/config/struct.ClientConfigBuilder.html) — native roots, certificate hashes, transport limits and idle settings.
2. [wtransport upstream repository](https://github.com/BiagioFesta/wtransport) — client/server architecture and implementation scope.
3. [W3C WebTransport Candidate Recommendation, 30 July 2026](https://www.w3.org/TR/2026/CR-webtransport-20260730/) — browser API, sessions, streams, policy, certificate hashes and context cleanup. This specification also permits an HTTP2 mapping, so a generic WebTransport success alone does not prove QUIC; the lab's HTTP3-only server provides that distinction.
