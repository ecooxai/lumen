# Lumen live progress

The task dashboard binds to `127.0.0.1:4187` and uses a dedicated HTTPS opentunnel route. It serves a small explicit set of resources, never the repository root. Safe aggregate status, host telemetry, the public task message board and the audited accepted-source archive are public. An existing optional viewer key unlocks private task history; do not commit or print it. Runtime files and browser profiles are ignored by Git.

## Run and manage

- Install existing dependencies: `npm ci --prefix agent/progress --omit=dev`.
- Run the local server: `node agent/progress/server.mjs`.
- Read only new user messages: `node agent/progress/manage.mjs poll`.
- Mark those messages read: `node agent/progress/manage.mjs poll --mark`.
- Reply: `node agent/progress/manage.mjs reply MESSAGE_ID "Reply text"`.
- Verify configured HTTPS endpoints: `node agent/progress/manage.mjs health`.
- The health path is `/healthz`; use GET. HEAD is not implemented.
- Update `agent/progress/status.json`, preserving scope, version, binary, timestamp and uncertainty boundaries. Focused iteration quality and whole-goal readiness are separate; neither substitutes for actual test counts.

The browser receives CPU, RAM and disk metrics every two seconds and measures WebSocket ping/pong latency. Public messages are length/rate limited and treated as untrusted text. The server cannot execute them. An active agent polls and replies; the server alone does not wake an agent after the task ends. Public message history must not contain passwords or private data.

## Accepted source snapshot

Run `python3 tools/codex_compile.py python3 agent/progress/snapshot.py` only after a reviewed accepted commit. It exports Git HEAD, excluding uncommitted experimental changes, and adds an audited portable Git bundle. It excludes runtime credentials, browser profiles, private messages, untracked data, Git hooks and repository configuration. The fixed `/src.tar.gz` endpoint serves only that artifact. Its commit, byte count and SHA-256 are recorded in `runtime/src.tar.manifest.json`.

Raw test-result archives and QA screenshot collections are not served by this endpoint or included in the static publication. Retain local evidence separately from reviewed source and aggregate status.

## Test monitoring

`python3 agent/progress/monitor.py --root-pid PID --output agent/progress/runtime/RUN.csv --label RUN`

This follows the process tree, sampling CPU deltas and RSS every second and PSS every five seconds. SIGTERM flushes the summary. One core equals 100% CPU. RSS can count shared pages more than once, and interval snapshots can miss short-lived processes. Use `cgroup_monitor.py` alongside it for authoritative monotonic `cpu.stat` deltas under the shared task quota. New tests use project-backed temporary storage and core limit zero. All builds and tests use the existing capped `tools/codex_compile.py` wrapper; the experimental CPUWeight wrapper was retired.

## Static publication preparation

`python3 agent/progress/prepare_pages.py` prepares a dedicated ignored runtime directory containing the saved public aggregate status, a static HTML fallback, `.nojekyll`, and the reviewed source archive. It verifies the archive hash and accepted commit. The fallback links to the live tunnel for telemetry and messages and remains readable when the tunnel is unavailable. This command does not push, create a remote branch or enable Pages.

At the task's final publication stage, publish the reviewed source branch and the separate model/agent-named static branch as distinct operations. Configure GitHub Pages to use the static branch root only after that branch is present, then verify the build and public GET response. Keep existing repository branches and documentation intact. Use existing authenticated tooling; do not initiate a new login or account creation flow.

Official requirements checked on 2026-10-28:

- https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site
- https://docs.github.com/en/pages/getting-started-with-github-pages/creating-a-github-pages-site
- https://docs.github.com/en/rest/pages/pages

Surge had no existing authenticated account in the retained preflight, and no new account was created. The live HTTPS tunnel remains the current preview until the final reviewed Pages deployment is verified.

The static preparer distinguishes the accepted runtime checkpoint, the archive commit including reviewed handoff files, and the frozen binary hash/scope. It requires runtime ancestry and permits only progress/iteration-record/AGENTS/devsummary/task documentation changes afterward; all other source and build inputs must be identical. It records source-tree and CMake object identities. This does not claim a bit-for-bit reproducible rebuild.

The optional fixed `/showcase.mp4` route stays404 until the explicitly requested actual-screen video has a reviewed publication manifest. Both live and static paths reject symlinks and nonregular files, read the same opened file once under a 64 MiB bound, verify exact size and SHA256, then serve or copy those verified bytes. Static preparation also verifies the final binary hash, duration under 60 seconds and 4:3 dimensions. Fourteen bounded positive/negative controls cover exact bytes, missing/unreviewed manifests, wrong hashes, symlinks and nonregular files. Actual video content review is separate. This route does not expose arbitrary runtime files or the denied raw-results archive.

The GitHub Pages output is an explicit saved snapshot with no same-origin telemetry or message backend. Its live link points to the tunnel for those functions while the server remains available. Public task messages persist between agent sessions; replies require an active agent session. Keep the ended-session status accurate and do not promise unattended replies.
