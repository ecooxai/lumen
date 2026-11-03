# Continue Lumen Linux from the session18 development checkpoint

Read AGENTS.md first, then this file, agent/devsummary.md and the complete verbatim
agent/task.md. Continue Linux standards/real-browser compatibility toward Chromium;
this session did not establish whole-platform parity or 95/100 overall quality.

## Environment and exact source

Use RubyWebTerm in `/mnt/p3/project/browser/lumen-android/lumen-android`
(alias `/home/a/project/browser/lumen-android/lumen-android`). WPT lives at
`/mnt/p3/project/browser/wpt`. Branch:
`gpt-6-astra-pro_builtin-agents_rubywebterm_session18-20261103`.
Tested engine source: fcf2d2d83686a554f492fbaf992173b12d924b64;
engine commit: d93c9769c750dacbfd4bf415aa0523fbc0de7d6f;
source tree: 905f9cd45711a1730d05807d85d99002b34e31a0.
Linux binary: build/codex-native/lumen-astra-i1808,
SHA 79af7023284551d597ce61ff4a5e3954a7edd2a43b4b8ee2af5d63a1658ff46d.
Manifest 742 inputs: 5c73f1b303089995b99e281ae20025600ea0c5345848eaf09705ca9d7d7ca2eb.
Later docs/build-orchestration commits preserve that engine; inspect the explicit mapping,
not just current HEAD. Preserve current work before creating the next named branch.

## Verification and limitations

The single preregistered unchanged-engine reliability replay passed: 759/1248 selected cases (60.8173%) and 22,826/26,245 assertions (86.9728%). All 22,826 required historical PASS identities were retained with no harness regressions. Compared with starting 1705, this adds 45 passing cases and 189 assertion passes. Both final unchanged namespace repetitions passed 51/51 with harness OK, real exit 0 and cleanup. Native 218/218 and retained 1318/1318 also passed. The first complete 1808 run remains HOLD: 758/1248 with 51/51 namespace assertions PASS but harness TIMEOUT; its cause remains unknown and no source fix is claimed. This is qualified development evidence, not production certification, whole-WPT parity or the unachieved 95/100 overall goal. On 26,004 exact common reference identities: 22,037 both PASS, 2,444 Chromium-only PASS, 586 Lumen-only PASS and 937 neither PASS. Independent recount SHA d60094edb3bde0aa72ba9c50fe05714a31c0df8dac8873e6fd0176b58bdc0f4c; namespace review SHA ff06f877ed01b0bffddb2ae85c480c97b7f2d9757308e2e95673e41f6382e12a.

The selected 1248 cases are 1247 source files in ten DOM/CSS testharness families, HTTP and
default-timeout only. The 44 long-timeout and one HTTPS exclusion predate this session.
All selected source blobs equal pinned upstream WPT 0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb.
The pinned local vendor-automation/native-report adapters are documented and unchanged.
Reference Chromium 150.0.7871.114: 1062/1248 cases, 24541/26064 assertions; compare shared
26,004 exact identities separately. Do not advertise this sample as all WPT or visual tests.

The first complete 1808 run remains a real HOLD (758 cases, 22,826 assertions): all prior
assertions passed, but namespace harness completion timed out with all 51 child checks
PASS. All three unchanged before/current isolated observations passed under unequal host
load; cause remains unconfirmed. The subsequent single preregistered reliability result
is separate evidence, not a source fix or deletion of the failure. No unlimited retry loop.

Historical 1807's original executable/build-state is unavailable after a disclosed copied-CLI
artifact overwrite; its raw partial results/guard records/oracles remain. Replacement bytes
are explicitly quarantined. Never reuse 1807 or claim a rebuilt file is the original.


The selected family counts are: CSS Values 282; CSSOM 265; CSS Syntax 48; CSS Variables 63;
CSS Sizing parsing 22; CSS Backgrounds parsing 74; CSS Transitions parsing 18;
CSS Animations parsing 41; DOM Nodes 394; DOM Ranges 41. The authoritative inputs are
`agent/validation/linux-wpt-20261101-session13/expanded-selection.json`,
`expanded-urls.json` and `expanded-manifest.json`. Official WPT manifest version9 SHA-256:
`440d5dc80c78ab184d977a7d58929732ce480522550a11c5664f17c76b5317ca`.
Selected manifest SHA-256: `a97bc7c43083140f02c5f7ae3a46de905c8da6506c6a834928dbaa99d3ae63a6`.
The reference resource bytes were shared, but native and WebDriver automation backends differ.

## Next investigations, in priority order

1. Investigate namespace/frame startup and parent-load completion reliability under controlled resource conditions. The first full run timed out after all 51 assertions passed; the cause remains unknown. Preserve the failure and every successful observation, with no timeout relaxation or test-specific bypass.
2. Improve stylesheet and CSSOM lifecycle behavior that affects real pages: import loading, constructable sheets, source reflection, media invalidation, and atomic frame/post-connection operations. The final reliability shared-reference comparison records 449 CSSOM and 106 DOM-node assertion gaps; use the preserved final replay ranking to select a bounded next cohort.
3. Address typed `attr()` values, cycles, `calc-size()` and context-sensitive numeric evaluation with explicit value provenance. CSS Values accounts for 1,230 of the final 2,444 shared Chromium-only passes; the large attr-all-types and calc-size-parsing files contribute 116 and 68 respectively. These counts are assertion gaps, not supported-feature percentages.
4. Extend animation/background shorthand and selector grammar using unchanged complete valid/invalid cohorts. The measured final gaps include 230 animation-parsing and 192 background-parsing assertions; preserve the already accepted alias, priority, numeric and keyword behavior.
5. Add separately declared rendering, network, visual/reftest and real-site coverage. The current HTTP DOM/CSS testharness sample cannot establish those broader platform properties.

