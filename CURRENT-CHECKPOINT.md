# Lumen Linux checkpoint 1808

**DEVELOPMENT DEVICE CHECKPOINT — preregistered reliability replay and two namespace checks PASS. Earlier intermittent namespace timeout remains documented. Not a production or whole-WPT certification.**

The one preregistered unchanged reliability replay completed 759/1248 selected test cases/URLs (60.82%), with 22826/26245 assertions passing and all 22826 prior passes retained. The first full run remains HOLD at 758/1248 because its namespace harness timed out despite 51/51 assertions passing. Its raw result is preserved; no source fix or resolved cause is claimed.

The starting accepted sample was 1705:714/1248 (57.21%). The 1248 URLs come from 1247 source files across 10 DOM/CSS families; the pre-existing selection excludes 44 long-timeout cases and 1 HTTPS-only case. This is not the whole WPT platform, and Chromium parity or 95/100 product quality is not claimed.

Read [CURRENT-CHECKPOINT.md](CURRENT-CHECKPOINT.md) and [EXPORT-QUALIFICATION.json](EXPORT-QUALIFICATION.json) for exact scope and identities. Use [OFFLINE-RECOVERY.md](OFFLINE-RECOVERY.md) for the approved public Git history.

Prepared with GPT-6 Astra Pro, built-in agents and RubyWebTerm on 2026-11-03.

Exact tested engine HEAD: `fcf2d2d83686a554f492fbaf992173b12d924b64`. Linux binary SHA-256: `79af7023284551d597ce61ff4a5e3954a7edd2a43b4b8ee2af5d63a1658ff46d`. Public Git ancestry is separately curated; private working history, credentials, profiles, inbox records and guarded validation machinery are excluded.

The final Linux display :0 restart and Android install-r/foreground/render/liveness checks are recorded in EXPORT-QUALIFICATION.json. User app data was retained. Android smoke coverage is separate from Linux WPT measurement.

External build dependencies are not bundled. Offline Git recovery is not an independent browser rebuild.
