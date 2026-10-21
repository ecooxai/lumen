# Selected-site Chrome session migration for Lumen

This development helper supports the user's authorized same-Mac browser comparison. It transfers only live, unpartitioned cookies scoped to Google, YouTube, ChatGPT/OpenAI, and X/Twitter into a **new private Lumen test profile**. The original Chrome and Lumen profiles are read-only sources and are not replaced. Cookie presence and successful transfer do not prove that a provider accepts the resulting browser session.

The helper defaults to metadata-only mode and does not access Keychain in that mode. `--apply` requests the existing **Chrome Safe Storage** entry through normal macOS Keychain access once. It does not change Keychain access controls, unlock a keychain, retrieve passwords from Login Data, copy device-bound private keys, spoof a user agent, or bypass provider security checks. A denied or timed-out Keychain request remains pending. A user may need to complete ordinary login in Lumen.

## Run on the signed-in Mac

Use the isolated Python environment installed for the HTTP/3 lab; it already includes `cryptography`:

```sh
tools/web-capability-lab/.venv/bin/python tools/session-migration/chrome_to_lumen.py
tools/web-capability-lab/.venv/bin/python -m unittest discover -s tools/session-migration/tests
tools/web-capability-lab/.venv/bin/python tools/session-migration/chrome_to_lumen.py --apply
```

The default source is the Chrome `Default` profile. Select an existing Chrome `Profile N` explicitly with `--chrome-profile 'Profile 1'`. The helper never scans other profile contents or makes a full copy of a Chrome database.

The destination is a new directory beneath `~/Library/Application Support/lumen/Lumen-session-tests/`. The sanitized result reports its path, counts, and skip reasons, never cookie values or the encryption key. An existing destination is refused. Launch Lumen with the reported directory in `LUMEN_PROFILE_DIR` and a restrictive `umask 077`; use the ordinary browser executable and an HTTPS site URL. The private cookie file is part of that local browser profile, not a report or repository artifact. Do not attach the profile to bug reports, commit it, serve it with the lab HTTP server, or put it in a comparison video. Remove the dedicated test profile after the authorized comparison when it is no longer needed.

## Format and restrictions

Supported source databases are versions 23 and 24 with the observed macOS `v10` encryption format. The helper uses AES-CBC with the normal Chrome key derivation and verifies the SHA-256 domain prefix required by database version 24. Unknown encryption versions and schema versions fail closed. The destination preserves host-only/domain scope, path, Secure, HttpOnly, expiry, and SameSite. Prefix restrictions are validated. Partitioned cookies, nonstandard source bindings, expired rows, invalid characters, and conflicting duplicate scopes are skipped instead of widening their reach. Device-bound sessions may still require ordinary authentication or revalidation, because their private keys are not migrated.

Tests use only synthetic cookies and a temporary synthetic SQLite database. They verify encryption and domain binding, source scope, partition/expiry filtering, prefix rules, flag preservation, private file permissions, refusal to overwrite, Keychain-denial handling, and duplicate-scope handling. No unit test opens a real browser profile or Keychain.

## Source references

- [Chromium macOS encryption implementation](https://chromium.googlesource.com/chromium/src/+/refs/tags/143.0.7497.1/components/os_crypt/sync/os_crypt_mac.mm)
- [Chromium Keychain service and account naming](https://chromium.googlesource.com/chromium/src.git/+/refs/tags/128.0.6536.1/components/os_crypt/sync/keychain_password_mac.mm)
- [Chromium cookie database version and domain binding](https://chromium.googlesource.com/chromium/src/+/main/net/extras/sqlite/sqlite_persistent_cookie_store.cc)
- [Chromium persisted SameSite enum](https://chromium.googlesource.com/chromium/src/+/HEAD/net/cookies/cookie_constants.h)
- Lumen destination format: `src/net/cookies.c`, `cookies_load` and `cookies_save`; profile override: `src/app/main.c`, `profile_init`.

The source references explain storage compatibility. They do not imply provider approval of session reuse or guarantee authenticated page rendering in Lumen.

## Alternative using Chrome's normal app access

If the generic `security` CLI needs additional Keychain consent, the installed signed Chrome app may already have its own normal access. `chrome_context_transfer.mjs` supports that separate route without changing or bypassing OS access controls:

```sh
tools/web-capability-lab/.venv/bin/python -m unittest discover -s tools/session-migration/tests
node tools/session-migration/tests/chrome_bridge.test.mjs
node tools/session-migration/chrome_context_transfer.mjs --fixture
node tools/session-migration/chrome_context_transfer.mjs --apply
```

The fixture creates only synthetic cookies in a new temporary Chrome profile, verifies host/domain scope and security attributes (including a non-Secure host-only cookie whose setting-origin sourceScheme is HTTPS), then closes and reopens Chrome to verify both session and persistent cookies. It never reads a user profile. The `--apply` path must follow a successful fixture run on the same installed Chrome app.

The actual transfer starts a read-only SQLite transaction, copies only supported live and unpartitioned selected-site rows with their original encrypted values and cookie-table metadata into a new private Chrome profile, and closes the source transaction. It copies no history, passwords, extensions, browsing data for other sites, device-bound session keys, or Local State. The isolated Chrome process loads those rows through its own normal storage implementation. A loopback CDP connection retrieves the cookies into memory; a validated conversion writes the matching set to a new Lumen test profile. The real transfer never clears or reinserts cookies through CDP: its original scoped rows remain intact. This avoids setter normalization of source metadata, Secure flags, and expiry. An exact in-memory comparison across a clean reopen still gates the write. Browser logs are discarded, and helper output contains only counts, status codes, and the private profile paths.

Both session and persistent cookies retain their original semantics. The dedicated Chrome profile uses the ordinary “continue where you left off” session setting and `--restore-last-session`; expiry times are not extended. After a clean close and reopen, every supported cookie value and relevant security attribute must still match in memory before the Lumen profile is written. An incomplete load, Keychain prompt, protocol failure, or changed attribute leaves the transfer unverified. The existing Chrome process and profile remain untouched. No provider site is navigated during initialization; authentication still requires the separate private status-only browser checks.

References: [Chrome session-cookie restoration policy](https://chromium.googlesource.com/chromium/src/+/HEAD/chrome/browser/profiles/profile_impl.cc), [CDP Storage cookie APIs](https://chromedevtools.github.io/devtools-protocol/tot/Storage/).

The isolated signed-Chrome route currently uses a new `GPT6-AstraPro-Chrome-scoped-preserved-20261021` profile. The previous `GPT6-AstraPro-Chrome-scoped-20261021` clone is not reused because its failed equality check exposed CDP setter normalization; it is not an authenticated comparison profile. No Lumen profile was written from that failed attempt.

The CDP setter behavior is visible in [Chromium MakeCookieFromProtocolValues](https://chromium.googlesource.com/chromium/src/+/HEAD/content/browser/devtools/protocol/network_handler.cc): an HTTPS setter URL implies Secure. The fixture keeps the Secure flag and setting-origin sourceScheme separate.
