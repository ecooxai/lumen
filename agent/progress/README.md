# Lumen live progress

The task dashboard binds to `127.0.0.1:4187` and uses a dedicated HTTPS opentunnel route. It serves a small explicit set of resources, never the repository root. Safe aggregate status, host telemetry and public task history are public. Posting requires a separate session message key and an active session before its deadline. The message key cannot unlock private history. The exact reviewed engine-source snapshot is available at `/src.tar.gz`; raw results, full Git history and unreviewed artifacts are not served. An existing optional viewer key unlocks private task history; do not commit or print it. Runtime files and browser profiles are ignored by Git.

## Run and manage

- Install existing dependencies: `npm ci --prefix agent/progress --omit=dev`.
- Run the local server: `node agent/progress/server.mjs`.
- Read only new user messages: `node agent/progress/manage.mjs poll`.
- Mark those messages read: `node agent/progress/manage.mjs poll --mark`.
- Reply: `node agent/progress/manage.mjs reply MESSAGE_ID "Reply text"`.
- Verify configured HTTPS endpoints: `node agent/progress/manage.mjs health`.
- The health path is `/healthz`; use GET. HEAD is not implemented.
- Update `agent/progress/status.json`, preserving scope, version, binary, timestamp and uncertainty boundaries. Focused iteration quality and whole-goal readiness are separate; neither substitutes for actual test counts.

The browser receives CPU, RAM and disk metrics every two seconds and measures WebSocket ping/pong latency. Messages require the current session posting capability, are length/rate limited, and are treated as untrusted text. The key is stored only in `runtime/message-token-SESSION_ID` (0600), can be entered in the UI or supplied as the `post` URL fragment, and is not included in published status, source, logs or archives. Posting access expires at `session_deadline_utc` and when `session_active` becomes false. Read-only viewers cannot post; posting access cannot read private history. The server cannot execute them. An active agent polls and replies; the server alone does not wake an agent after the task ends. Public message history must not contain passwords or private data.

## Private source review staging

Historical automatic review rejected a complete source export containing the comparator. The reviewed engine-only snapshot is now explicitly allowlisted and served after exact size/SHA-256 verification. New source packages require concrete file/content review; do not re-export a previously denied payload through another route. Raw results, profiles, secrets and full Git history stay private.

After a reviewed accepted commit, `python3 tools/codex_compile.py python3
agent/progress/snapshot.py` stages a source archive and portable Git bundle only
under ignored `runtime/private-prepared/`. Its manifest records the exact commit,
size and SHA256. Custom output paths outside that private directory are rejected.
It does not publish or upload anything. The caller must retain the separate
artifact and destination review before any later publication implementation.
Runtime credentials, browser profiles, private messages and untracked data are
excluded; raw test-result archives and QA collections stay local.

## Test monitoring

`python3 agent/progress/monitor.py --root-pid PID --output agent/progress/runtime/RUN.csv --label RUN`

This follows the process tree, sampling CPU deltas and RSS every second and PSS every five seconds. SIGTERM flushes the summary. One core equals 100% CPU. RSS can count shared pages more than once, and interval snapshots can miss short-lived processes. Use `cgroup_monitor.py` alongside it for authoritative monotonic `cpu.stat` deltas under the shared task quota. New tests use project-backed temporary storage and core limit zero. All builds and tests use the existing capped `tools/codex_compile.py` wrapper; the experimental CPUWeight wrapper was retired.

## Static publication preparation

The existing static preparer is a local review aid. Current user instructions explicitly request source and cloud publication. Publish only the concretely reviewed package and destination; preserve any current automatic-review rejection. The live preview includes the reviewed engine-source snapshot, not raw results. Surge has no retained authenticated account; do not create
an account or transmit a new login as part of this task.

Official requirements checked on 2026-10-28:

- https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site
- https://docs.github.com/en/pages/getting-started-with-github-pages/creating-a-github-pages-site
- https://docs.github.com/en/rest/pages/pages

Surge had no existing authenticated account in the retained preflight, and no new account was created. The live HTTPS tunnel remains the current preview until the final reviewed Pages deployment is verified.

The static preparer distinguishes the accepted runtime checkpoint, the archive commit including reviewed handoff files, and the frozen binary hash/scope. It requires runtime ancestry and permits only progress/iteration-record/AGENTS/devsummary/task documentation changes afterward; all other source and build inputs must be identical. It records source-tree and CMake object identities. This does not claim a bit-for-bit reproducible rebuild.

The optional fixed `/showcase.mp4` route stays404 until the explicitly requested actual-screen video has a reviewed publication manifest. Both live and static paths reject symlinks and nonregular files, read the same opened file once under a 64 MiB bound, verify exact size and SHA256, then serve or copy those verified bytes. Static preparation also verifies the final binary hash, duration under 60 seconds and 4:3 dimensions. Fourteen bounded positive/negative controls cover exact bytes, missing/unreviewed manifests, wrong hashes, symlinks and nonregular files. Actual video content review is separate. This route does not expose arbitrary runtime files or the denied raw-results archive.

The GitHub Pages output is an explicit saved snapshot with no same-origin telemetry or message backend. Its live link points to the tunnel for those functions while the server remains available. Public task messages persist between agent sessions; replies require an active agent session. Keep the ended-session status accurate and do not promise unattended replies.
