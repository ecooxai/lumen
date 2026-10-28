# Lumen native HTTP/3 transport experiment

This is a **standalone macOS transport gate**, not a browser implementation. It does not expose `WebTransport`, claim Lumen support, modify a system trust store, or use a browser profile. The crate is pinned to `wtransport = 0.7.2`; the committed Cargo lockfile pins its transitive dependencies and registry checksums.

## What the executable measures

The fixed endpoint is the project's HTTP/3-only aioquic server at `https://127.0.0.1:4433/echo`. The client supplies the lab origin `http://127.0.0.1:8765` explicitly. These checks do not establish that browser origin, CSP, CORS, permission, stream, or navigation policies are implemented.

1. Connect using the public certificate hash from the lab's manifest. The upstream verifier checks the certificate validity, ECDSA P-256 algorithm, and maximum fourteen-day validity period. No insecure verifier is enabled.
2. Require actual negotiated ALPN `h3`. Send a fresh OS-random datagram nonce and require an exact echo. Independently send a different fresh nonce on a reliable bidirectional stream, require an exact echo and stream EOF.
3. Keep the connection idle for three seconds with keep-alive disabled, while the observer samples the owned process tree. Record actual QUIC UDP byte counters, not application payload counts relabelled as wire traffic.
4. Close the session and endpoint. Require `wait_idle()` to complete within three seconds and the endpoint to report zero remaining connections.
5. Require a deliberately wrong pin to fail with a certificate-specific error. A timeout or network outage cannot pass this control.
6. Connect with normal platform roots and require rejection of the untrusted local self-signed certificate. A successful public-CA connection is a separate, untested case.
7. Repeat the positive pinned exchange after both negative controls, proving that those rejections were not merely a dead test server.

Each positive case has a twelve-second deadline; each negative connect has a five-second deadline. The Tokio runtime has two worker threads. The process exits normally after the report so the runtime and sockets are dropped. Nonce payloads, private keys and full error chains are not recorded. Reports contain stage/category codes, booleans, durations and byte counts only.

## Reproduce

Start the separately documented `tools/web-capability-lab/webtransport_server.py` inside its project virtual environment first. That server creates the short-lived public fingerprint manifest; its private key stays in ignored output and is never supplied to this client.

From this directory:

```sh
python3 run_measured.py --label GPT6-AstraPro-build --timeout 600 -- sh validate_and_build.sh
python3 run_measured.py --label GPT6-AstraPro-http3-gate --timeout 70 --max-rss-mib 512 -- target/debug/lumen-webtransport-native-spike --certificate-manifest ../web-capability-lab/output/wt-certificate.json
```

The build script uses `cargo test --locked --jobs 2` followed by `cargo build --locked --jobs 2`. The build and network gate remain separate acceptance steps. The executable is a debug build; its CPU/RAM figures describe this particular experiment, not an optimized production browser.

`output/GPT6-AstraPro-subagents-WebTransport-gate.json` holds the protocol results. The observer records one-second process-tree CPU and resident memory samples in a separate measurement JSON. CPU is the sum of operating-system `ps` values; it may exceed 100% across cores. Peak RSS is a sampled sum, not an instantaneous allocation bound or unique physical memory measurement. The measurement runner terminates only its owned process group on timeout, observer failure, or the configured memory ceiling.

For independent evidence, retain the server's count-only `webtransport-events.jsonl` delta, the executable SHA-256, source revision, and Cargo.lock SHA-256. A successful gate is only evidence that this native dependency can exchange real HTTP/3 traffic on this Mac. Browser integration still requires an owner-thread bridge, per-document cancellation, finite queues and backpressure, URL/security policies, browser streams and lifecycle semantics, and separate Apple target builds and tests.

## Sources

- Pinned crate source: https://docs.rs/crate/wtransport/0.7.2/source/
- Certificate configuration: https://docs.rs/wtransport/0.7.2/wtransport/config/struct.ClientConfigBuilder.html
- WebTransport specification: https://www.w3.org/TR/2026/CR-webtransport-20260730/
