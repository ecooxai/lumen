# Public results capture adapter

The candidate 214 capture and encoder have completed and were independently reviewed. For future runs, use this workflow only after the final broad validation and all browser/monitor cleanup gates drain. This captures the final public results page in pinned Chromium, not Lumen UI footage. Existing validation runners and protected capture internals are unchanged.

Create a new JSON plan after the final public content is reviewed. Required keys are `page` with exact public `url` and SHA256, `shots` with 1–5 CSS selectors (or `top`), and `credit_selector`. Optional `assets` supplies additional exact URL/SHA256 pairs; `ready_selector` should identify the completed results region; `duration_seconds` is 30–45 (default36). The credit selector is added as a shot automatically and must be fully in view, containing GPT-6 Astra Pro, Built-in agents, RubyWebterm, Chromium, and FFmpeg. Put the honest Chromium-rendered results-walkthrough label in the reviewed page itself.

Run the adapter through `python3 tools/codex_compile.py python3 agent/ops/linux-wpt-20261030-session7/showcase/capture-public-results.py --plan PLAN.json --plan-sha256 SHA256 --output NEW_DIRECTORY`. It fixes the viewport to 1024×768 at 1×, uses a fresh profile, checks public content and browser pins before/after, records 1-second owned/cgroup resources, drains only owned processes, writes PNG bytes only to files, and emits only a compact aggregate on stdout.

Inspect the PNG frames and local receipt before using the generated `encode-command.json`. The adapter never executes FFmpeg. The command uses H.264, two encoder threads, square pixels, fixed 4:3 dimensions and 30–45 seconds. Run that argument array through the normal CPU guard with a separate1s monitor, then verify the MP4 dimensions/duration and actual encoder/monitor cleanup. Publish only reviewed PNG/MP4 outputs, not local driver/resource/receipt records.


## Completed candidate 214 showcase

The sealed plan is plan214.json (SHA-256: 2851615f6f4c63424ce261b194c5ea43850bce1b52719bbde64cf004b20bd981). Capture output is capture214-final, with five original 1024×768 PNGs from the stable public showcase214.html (SHA-256: 04ede4c165bb678e8f95091fe3ae16fe82d9235b191fcd0c1f4c945553feaeac). Source frames are unchanged; frame-02 is the public comparison poster. The original page remains unchanged even though index.html now embeds the video.

The reviewed standalone encoder is encode-public-results.py, SHA-256: 332a51addcc7a285202803c9c88ef617420988f52016dcd05799f9039e59cf2e. It rebuilds the concat file solely from pinned frame metadata, uses two encoder/one filter threads, samples its own Linux process group every second without an optional Python dependency, and verifies format, dimensions, duration, square pixels, hashes and cleanup. It does not inspect protected validation internals.

The final MP4 is 36.000000 seconds, 1024×768, H.264 / yuv420p / SAR 1:1, and 467,513 bytes; SHA-256: 0df401b60376874fafe73b7494dba1c57ad946b37e10f53779ec2c98e52e8c68. Actual encoder exit 0; 11 one-second samples; owned process group and monitor drained. No audio or image generation was used. Public media/index bytes were verified after non-forced Pages push eb741a324ab2606da47fea9370a2dfd177d37cdc.

Public video: https://ecooxai.github.io/lumen/lumen-linux-gpt-6-astra-pro-builtin-rubywebterm-chromium-ffmpeg-checkpoint214.mp4

Keep raw driver, profile, monitor and local controller receipts private; the published showcase214 JSON is a minimized public projection. Never describe this results presentation as native Lumen UI footage or a performance benchmark.

The exact executed encoder bytes (SHA-256332a51addcc7a285202803c9c88ef617420988f52016dcd05799f9039e59cf2e) are retained in ../artifacts214/lumen_gpt-6-astra-pro_builtin-rubywebterm_release-helper-snapshots214.tar.gz. The maintained helper now has SHA-256 dae8a7d606d94640e02dd2ee2365cc3be82c58605352a45d90c547e75521be5a after removing only an extra blank line at EOF; its parsed AST is identical. No media or engine test result was regenerated or relabeled. The companion release-helper-formatting214.json records each exact snapshot/current pair.
