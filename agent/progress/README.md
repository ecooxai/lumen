# Lumen live progress

Small task dashboard at localhost:4187, exposed through a dedicated HTTPS opentunnel route. It never serves the repository root. A random task-specific viewer key gates all metrics, messages, and source downloads; the static page itself contains no key. Runtime files are ignored by Git.

## Run and manage

- Install: `npm ci --prefix agent/progress --omit=dev`.
- Run: `node agent/progress/server.mjs` (binds only 127.0.0.1).
- Read pending user messages: `node agent/progress/manage.mjs poll --mark`.
- Reply: `node agent/progress/manage.mjs reply MESSAGE_ID "Reply text"`.
- Verify HTTPS health: `node agent/progress/manage.mjs health`.
- View authorized URL: `node agent/progress/manage.mjs url` (contains private task key; do not commit).
- Update evidence in `agent/progress/status.json`; preserve historical/current labels and disclose uncovered tests.
- Rebuild immutable audited snapshot after iterations: `python3 agent/progress/archive-source.py`. Runtime audit JSON records Git HEAD, dirty status, SHA-256, and exclusions. `/src.tar.gz` is an authenticated working-tree source snapshot, not a release or Git-history archive.

## Test monitoring

`python3 agent/progress/monitor.py --root-pid PID --comm lumen-wpt --output agent/progress/runtime/RUN.csv --label RUN`

Samples CPU deltas and RSS at one-second intervals, PSS every five seconds, follows descendants, stops when the root exits or at five hours. Omit `--comm` for the entire tree. SIGTERM flushes summary JSON. One core equals100% CPU. RSS sums can double-count shared memory; PSS is labeled separately. Short processes between samples can be missed.

## Static Surge deployment

`npx surge whoami` returned **Not Authenticated** on2026-10-27. Publishing requires an existing authorized account/token. No account was created. Once authenticated, copy the audited archive to `agent/progress/site/src.tar.gz`, generate public `config.js` with `window.LUMEN_API` set to the tunnel origin (never include the viewer key), deploy `agent/progress/site`, and add the exact resulting Surge origin to runtime/endpoints.json for CORS. The live HTTPS tunnel remains available independently.

## Verified2026-10-27

HTTPS health200; anonymous API401; authorized API200; WSS ping/pong; real CPU/RAM and six block-device filesystems. Desktop1365px and mobile390px layouts viewed; no mobile horizontal overflow. A clearly marked synthetic message and agent reply completed the round trip and were removed afterward. Browser validation used an isolated Chrome profile and one-second process monitoring. No screenshot is included in durable checkpoint files.

Incoming text is displayed and stored only; it cannot execute commands. Operators must still treat all messages as untrusted task input and respect authorization boundaries. The main agent polls during active work; the server alone does not wake an agent after the task ends.
