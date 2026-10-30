# Lume chrome retention review — task 102900614

Prepared by GPT-6 Astra Pro, builtin apple_runtime agent, with Mac execution
brokered through devinmac3d. Updated from the authoritative Mac build and fixed
resource receipts after the candidate measurement ended at 00:04:25 UTC.

## Purpose and exact scope

The measured glyph-cache artifact `4773b85d28d3b1a7c17a1beee5225abc5141bb4cec4b713d409464ebe42582e9`
still exceeds the unchanged 10% p95 scrolling CPU ceiling: its p95 is 12.060%.
Its five-second sampling profile contains 40 chrome raster samples, while the
display hash remains unchanged during the fixed scrolling workload. Most
other main-thread wall samples wait in Metal; those waits are not CPU spinning.

The candidate retains already painted toolbar/sidebar pixels. It still builds
and hashes the dynamic browser chrome on every full frame. Page rasterization,
input delivery, fractional scroll positions, layout, GPU submissions, timing,
and output dimensions are unchanged. The state is a fixed small App member;
there is no additional heap allocation or persistent glyph storage.

## Invalidation review

The page copy writes only the bottom-right page rectangle. The retained chrome
occupies its complementary toolbar and sidebar rectangles. Reuse requires
the same underlying pixel pointer, width, height, stride, scale, page offsets,
clear color and rebuilt display-list hash.

Frame reallocation explicitly invalidates the state even if an allocator
reuses the old address and dimensions. Viewport origin shifts change the
subview pointer. Existing safe-area clearing only touches pixels outside the
current chrome subview. Geometry and backing changes are separately keyed.

Any floating overlay disables reuse and every actual overlay draw invalidates
the cache, including partial renders. The first full frame after an overlay
disappears therefore restores the clean chrome. Dynamic navigation, loading,
hover, text and icon changes still rebuild the list and affect its existing
content hash. This preserves the existing display-hash collision model; it
does not introduce a new text or image identity scheme.

App state begins with the existing zero-initialization. All framebuffer
writes in the reviewed render path are either the guarded chrome paint,
nonoverlapping page copy, safe-area clear, or explicitly invalidating overlay.

## Independent verification plan

The frozen fixture compares the candidate with the original paint function
over 20 sequential frames and both complete backing arrays, including canary
padding. The transitions cover changing page pixels, alpha and rounded layers,
content changes, overlay appearance/removal, same-address recreated storage,
scale, viewport origin, dimensions, sidebar/toolbar extent, clear color,
stride and replacement backing storage. It expects 15 invalidation paints and
five unchanged reuses.

The pre-candidate fallback always performs the original paint. The actual RED
result was 99/100, with exactly the missing-reuse assertion failing. Its raw log
SHA-256 is `f9e6a2b55fb44fa9749ce81053877e00a64b55a1f7959db6238f515d96def490`.
The candidate passed all 100 checks. Existing 12,000 scalar rectangle-oracle and
7,168 text-oracle cases remained unchanged.

The performance pair must use a fresh immutable reference after all other
production changes have frozen. The coordinator checks executable byte
identity, all-source manifests, generated/compiler dependencies and linked
dependency hashes. Only `src/app/main.c` and `src/app/chrome_paint.h` may differ.
It retains the original 115-second workload, all 1,875 wheel inputs, one-second
CPU/RSS telemetry and the 10% scrolling ceiling. Exact page pixels, a scoped
native-window capture and clean GPU logs are required alongside the resource
gate. A later final combined artifact still needs the 20-navigation soak.

## Reviewed hashes

| File | Reference SHA-256 | Candidate SHA-256 |
| --- | --- | --- |
| src/app/main.c | 820f482562977eb2b3c4d462892250a21549a39ff687c9d6a5a1a19c61795a1a | d76a2f3bd2c22d52e66ca2a4b32a851332aacbbd92b2d684babe475a6d8852fd |
| src/app/chrome_paint.h | 2c1d4f35ae7d73ddb9517b0701e7facb640a4f5484a828eb946dbfcfa28fcd00 | 1a85870834b5282ca6b0e53ad3a327073ff0343cdc8964a5d697cc86168eba68 |
| tests/test_render_overdraw.c | 32515b62aece48fd75fe0246866461c7438a195ba196921faaa64ab6c040feb0 | 32515b62aece48fd75fe0246866461c7438a195ba196921faaa64ab6c040feb0 |

Raster source remains
`ab8490489c5c3f670d903ee0959ded347464b5b3bcc577007db164d0012cedf1`;
GPU source remains
`93f7e48a28806f6ac57e0e3e9c9b3b8f9d5cfa57c20b1328abe57d43e983888a`.

## Actual paired outcome

The reference build/verification completed in 2.612s and reproduced the fresh
combined native executable
`07cca318e49e830d25280b9a8b5e345fe9ea7bf57b6515e80d12946f767068e8`.
Its source manifest has 202 entries, SHA-256
`8af303a92b57be1c2c9a9f6af13a4da6864b2fba609ce82b2cd924a5a408f825`;
the compiler dependency manifest has 569 entries, SHA-256
`4ed3da9bbce8f1c7d1977782eb4cb6d2fc0bccd0dee224fee313991b9a6cbf4e`.
Both content hashes were independently recomputed from the broker bytes.

The candidate build and deterministic verification completed in 3.584s,
including 2.104s for the monitored build. Its immutable native executable is
`build-apple-load-20261029/lumen-chrome-cache-candidate-f52a08ca9a1bc6cd`,
SHA-256 `f52a08ca9a1bc6cd79eb082d8bedd7b5851e23341ee7427ff01cddfb038a8a9c`.

| Fixed workload metric | Fresh reference 07cca | Candidate f52 |
| --- | ---: | ---: |
| Scrolling CPU p95 | 15.998% | 15.090% |
| Scrolling CPU mean | 12.058% | 11.679% |
| Scrolling samples / covered seconds | 34 / 33.008 | 35 / 33.998 |
| Final idle CPU p95 | 0.994% | 0.995% |
| Final idle RSS start→end | 154.172→154.125 MiB | 150.312→150.312 MiB |
| Final idle samples / covered seconds | 50 / 49.001 | 50 / 48.983 |

The candidate coordinator ran from 00:02:27.690022 to 00:04:25.172133 UTC,
117.482s. Its resource receipt is
`output/session-102900614/scroll-chrome-cache/receipt.json`, SHA-256
`ab8352381773f52b44e6b012069d8cb9803304050c1b00d5b7eee32fa28c7701`.
The fixed scroll gate still fails solely because 15.090% exceeds 10%.

The source and compiler dependency comparisons each contain exactly the two
reviewed App files. The coordinator reports exact page pixels, successful
PID-scoped native capture and clean GPU logs. The original 1,875 input schedule,
fractional end position 6000.07177734375 and unchanged runner remain in force.
The p95 reduction is about 5.7% relative to this fresh paired reference. The
older glyph-only 12.060% result is not used as this candidate's comparison.

## Evidence limits

This supports a modest renderer improvement and stable idle memory for this
artifact. It does not satisfy the fixed scrolling ceiling or replace the
same-artifact 20-navigation soak. Native Mac execution cannot establish the
requested identical signed iOS IPA working on Mac, iPad and iPhone. That
separate gate remains blocked by the recorded toolchain, dependency and
signing/artifact inventory. The project score remains with root's scorecard;
this review awards no new full-WPT or Apple-package points.
