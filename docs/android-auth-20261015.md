# Android Google / ChatGPT authentication — 2026-10-15

Priority: Google sign-in and ChatGPT, including Google OAuth, on the connected
MI8. Other renderer work remains deferred; Android retains its Vulkan path.

## Changes

- Implement document and shadow-root adopted stylesheets. Constructed sheets
  apply only to adopting scopes; updates propagate to those scopes. Shadow
  stylesheet collection and selector matching respect their tree scope.
- Add dialog invoker commands, CommandEvent / ToggleEvent, modal state,
  cancellation, focus restoration and dialog lifecycle behavior.
- Keep `@starting-style` entry values out of the final CSS cascade. Entry
  transitions themselves are not implemented by this change.
- Load initial preload links and recognize a link's transition from preload to
  stylesheet even when its href stays unchanged.
- Identify as Lumen on accounts.google.com in the request and navigator APIs,
  including redirects, rather than using the selected Chrome/Firefox identity.
  Other origins retain the user's compatibility preference. This follows
  [Google's browser requirements](https://developers.googleblog.com/en/guidance-to-developers-affected-by-our-effort-to-block-less-secure-browsers-and-applications/).

The stylesheet implementation still uses internal style-node mirrors and is not
a claim of complete CSSOM conformance. Modal top-layer/backdrop, inertness and
focus trapping are not fully implemented by the dialog changes.

## Verified

- Native and Android builds through the shared compilation CPU limiter.
- 17 native tests pass: 12 dialog modes, 4 starting-style cases and auth identity.
- Full native browser rendering: adopted-sheets fixture 8/8 checks pass;
  preload-to-stylesheet load-event fixture passes.
- APK arm64/API29 native dependency checks and v2 signature verification pass.
- Installed on the MI8 without clearing application data.
- Clean runtime, without the temporary injected debugging script: ChatGPT's
  Log in button opens its login dialog, Continue with Google reaches Google's
  OpenAI OAuth page, email and password advance to Google's account-verification
  prompt. The earlier Chrome-identity run was rejected after the email step as
  an insecure/unsupported browser. The new run reached verification successfully.

The owner completed Google's verification and the browser returned to signed-in
ChatGPT. Sending prompts and receiving streamed replies now work; the signed-in
session survives replacement APK installations without clearing data. Long-reply
performance remains above the requested CPU target. See
`android-chatgpt-performance-20261015.md` for the ongoing measurements and fixes.
No password or complete OAuth URL is stored in this report.

## Artifact and measurements

Original authentication APK: `build/codex-auth-20261015/lumen-auth-debug.apk`.
SHA256: `c75e6c01ea642bdbd47f730f7a856d0964a7c413cf7b1ed1373115f59d8e8810`.
Temporary auth-debug startup hooks were removed before this build. Their
device marker, script and log were removed before entering the account details.

The phone process and all threads were sampled each second throughout the run
by `build/codex-vulkan-pacing-20261015/monitor.py`. The previous Google rejection
page settled to 0.20% mean CPU over 30 samples (3% peak, 309.6 MiB RSS). ChatGPT's
loading/hydration interval averaged 51.6% over a 20-sample window (123.47% peak,
604 MiB RSS); this is not an idle or typing result. The first verification-page
window still included loading and averaged 9.82% (41% peak, 288.8 MiB RSS).

Native render-fixture CPU was sampled per process and all threads as well:
adopted-sheets sampled 26.88% during initialization, then 1–3%; preload sampled
14.99% during initialization, then 0–2%. These short screenshot-driven tests
validate rendering, not Android performance.
