# Android Vulkan presentation measurement, 2026-10-15

MI8, Android 15, Vulkan, local thumbnail-scroll fixture. Retained image-blend
APK SHA256 `4af25ce3e5242b8ea9aab899d09c96ba29b5afd06a360e069ba7d2a4b7a0226e`.
The earlier 127-frame tail was insufficient to describe sustained scrolling.
This run polls SurfaceFlinger latency every 0.5 seconds during twelve 1.2-second
ADB drags, deduplicates actual presentation timestamps and excludes 100 ms at
each gesture boundary. Process and all threads sampled at 1 Hz throughout.

| Measurement | Retained build | Experimental latency hint 1 |
| --- | ---: | ---: |
| Captured frames | 929 | 927 |
| Within-drag intervals | 752 | 752 |
| Median presentation interval, ms | 16.704 | 16.698 |
| 95th percentile interval, ms | 16.735 | 16.728 |
| Maximum interval, ms | 16.859 | 16.920 |
| Within-drag intervals above 25 ms | 0 | 0 |
| Median frame-ready to presentation delay, ms | 23.049 | 23.214 |
| Process CPU during drags, % of one core | 46.20 | 45.25 |
| RSS at drag end, MiB | 251.50 | 253.11 |

The experiment set Android Vulkan's `desiredMaximumFrameLatency` to 1,
retaining FIFO presentation. It did not improve the measured queue delay or
memory footprint. The configuration and associated experimental test edits were
removed, and the retained build reinstalled. No queue-depth change is retained.
The experiment passed lifecycle 9/9 UBSan, upload 15/15, native/Android builds,
APK native dependency/API 29 checks and v2 signature verification before testing.

These are steady-drag measurements of one controlled page, not end-to-end touch
latency, general website smoothness, or a claim that flings/page loads never lag.
Frame-ready to presentation delay is not input-to-display latency. SurfaceFlinger
column interpretation follows AOSP FrameTracker's desired/actual/ready dump.

Artifacts: `build/codex-vulkan-pacing-20261015/`, including capture.py,
summarize.py, baseline-timing.json, candidate-timing.json, both summaries and
samples.jsonl. The rejected APK is `lumen-vulkan-latency1-debug.apk` (SHA256
`e7a9f86d94fe02f62fe273f643e857e755cd2834573172a05b3750f005ced982`).
Real YouTube touch testing followed this experiment; see continuation notes for
its separate results and installed APK, since later builds replace libmain.so.
