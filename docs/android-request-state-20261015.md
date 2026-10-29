# Android Request state compatibility, 2026-10-15

Vulkan remains first. OpenGL ES/software acceptance and the full daily-browser
objective remain unfinished.

## Repair and reproduction

Request construction previously copied script-visible properties such as `url`
and `method`. Own property getters could therefore turn a local data Request
into an unrelated network POST. This happened in YouTube's public page script:
a local JSON Request with overridden public getters produced an empty player
API POST. It explains that particular unexpected HTTP 400, but does not explain
all playback failures.

Request metadata now lives in a WeakMap. Construction, cloning and fetch use
that state; public metadata properties are readonly prototype getters. Copies
retain request options, with explicit initialization overrides taking precedence.
Known HTTP methods are normalized, custom method case is retained, invalid or
forbidden methods and GET/HEAD bodies are rejected. This follows the internal
request model in the [Fetch specification](https://fetch.spec.whatwg.org/#request-class).
It is a general browser compatibility repair, with no site exception or change
to server access checks. Full Request body-stream ownership/clone semantics
remain outside this repair and are not claimed as conformant.

Run `node --test tests/test_request_state.js tests/test_fetch_preflight.js tests/test_document_base_uri.js`:
12 tests pass. Running the four new tests against the previous source fails.
The local phone fixture is `/request-state` on `tests/fetch_preflight_server.py`.
The baseline passes 1/5 checks and sends an unintended empty POST to the local
`/unexpected-request` endpoint. The corrected clean APK passes 5/5 and sends no
unintended request. All six streaming checks also pass; first data after a
preflight arrives in 61.72 ms, or 57.76 ms with credentials, while the server
completes the response after three seconds.

## Device evidence and limits

MI 8, Android 15, Vulkan. Continuous 1 Hz process/thread sampling is saved in
`build/codex-vulkan-playback-20261015/samples.jsonl`. Local Request fixture idle
CPU over 30 samples averages 0.166%, peaks at 1.01%, with 225.43 MiB process RSS.
These are local-fixture results, not whole-site improvements.

The clean APK still reaches YouTube's "Something went wrong" screen. A tap
while video was visible did not expose controls. An error-page 30-sample window
averaged 7.96% CPU with a 70.71% peak and 477.82 MiB RSS. This fails the idle target.
Earlier native/V8 profiles show site JSON/object work and GC, plus media polling
and observer work. The Request repair does not establish a fix for that CPU cost.

Clean artifact: `build/codex-vulkan-playback-20261015/lumen-vulkan-request-debug.apk`.
SHA256: `18260ca25d6cd7645b74c1cd55ab3e9c42caf4a5dbcef16f7b6b4f656226e395`.
Native dependency closure (arm64-v8a, API 29) and APK v2 signature checks passed.
Built through the capped compilation wrapper using the verified V8 prebuilt.
No account credentials were entered. Controls diagnostics are subsequent,
temporary work and are not included in this clean artifact.
