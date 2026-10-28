# Lume Apple WPT 10290244 — active task

Session start: 2026-10-28 18:47:35 UTC / 2026-10-29 02:47:35 Asia/Shanghai.
Deadline: 2026-10-28 21:47:35 UTC / 2026-10-29 05:47:35 Asia/Shanghai.
Goal: 98/100 overall; stop after goal or authorized three-hour budget, maximum 1000 iterations.

## Source and team

Native source is `/Users/devin/repos/lumen`, the canonical target of requested `~/project/lumen`.
Current branch `gpt6-astra-pro_release-integrator_lume-apple-wpt-10290244`; inherited source checkpoint `a8bd64552f9379cca33cd736a187924ec18eb356`.
Only release_integrator stages/commits. Engine/tests belong to wpt_engine; Apple build/runtime/monitoring belongs to apple_runtime; root brokers remote I/O and reviews.
The three worker agents inherit the main model and reasoning level. Child MCP calls hung; root calls are functioning. Do not overwrite the native source with older public/companion checkouts.
The complete current user request is in `agent/task.md`. Prior handoff and instructions are archived with timestamp `20261029-024735+0800`.

## Intake findings and pending evidence

- Source inherited +19 HTML tag interface registrations in `src/js/prelude/04-tags.js`; checkpoint preserves rather than reclassifies those changes as new work.
- Existing untracked clone tests/tooling/evidence remain in place; review and add only meaningful source files when verified.
- Native host is Apple arm64, macOS 26.6.2. Default developer tools are Command Line Tools; Xcode/simulator availability needs explicit-path diagnosis. No valid code-signing identity appeared in initial inventory.
- Existing matching prebuilt V8/dependencies should be reused before considering rebuilds.
- Latest raw focused WPT report candidates are under `output/session-20261029/native-qa-kit-v1/source/output/`; wpt_engine is auditing them. Do not promote the older 1695/1850 summary to current full-WPT status.
- Existing progress/tunnel processes are present. Validate their scope, authentication, and evidence freshness before publishing. `output/` includes tunnel private keys and tokens; it must never be staged or shared wholesale.

## Next work

Audit existing WPT raw reports and Chrome reference; report scope and denominator first. Run a fresh focused baseline against an explicitly identified binary/source revision, then regression-first standards fixes. Verify native Mac launch and inspect actual IPA/simulator feasibility. Reuse resource monitors, sample every second, and record timings/limits.
Prioritize important DOM/events/URL/encoding/fetch/Promise behavior by evidence rather than aggregate score inflation. Treat simulator, macOS-native and same-device-IPA evidence as separate gates.
Publish only reviewed files and committed tracked source, using the existing project release workflow where possible. Check a progress message inbox if available; retain raw metrics and run receipts separately from user-facing summaries.
