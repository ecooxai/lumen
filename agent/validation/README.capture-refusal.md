# Linux native test capture refusal

The native WPT and reference-image launchers require an independently verified
capture-refusal library. This is test infrastructure for frozen browsers,
including revisions that predate the production Linux capture policy. A test
must not acquire a physical camera or microphone merely because it calls
`getUserMedia()`.

This environment makes physical capture unavailable. It does not manufacture a
successful stream, change WPT assertions, or establish positive media API
conformance. Chromium's reference runner uses its fake-device and fake-UI flags;
that difference must be stated in comparisons involving capture capability.

## Build and preflight

Run from the repository root, under the normal shared build/test CPU limit:

```sh
python3 tools/codex_compile.py python3 agent/validation/build_capture_guard.py \
  --output agent/validation/linux-wpt-20261028/capture-refusal/reproduced
```

The helper builds `capture_guard.so` and a dummy-only SDL oracle. Its manifest
records commands and source/binary hashes. The oracle checks camera refusal,
default/explicit/unknown recording-device refusal, recording-stream refusal,
and preserved dummy playback. It also requires rejection before the oracle main
function for a rejected acknowledgement, missing nonce, and a competing preload.
No physical device is opened by this preflight.

Use the exact absolute library path and its SHA-256 from that successful build:

```sh
export LUMEN_CAPTURE_GUARD_LIBRARY="$PWD/agent/validation/linux-wpt-20261028/capture-refusal/reproduced/capture_guard.so"
export LUMEN_CAPTURE_GUARD_SHA256='<SHA-256 from preflight.json>'
export LUMEN_CAPTURE_GUARD_AUDIT_DIR="$PWD/agent/validation/linux-wpt-20261028/my-run/guard"
```

A missing path or hash is an error. Do not silently fall back to an unguarded
launch. The following native entry points call `native_capture_guard.guarded_popen`:

- `agent/validation/paired_wpt.py`
- `agent/validation/reftest_pilot.py`
- `tests/wpt/lumen_wpt.py`, including every batch-worker respawn

The Chromium launch paths are unchanged. Use a fresh output and audit directory
for each attempt and retain startup failures as startup failures.

## Refusal and startup invariants

The C library intercepts `SDL_OpenCamera`, `SDL_OpenAudioDevice`, and
`SDL_OpenAudioDeviceStream`. Recording and unknown device identifiers are denied;
playback remains available. A constructor verifies that all three resolved
symbols belong to this library using its own DSO base, then emits a PID/nonce
handshake and waits for an acknowledgement before entering the program.

The Python launcher verifies a normal dynamically linked ELF executable, rejects
set-ID/file-capability executables and inherited preload/audit chains, verifies
the library hash, copies its bytes to a sealed memory file, and passes only that
reviewed preload. It requires the exact child PID and fresh 32-hex nonce before
sending the acknowledgement. The library descriptor, handshake pipe, and
acknowledgement pipe remain distinct. Linux libc `memfd_create` is used when the
Python build does not expose `os.memfd_create`.

Author navigation has an independent gate. The initial argument is a private
loopback HTTP endpoint that withholds its redirect until verification succeeds.
It serves no author HTML. The child receives a fresh XDG data/config profile,
`LUMEN_NO_SESSION=1`, and exactly one initial URL, so restored tabs cannot bypass
the gate. If the preload is absent or ineffective, no target redirect is released
and the child is terminated. Setup failures close owned descriptors, remove the
fresh profile, and stop the private server.

The fixed activity markers report only blocked categories, once per process:
`camera`, `recording-device-or-unknown`, and `recording-stream-or-unknown`. They do
not contain device identifiers or media data. Missing markers alone do not prove
that no capture call was attempted: retain complete logs and state the limits of
any rotated or partial logging.

## Provenance and measurement limits

Every run must retain the browser, library, wrapper, selection, and reporter
hashes; verified spawn/redirect counts; startup failures; raw URL outcomes;
resource logs; and interruption state. One-second cgroup CPU measurements cover
all concurrent task work; process-tree RSS/PSS observations have their separate
sampling limitations. A wrapper redirect adds a request and startup work, so do
not interpret this suite's wall time as normal browsing performance.

The 2026-10-28 accepted instrumentation was independently source-reviewed and
validated with six Python-wrapper controls, two injected pre-child setup failures
with no descriptor/profile leaks, the C dummy-only oracle, and actual frozen
original-browser HTTP and HTTPS targets. Both targets preserved document URL,
location, origin, encoded/repeated query, and history length one; HTTP remained
insecure and HTTPS secure. These smoke observations are infrastructure evidence,
not additional passing WPT assertions.

Session evidence is retained under
`agent/validation/linux-wpt-20261028/capture-refusal/`, including invalid earlier
attempts. The final reviewed library hash was
`45643caa22bb659bc7b431acfc7b8af04d68f971c3610f4d56dcb2847d808900` and wrapper hash
`d2099b548c524ae039533afe6c8f44fe4e1a4abe11c586bc08af98e3f91edc1d`.
The reproducible helper produced that same library.

The earlier unguarded broad comparison was stopped at 06:44:52 UTC when the
missing native media isolation was discovered. Its completed 8,000 URLs and
partial shard remain historical evidence with an explicit limitation: actual
past device access is unknown. They are not appended to guarded results. A fresh
guarded replay of the original first 2,000 URLs completed at 07:07:33 UTC with
36 verified spawns, 36 released bootstraps, and no startup failures. Future
before/after comparisons use that guarded configuration consistently.
