# Android Blob allocation optimization, 2026-10-15

Vulkan remains the priority; the daily-browser goal is unfinished.

## Finding and change

A bounded 30-second V8 profile of the YouTube error page identified repeated
Blob construction from individual characters. The public player helper computes
`new Blob(q.split("")).size`. Lumen previously allocated a native encoded buffer
for every character, followed by the final concatenation allocation.

The Blob constructor now batches adjacent text parts into a single encoding
operation. Each part is converted to a well-formed string before joining so
surrogates split across parts remain separate replacement characters. Mixed
binary/text ordering and copied output ownership are preserved. This follows
[File API part processing](https://w3c.github.io/FileAPI/#process-blob-parts).
It does not establish full Blob API conformance.

## Validation and measurements

17 focused host tests pass (Blob, Request state, preflight streaming and video
frame callbacks). The allocation regression fails on the previous source:
40,960 encoding calls versus one after the change. Unicode, mixed part order,
view offsets, snapshot ownership and string coercion are covered.

The MI 8 Android 15 device fixture passes all seven checks before and after.
For 20 constructions from 32,768 one-character parts:

| Measurement | Before | After |
| --- | ---: | ---: |
| Total construction time | 1,180.85 ms | 121.49 ms |
| Sampled peak process RSS | 302.86 MiB | 241.06 MiB |
| Mean settled CPU, 30 samples | 0.2970% | 0.3340% |
| Settled RSS | 295.35 MiB | 238.92 MiB |

This is about 9.7 times faster construction and 61.8 MiB lower sampled peak RSS
for this fixture, not a whole-browser or whole-YouTube speedup. Both runs used
continuous one-second process/thread sampling. The baseline APK contained dormant
profiling hooks; profiling did not start during the fixture measurement.

Actual YouTube still displays "Something went wrong" with the final APK.
The last 30 settled samples average 4.3941% CPU (peak 9.9010%), with final RSS
421.44 MiB. This exceeds the 0–3% idle target. It does not establish a significant
whole-site idle improvement over earlier runs. Playback stability and controls
remain unresolved; further allocation/scheduling investigation is necessary.

## Artifact and evidence

Evidence directory: `build/codex-vulkan-tasks-20261015/`. It contains the V8
profile and parsed report, baseline/optimized fixture results, continuous CPU
samples, `youtube-blob-final-idle.json`, and `final-1.png` through `final-4.png`.

Clean installed artifact: `lumen-vulkan-blob-debug.apk`, 85,240,084 bytes.
SHA256: `5ca4a8f8a2e8c7ccb174e646aed2acbf28b2cb796ca511a1e700c6769b7338d9`.
Native dependency closure and APK signature verification pass. Build used the
required capped wrapper and verified V8 prebuilt. Temporary native profiling
code is removed; no credentials were entered. The app was stopped after testing,
the owned sampler/server joined, and the temporary reverse port removed.

ChatGPT/Gemini/Google login, zoom and full daily feature acceptance remain
unfinished. OpenGL ES and software-mode optimization remain deferred.
