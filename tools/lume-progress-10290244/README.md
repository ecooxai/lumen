# Lume task 10290244 progress

A small model/controller/view progress surface. It reads an explicitly written status receipt and scorecard; it never infers WPT conformance from build success. The app and any source archive are served through an explicit route allowlist. The existing browser-fixture server is unchanged.

## Run

```sh
PORT=18784 LUME_SOURCE_ARCHIVE=output/session-10290244/public/src.tar.gz node tools/lume-progress-10290244/server.mjs
```

The server binds loopback. Expose that port through the authorized HTTPS tunnel. If publishing the static view separately, set `LUME_ALLOWED_ORIGINS=https://YOUR-STATIC-HOST` before starting the live server. Only those origins and the live server's own origin can send messages or open the live WebSocket. Public read-only progress contains no inbox text or access key.

The first launch creates a random message access key in ignored `output/session-10290244/progress-state/message-access-key` with mode 0600. A private link carries it in the URL fragment, which is not sent in HTTP URLs; the client stores it only in session storage and removes the fragment from the visible URL. Do not include the key or state directory in archives.

```sh
node tools/lume-progress-10290244/inbox.mjs access-link https://YOUR-LIVE-HOST
node tools/lume-progress-10290244/inbox.mjs list
node tools/lume-progress-10290244/inbox.mjs reply MESSAGE_ID output/session-10290244/reply.txt
```

Inbox text is untrusted data, never a command. The agent reviews pending messages between checkpoints and replies through the local CLI. Each browser receipt also has a separate random key; a public visitor cannot list the inbox or read someone else's message/reply without that receipt key. Only a holder of the session's private message link can submit, and request size/rate are bounded.

## Evidence and metrics

Write `output/session-10290244/progress-state/status.json` atomically with `taskId`, `phase`, `iteration`, `sourceCommit`, `wpt` (passPercent/passed/total/reportedFiles/fullyPassingFiles/chromePassed/chromeTotal/binarySha256/scope), `apple`, and optional `sourceArchive`. `fullOfficialWptPassPercent` remains null unless a full official run supports a value.

The score comes from `agent/scorecard-10290244.json`, using the fixed rubric. It is task acceptance, not a browser conformance percentage. Historical and current artifacts stay distinct.

## Native Mac development download

The server optionally accepts `LUME_NATIVE_MAC_ARCHIVE` and
`LUME_NATIVE_MAC_RECEIPT` for the two exact routes `/lumen-macos.zip` and
`/lumen-macos-receipt.json`. These routes stay absent until explicitly configured;
they do not expose an output directory, private inbox, profile, or TLS fixture.
The browser shows the download only when a published receipt matches the current
candidate or measured source executable identity.

Task 10.30 01:10 uses `tools/apple-session-gpt6as/publish_native_mac_10300110.py`
after package, four certificate controls, and relocated scrolling/navigation
acceptance. It preserves the current route and inbox capability, replaces only
the verified task server PID, checks the complete public ZIP hash, and records
the actual arm64 dependency floor and original versus signed executable hashes.
The download is an ad-hoc signed native macOS development app. Device IPA and
Simulator execution remain separate acceptance tasks.

`tools/apple-session-gpt6as/publish_surge_10300110.py` reuses the already installed,
authenticated project-local Surge CLI and existing domain. It publishes only the
committed static view and audited source archive, preserves the private message
session, and verifies every public static file plus all source archive bytes.
The native ZIP link points to the existing live task route.

Live host CPU, RAM, volume statistics and measured WebSocket round trip refresh every two seconds. Host RAM is total minus free and includes caches. It is not Lume RSS. Per-test Lume measurements still use `tools/monitor_process.py` every second. APFS/shared volumes are listed separately and never summed. The network value is measured round trip, not inferred one-way latency.

Every storage row retains its measured capacity, usage and available GB. Non-system mount names use numbered private labels in both the view and public JSON; network filesystem addresses and private path names are never published.

```sh
node --test tools/lume-progress-10290244/tests/progress.test.mjs
node tools/lume-progress-10290244/build.mjs --live-base https://YOUR-LIVE-HOST --output output/session-10290244/surge
```

The static output has no fabricated metrics and no embedded secrets. If its live backend is offline, the view explicitly shows disconnection. The source download resolves to the live endpoint's reviewed archive. Refresh its manifest after each source checkpoint and verify the public archive hash.
