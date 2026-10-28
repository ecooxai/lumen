# Owned native WebTransport backend

This Rust component implements an actual HTTP/3 WebTransport client over QUIC/UDP using **wtransport 0.7.2**. It is integrated through a narrow C ABI and has no V8 or DOM pointers. Its presence or a successful standalone test does not establish browser API support. The native secure-context, ancestor, CSP and document-origin checks, JS streams facade and actual browser acceptance gates remain separate requirements.

## Dependency and build provenance

`Cargo.toml` pins the transport library and Tokio; `Cargo.lock` records every resolved dependency version and registry checksum. Builds use `--locked --offline` after the reviewed dependency fetch, at most two Cargo jobs, and a project-owned target directory. `build_component.py` obtains the exact target's native linker requirements from `rustc --print native-static-libs`, then records the archive/header/source/lockfile hashes, compiler, target, profile, and dependency license expressions. No framework flags are guessed. The initial debug archive is an integration-test artifact; production packaging must record its actual profile.

The generated installation prefix is `build-deps/webtransport/{macos,sim,ios}/` with `lib/liblumen_webtransport_native.a`, `include/lumen_wt.h`, `manifest.json` and `dependency-provenance.json`. Build outputs, copied archives, process measurements and profiles are not tracked as source. The native build remains `WT=0` until actual browser protocol/security/lifecycle gates pass.

Primary upstream sources:

- [wtransport 0.7.2](https://github.com/BiagioFesta/wtransport/tree/v0.7.2), dual MIT/Apache-2.0 upstream licensing; exact dependency licenses are recorded from Cargo metadata.
- [WHATWG Streams](https://streams.spec.whatwg.org/), for stream ownership and backpressure.
- [WebTransport API](https://w3c.github.io/webtransport/), for the browser contract and certificate-hash constraints.
- [WebTransport over HTTP/3](https://ietf-wg-webtrans.github.io/draft-ietf-webtrans-http3/draft-ietf-webtrans-http3.html), including the 32-bit stream-error mapping that skips HTTP/3 reserved codepoints.

## Ownership and limits

One shared Tokio runtime has two async worker threads and at most two blocking resolver threads. Idle sessions wait for IO/cancellation; no polling loop, periodic keepalive or idle JavaScript timer is introduced. Each native document owns at most four sessions, each with a dedicated QUIC endpoint. There are at most 32 materialized/reserved stream handles per session, 128 outstanding operation requests per session, and one pending read per receive half/datagram/accept route. Handles and request IDs remain JavaScript-safe integers.

An accepted FFI write is copied before return and is at most 64 KiB. Pending application send payload is limited to 4 MiB per document. Independently, copied events are limited to 4 MiB and 256 entries, including metadata and payload. Overflow closes the affected document's transports and reserves terminal error delivery; it never silently leaves operation promises pending. Stream reads allocate 16 KiB only on demand. QUIC has a 1 MiB send window, 1 MiB aggregate receive window, 64 KiB per-stream receive window, and 64 KiB datagram send/receive buffers per session. These bounds do not represent total process RSS: TLS, protocol objects, allocators and the rest of the browser still require measured memory tests.

Only the owner thread polls events. A returned event is exclusively owned by that caller until one `event_free`; both byte and metadata buffers must be copied before free. The global wake callback has no context pointer. Owner destruction first suppresses all events, cancels every task and endpoint, then waits up to two seconds; on timeout it aborts remaining tasks and reports the timeout. A late global event-loop wake cannot deliver to a destroyed document.

## Real IO semantics and explicit limits

Datagram writes resolve when accepted by the real QUIC datagram sender; this alone does not prove delivery. Reliable writes wait for actual stream acceptance and account for partially accepted payload. Finish waits for acknowledged data/FIN. Reads report true EOF separately from zero-length datagrams. Cancelling a pending read interrupts its task and sends STOP_SENDING; aborting a writer sends RESET_STREAM with the mapped WebTransport application code. Payload counters and actual UDP counters have distinct names.

Normal connections use the operating system's native roots. Hash-constrained connections use wtransport's SHA-256 verification with its certificate validity, maximum 14-day lifetime and key constraints. Invalid hashes, missing normal trust or wrong pins are never retried with an insecure verifier. Certificate-specific negative results require an actual TLS certificate failure; a generic network timeout is not accepted as certificate evidence.

The pinned upstream client closes a dedicated underlying QUIC connection. It does not expose CLOSE_WEBTRANSPORT_SESSION capsule emission or remote graceful application close codes. Therefore locally requested close metadata is labelled `localCloseRequested`, while remote closure is an error in this initial profile. Connection pooling, remote graceful session-close metadata, custom congestion control, application protocol negotiation, byte-stream/BYOB readers and complete aggregate WebTransport statistics are not claimed. The JS facade must explicitly reject unsupported options, and the final browser capability declaration must preserve these limits.

## Acceptance gates

The Rust unit tests cover policy parsing, certificate shape, exact integer handles, resource bounds, metadata accounting, terminal-event retention, late-event suppression and document isolation. They are semantic fixtures, not network proof.

`examples/ffi_gate.rs` uses the exported C functions to exercise the real local HTTP/3-only server: fresh binary datagram echo; a 20,000-byte reliable stream echoed through EOF using multiple 16 KiB reads; rejection of an oversized write; cancellation of an outstanding read; separate document owners with a fresh surviving-owner echo; wrong-document handle rejection; certificate-specific wrong-pin and native-root controls; and zero resources after cleanup. It records no raw nonce, certificate, URL query or private browser data. Its result still does not expose a browser constructor. Independent V8/native policy and actual browser-page checks are required next.