Preserve explicit gaps: 23 unsupported stylesheet/custom/inherited/typed-length/pseudo/shadow
tree-count controls; no runtime random sampler/cache; no native sizing shorthand expansion;
limited animation/timeline solver/initial metadata; incomplete stylesheet imports/sets/external
CSSOM source reflection/native media invalidation. Recheck relevant existing failures before
starting a bounded fix, use primary specs, freeze oracles, and obtain independent review.

## Safe build and validation workflow

The ordinary build helper now permanently reserves candidate IDs and requires an exact
expected HEAD. Use a new ID even after a failed attempt. Do not edit prior output directories.
Example shape (fill placeholders, use fresh output paths):

```sh
python3 agent/validation/linux-wpt-20261101-session10/run_monitored.py --output <fresh-monitor-dir> --label <build-label> -- python3 tools/codex_compile.py python3 agent/validation/build_candidate.py --iteration <fresh-id> --expected-head <full-frozen-HEAD> --startup --jobs 2
python3 agent/validation/linux-wpt-20261101-session10/run_monitored.py --output <fresh-monitor-dir> --label <cohort-label> -- python3 tools/codex_compile.py python3 agent/validation/linux-wpt-20261101-session10/run_pinned_cohort.py --candidate <immutable-id> --urls agent/validation/linux-wpt-20261101-session13/expanded-urls.json --manifest agent/validation/linux-wpt-20261101-session13/expanded-manifest.json --output <fresh-cohort-dir>
```

Use same-model built-in source/test/review agents with one canonical Git owner. Keep the
main agent on CTO/coordination work. Serialize heavy jobs, keep the 480% quota/one-second
CPU/RSS monitors, and diagnose concrete slowness rather than suppressing checks. Do not
alter WPT tests, expectations, timeouts, skips, trusted guards or historical receipts.
Preserve every recorded prior PASS identity and honest held/qualified labels.

## Runtime, source backup and next-agent delivery state

Linux display :0 was restarted successfully at 2026-11-03T12:48:21.656938+00:00 with the exact candidate 1808 binary. Actual keyboard input, navigation/history and four own-page semantic checks passed; clean and attributed 36-second 1280x960 recordings were preserved. Android was built from exact fcf, passed all 33 original APK/RTC checks, installed with install-r, and finally restarted at 2026-11-03T12:56:33.126372+00:00. Installed APK SHA-256 is 900e38488462b364076db09c574c21405e26e5b5a474b850537b953675cf4a02; package UID 10196 was retained and no app data was cleared. Six stable foreground/liveness samples and four actual page checks passed after restart. Android smoke coverage is separate from Linux WPT. Both verification-controller corrections and failed attempts remain recorded; neither changed engine/APK bytes. The exact-source delivery window was closed and the guarded task branch restored, with all 742 inputs/binary/build-state unchanged. Final runtime receipt hashes: Linux 846747c9ac1dd03d7f8d58fc930cb29909d5f868d7df1fd3c3a556c9b2e14cdb, Android ce2c885faf24dc7e5bf07c90c3fd318321a58860974bf192f8d6abb8f81481cd.

Exact tested source is durably published at public commit ec40e1f4a60c4e23921ee66cf74f7c09218a0344 on gpt-6-astra-pro-builtin-agents-rubywebterm-linux-source1808-20261103 (https://github.com/ecooxai/lumen/tree/gpt-6-astra-pro-builtin-agents-rubywebterm-linux-source1808-20261103). The final metadata-only child and archive/report/media publication are recorded separately in agent/ops/session18/delivery-runtime and current progress status; preparation is never treated as publication. All 742 tested input hashes and the 842-file approved public inventory were independently reviewed. Public history excludes canonical private Git objects, profiles, credentials, message queues and validation machinery. Live HTTPS status/source download: https://a0bb38e846444140.5x7aqstuceku.opentunnel.xyz/ . Its source_archive_candidate/hash/label distinguish the current download from a newly prepared artifact. Source recovery is not an independent rebuild. The PDF/video artifact handoff was still being finalized at this documentation timestamp; consult actual publication/readback receipts rather than infer success.

Preserve profiles and Android data. Verify exact task-owned executable/PID/start ticks or
known package/UID before stopping/restarting. Never terminate unrelated host load. Record
new source, binary, APK, runtime, Git/archive and test identities explicitly. Public Git must
use approved public ancestry and exact source overlays, excluding canonical private history,
validation internals, profiles, credentials and message queues. Keep the final dashboard
configuration/session state accurate; a live page does not imply an active responding agent.

Session started 2026-11-03T06:58:59Z. Final recorded handoff: 2026-11-03T12:59:48.301875+00:00;
measured wall interval: 21649.302s. Task/agent intervals overlap and are not added
into session wall time or invented model-compute time. Full timing evidence remains in
agent/devsummary.md and agent/source-work/session18/final-evidence-review/.
