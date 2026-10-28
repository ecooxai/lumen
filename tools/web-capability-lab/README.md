# Lume browser capability lab

A self-contained engineering page that exercises real browser APIs and keeps an
exportable record of what happened. The lab never patches feature detection,
alters a browser user agent, invents an HTML5Test score, or treats a simulated
sign-in as an authenticated provider session.

## Run

The HTTP and WebSocket server needs Node.js 22 or newer and has **no runtime npm
dependencies**:

```sh
cd web-capability-lab
node server.mjs
```

Open `http://127.0.0.1:8765/` in Lume, Safari or Chrome. Loopback is a secure
context in supporting browsers. The server binds to loopback by default and
serves only an explicit list of public lab assets. Source code, certificates,
private keys and reports cannot be fetched through this server.

On an iOS simulator, test whether its loopback reaches the host service. A real
iPhone or iPad needs a reachable HTTPS endpoint with a certificate trusted by
that device; an HTTP LAN address does not establish a secure context. Optional
environment settings are `LUME_LAB_HOST`, `LUME_LAB_PORT`, `LUME_LAB_TLS_CERT` and
`LUME_LAB_TLS_KEY`. Use the TLS settings for your own trusted certificate. The
lab does not change OS certificate trust or disable certificate checks.

## Real HTTP/3 / QUIC endpoint

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-webtransport.txt
.venv/bin/python webtransport_server.py
```

Start the HTTP server in another terminal and reload the page. The optional
server listens on UDP `127.0.0.1:4433`, negotiates **HTTP/3 only**, accepts the
WebTransport extended CONNECT on `/echo`, and echoes datagrams and reliable
bidirectional streams. There is no HTTP/2/TCP fallback. Verified echoes therefore demonstrate actual
QUIC traffic for this endpoint. `output/webtransport-events.jsonl` records byte
counts and the `h3-quic` transport marker without message payloads.

The server generates a seven-day ECDSA certificate and supplies its SHA-256
fingerprint through `/api/config`. The frontend passes that fingerprint using
`serverCertificateHashes`; it does not globally ignore TLS errors. The private
key has mode `0600` and all generated output is ignored by Git. The fingerprint
file's presence means the endpoint is configured, not that it is running: a
stopped endpoint produces a failed connection test. For custom origins, set the
comma-separated `LUME_LAB_ORIGINS` allowlist before starting the HTTP/3 server.

## What each test proves

| Test | Required evidence | Limit |
|---|---|---|
| Camera | A live video track and playing local preview | Physical hardware and OS permission still require a real device test |
| Microphone | A live audio track; level meter when AudioContext is available | No audio is recorded or uploaded |
| Geolocation | Valid latitude/longitude with reported accuracy | Coordinates are omitted from exported evidence |
| WebAssembly | Compiled module executes `add(20, 22) === 42` | One functional module, not a performance benchmark |
| WebGL | Real triangle and a checked framebuffer center pixel | One frame, no animation loop |
| WebGPU | WGSL compilation, pipeline execution and checked texture bytes | Native presentation and offscreen readback are explicitly separate modes |
| Iframe | Loaded child, verified message source/origin and policy observations | Camera/mic/location inside the child require their own buttons |
| WebRTC | Exact local channel echo; recorded ICE/DTLS status when exposed | Does not prove external connectivity, actual protocol implementation, STUN or TURN |
| WebSocket | Exact text echoed by the local server | Does not prove every third-party WebSocket service |
| WebTransport / QUIC | Fresh exact datagram and reliable stream echoes, stream EOF, and resolved session closure against the HTTP/3-only server | Engine support and certificate-hash support are required |
| Google / Apple | Explicit UI simulation, or navigation to the real provider page | Neither action verifies an authenticated OAuth callback |

### WebGPU presentation modes

**Native WebGPU canvas** is the default. It renders to the actual canvas texture
with `COPY_SRC` enabled and reads that same texture back for byte verification.
That proves the submitted canvas texture contains the expected pixels; actual
window/compositor visibility requires the separately captured screen evidence.
A driver or canvas-presentation error remains a failure; there is no automatic fallback.

**Offscreen GPU + pixel readback** is an explicit user selection. It runs the
same WGSL graphics pipeline, copies the rendered texture back from the GPU, and
displays those actual pixels through a 2D canvas. Its exported evidence states
`nativeCanvasVerified: false`. This mode permits meaningful graphics-pipeline
testing in environments whose headless compositor cannot present a WebGPU
swapchain. It is not evidence that a native canvas is working.

### Identity tests

The consent simulation uses `demo@example.invalid`, contains no password fields
and contacts no identity provider. Completion remains `configuration`, with
`authenticated: false`. The adjacent real provider links open Google's or
Apple's account site. Production sign-in requires a registered client,
appropriate callbacks, CSRF/state and nonce checks, and an integration supported
by the provider. Lume's native authentication implementation must be assessed
separately. This lab neither reads nor transfers Chrome cookies.

## Lifecycle and performance

No probe runs on page load. Automatic mode runs only the seven probes that do
not request camera, microphone or location. Media capture stops after fifteen
seconds; child-frame capture stops after eight. Stop, a hidden page, and page
unload release media tracks, audio contexts, peer connections, sockets, devices,
textures, buffers and frame listeners. The stop action also cancels a pending
automatic sequence. Late-resolving media requests immediately stop their tracks
if the user has already cancelled the test.

Graphics render a single frame. The microphone meter samples at ten updates per
second while its short-lived capture is active. WebRTC, WebSocket and
WebTransport close immediately after the payload is verified. CPU/RAM figures
must be measured externally for the complete native browser process tree; the
number of registered resource scopes is a lifecycle indicator, not an OS memory
measurement.

## Automated verification

```sh
node --test tests/*.test.mjs
npm install
npx playwright install chromium
node tests/browser-smoke.cjs
node tests/media-fixture.cjs
```

Set `LUME_CHROME_EXECUTABLE` to an existing Chrome/Chromium binary when needed.
The browser smoke test starts its own temporary HTTP server on port 8765, so
stop a separately running lab HTTP server first. Start the optional HTTP/3
server beforehand to exercise QUIC; otherwise the honest expected state is
`configuration`.

`tests/media-fixture.cjs` uses **synthetic Chrome camera/microphone fixtures** to
verify acquisition, track counting, restart, stop and hidden-page cleanup. Its
output explicitly says that physical devices were not tested.

For a Linux VM without a hardware GPU, an optional regression command is:

```sh
LUME_ENABLE_SOFTWARE_WEBGPU=1 LUME_CHROME_EXECUTABLE=/path/to/chrome node tests/browser-smoke.cjs
```

This sets explicit SwiftShader/Vulkan test flags and selects the offscreen
readback mode. These results must not be presented as Apple hardware
compatibility or native performance numbers. No such flags are applied to the
user's ordinary browser.

### Automation hooks

- `await window.LumeLab.runAutomatic()` returns measured results.
- `await window.LumeLab.run('webgpu')` runs one named probe.
- `window.LumeLab.snapshot()` returns environment, status, evidence, resource
  counts and the event log.
- `window.LumeLab.stopAll()` releases active resources and cancels the sequence.
- Stable selectors include `[data-test="run-automatic"]`,
  `[data-test="stop-all"]`, `[data-test="run-webgpu"]` and
  `[data-capability="webgpu"]`.

The UI adapts to desktop, iPad and narrow phone widths. Export writes a local
JSON file and never sends the report to a remote endpoint.

## Official implementation references

- [aioquic HTTP/3 API](https://aioquic.readthedocs.io/en/latest/h3.html)
- [Chrome WebTransport example server](https://github.com/GoogleChrome/samples/blob/gh-pages/webtransport/webtransport_server.py)
- [WebGPU specification](https://www.w3.org/TR/webgpu/)
- [Chromium headless GPU documentation](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/docs/gpu/using-gpu-hardware-in-headless-chrome.md)

Created by GPT-6 Astra Pro with native subagents and web tooling. Evidence files
record the actual runtime and scope of each test; a successful build is not a
claim that all native-device or website requirements have been satisfied.

For an engine with compatibility shims, even reported ICE/DTLS statistics may be synthetic. This local API check cannot establish real UDP/DTLS/SCTP implementation. Use an independent Chrome peer interoperability test before claiming standards transport support.

## Independent browser WebRTC proof

Start the HTTP server. Open the following URLs in **different browser processes** with one unique room identifier (8–80 letters, digits, hyphens or underscores):

- Chrome offer: `http://127.0.0.1:8765/rtc-peer.html?room=lume-unique-test01&role=offer&autostart=1`
- Lume answer: `http://127.0.0.1:8765/rtc-peer.html?room=lume-unique-test01&role=answer&autostart=1`

Each peer sends a random nonce through RTCDataChannel; the other peer must echo it. Both peers close on success, failure, stop, hidden page or a 20-second deadline. Inspect `window.LumeRTCPeer.snapshot()` or the `LUME_RTC_INTEROP ` console record. Reports include ICE/DTLS state and byte counts, but no SDP, ICE candidate addresses or payload. A failed handshake or missing echo stays failed; it is not replaced by in-process echo.

`/api/rtc-signal` is a same-origin, loopback-only temporary broker. Signaling data stays in bounded process memory, expires after two minutes, and is never written to logs or files. Rooms are limited to 32, messages to eight per direction, and SDP to 32 KiB. It has no background polling thread; peers poll every 350 ms for a bounded test. A real-device/LAN deployment requires a separately designed authorized signaling endpoint.

`node tests/rtc-interop.cjs` validates the helper with two independent headless Chromium processes on an automatically assigned temporary loopback HTTP port. That reference test proves the helper works; it does not count as Lume interoperability. Use a fresh unique room for each run.

### Native engine declarations

When Lumen exposes `navigator.lumenCapabilities`, the exported environment preserves that capability descriptor. A declared `webrtcNetworkTransport: false` prevents both RTC probes from treating the experimental in-process shim as network support, even when the shim is explicitly enabled. A declared `webTransport: false` is unsupported. Location results include the declared provider (currently IP approximation in Lumen), and do not claim physical GPS verification. `node tests/engine-declarations.cjs` exercises these guards in a real Chromium document with an explicit synthetic capability descriptor.

### Embedded permission fixture

The synthetic Chromium media regression also clicks camera, microphone, and location controls inside the real same-origin iframe. It checks track replacement, preserves track references while the iframe is removed, and verifies every retained track is ended after parent Stop. Parent teardown calls the child's explicit stop hook before removing the frame. Location uses injected fixture coordinates, and exported evidence omits the coordinates. This verifies lab wiring and resource behavior; it does not prove physical camera, microphone, GPS, or native Lumen child permissions.

## Measured lab coverage

The bottom-right iteration badge reports a measured `n/100` coverage value: passed probes divided by all twelve requested probes, rounded to the nearest integer. Blocked, unsupported, setup-required, cancelled and untested probes remain in the denominator. Counts for those states remain visible. Simulated identity consent does not pass real authentication. The badge is not an overall browser-quality score or an HTML5Test result; independent engine acceptance, provider authentication, device coverage and resource budgets remain separate.

The native libdatachannel backend reports measured application payload bytes and its actual selected ICE candidates. It does not expose ICE/DTLS/UDP byte totals through its C API; those fields stay null or absent. The local probe accepts this specifically declared backend only with a selected pair, native connected/completed state, and nonzero payload bytes in both directions. This remains same-browser evidence. The independent browser helper still requires a nonce from the other process; missing transport totals are not converted into fabricated zeros or inferred DTLS measurements. Native media RTP remains unsupported.

The peer page records only its own fresh nonce echo (`independentPeerEcho`) and released resources. It cannot inspect OS process identity, so `processSeparationVerified` and `crossBrowserInteroperability` remain false in the page report. The external runner records separately launched PIDs and browser binary fingerprints and performs the aggregate assertion. Both peers now exchange fresh nonces and acknowledge completion through the bounded signaling broker before teardown. The two-Chromium reference test proves independent-process transport only; Chrome-to-Lumen evidence is recorded by the separate engine verification runner.
