# Android mobile identity and off-screen drawer sizing, 2026-10-15

The daily-browser goal is active and incomplete. Vulkan remains first; OpenGL ES
and software optimization are deferred. No credentials were entered.

## Changes

Android previously sent desktop Linux/x86_64 user agents and client hints.
Its Chrome compatibility profile now uses the reduced Android mobile format;
HTTP client hints and navigator.userAgentData identify Android/mobile. The
Firefox and Lumen profiles also use Android tokens. Native navigator.platform
uses the Android compatibility value. Desktop profiles are unchanged.
userAgentData.toJSON() and getHighEntropyValues() now return the low-entropy
brand/mobile/platform values instead of empty dictionaries. Additional device
identifiers are not exposed. This is not full UA Client Hints conformance.

The literal Android 10/K tokens follow the
[Chromium reduced UA format](https://www.chromium.org/updates/ua-reduction/),
not the installed Android version. Low-entropy return values are described by
the [UA Client Hints specification](https://wicg.github.io/ua-client-hints/).
Touch capability is unchanged: full DOM touch input is not implemented here.

YouTube now redirects to its mobile site. This exposed a layout error in its
off-screen drawer: width:70% with left:-100% resolved width against the remaining
space after subtracting the negative left inset, doubling the percentage basis.
At a 540 CSS-pixel viewport the drawer was 756 pixels wide instead of 378, and
covered the left 216 pixels of the page. Specified widths now resolve against
their actual containing block. Percentage padding, margins and width limits in
that path use the same basis. Auto-width shrink-to-fit behavior is unchanged;
this does not establish general absolute-positioning conformance.

## Vulkan scrolling measurement

MI 8, Android 15, Vulkan, pixel_scale=200. Fixture:
tests/js/positioned-drawer.html, 160 rows, fixed header/footer and the off-screen
drawer. Six alternating swipes at x=700, y=1650 to 700 and back, 600 ms each,
one-second gaps. Same APK code except for the width repair; both benchmark builds
had the same temporary logging/preload support and paint profiling enabled. The
preload exits immediately on the local fixture. Neither benchmark is the final
clean APK. Process and thread CPU sampled once per second throughout all runs.

| Measurement | Before | After |
| --- | ---: | ---: |
| Scroll samples / elapsed intervals | 10 / 9.95 s | 10 / 9.99 s |
| Mean process CPU | 70.7192% | 33.4966% |
| Peak process CPU | 101% | 44.8980% |
| Settled idle, final 30 samples | 0.3327% | 0.3323% |
| Settled idle peak | 5.9406% | 7% |
| Settled RSS | 230.34 MiB | 229.43 MiB |

Mean scrolling CPU falls about 53% in this fixture only. The incorrect visible
full-height fixed drawer forced full repaints. After correction it is fully off
screen; paint logs show partial repaint and row reuse. Example continuous-motion
intervals change from 40 full frames / 1005 ms to 60 partial frames / 1014 ms.
These are paint counts, not compositor frame-time or broad real-site acceptance.
Gesture tails and idle gaps affect counts. No cadence errors in these windows.
Screenshots confirm full-width edges and fixed header/footer after scrolling.

## Validation and artifact

- Three JS identity tests pass (Android, desktop, non-Chromium profiles).
- Real CSS/layout regression under UBSan: seven position/size assertions at each
  of two viewport widths. Before repair, twelve assertions fail, including the
  actual 756-versus-378 drawer defect; after repair all fourteen pass. Covers
  negative/positive insets, right/both insets, percentage padding/margin, nested
  absolute positioning and unchanged auto stretch.
- Six identity checks pass on the phone: network UA matches JS, Android/mobile
  identity, direct/JSON/async client hints and independent returned brand data.
  This local HTTP fixture does not inspect HTTPS client-hint headers.
- Phone drawer geometry report changes from failure (756) to pass (378).
- Final clean APK renders the real mobile YouTube page across the full viewport.
- Native ELF/dependency and APK v2 signature verification pass. Builds used the
  capped compilation wrapper, Gradle without a daemon and verified V8 prebuilt.

Clean installed APK:
build/codex-youtube-detail-20261015/lumen-vulkan-mobile-drawer-debug.apk

SHA256: 3a0a9eb9071df9708d568cc8a0b2836c5e0abd809927d7762e1835cf87cdfe5b

Evidence is in build/codex-youtube-detail-20261015/: native negative/positive
test output, identity-server.jsonl, drawer-{before,after}-stats.json, screenshots,
per-process logcat paint traces, artifact verification and samples.jsonl.

## YouTube remains incomplete

The earlier desktop failure is now captured precisely: at context time 56.704 s,
onDetailedError reports ump.spsrejectfailure / HTML5_SPS_UMP_STATUS_REJECTED.
The public player code maps a rejected UMP streaming status to this error. Early
HTTP 403 responses are also present, but do not establish the final cause.
The same public video continued playing in Brave on this device/network. That
comparison uses a different browser/client and is not a controlled engine test.

Mobile identity does not repair playback: the diagnostic mobile run reproduces
the same error at 86.909 s of context time, after approximately 70 seconds of
video. Its settled error-page window averages 3.3659% CPU, peak 5%, RSS 340.57 MiB.
Do not interpret the desktop/mobile RSS difference as a controlled memory gain.
The mobile frontend also throws "Class extends value undefined" at
mobile-ERsVff.js:1027:199 (class extends _.EX). That class is absent from the
downloaded root module. Its relationship to controls/playback is unproven.

The clean final run plays initially with the layout corrected. First center tap
removes the muted-playback prompt; a second tap leaves the same video image in
captures at 0.5, 3 and 7 seconds and later. Player controls do not appear. Pause
is inferred from stationary images, not instrumented on that clean run. Thus
the final run does not verify sustained playback, control hold, seek or resume.
Final 30 stationary post-tap samples: mean 3.0287% CPU, peak 4%, RSS 318.89 MiB,
no cadence errors. An earlier window averaged 3.3985%, peak 12%; both include
their spikes. Idle acceptance remains provisional.

The earlier desktop Xch/_.qE8 profile is now identified from downloaded public
kevlar.js as periodic generated consistency/adblock checks, not an ordinary page
response cache. Do not suppress site checks or treat them as proof of a browser
bug. Profile remaining browser costs and fix demonstrated API/engine behavior.

Next: diagnose the missing mobile controls/frontend exception and rejected
streaming status; validate real-site scrolling and settled CPU. ChatGPT/Gemini,
Google login, zoom and full daily-browser acceptance remain unfinished.

Temporary native logging/preload code was restored before the final build.
Private probe/log/profile marker files removed; app stopped; sampler/server
stopped and joined; owned adb reverse removed. The clean APK remains installed.
