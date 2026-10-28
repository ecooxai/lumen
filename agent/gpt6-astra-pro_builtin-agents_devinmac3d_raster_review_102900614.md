# Raster performance review — task 102900614

Review author: GPT-6 Astra Pro, built-in Apple runtime subagent; Mac execution
brokered through devinmac3d by the project manager. Reviewed 2026-10-28 UTC.

## Decision

The rectangle-span and glyph-cache changes are verified performance
improvements. They remain a candidate because the unchanged scrolling CPU
ceiling is 10%, and the latest measured p95 is 12.060%. This review supplies no
new WPT, device IPA, Simulator, or final navigation-soak acceptance points.

Production changes are limited to `src/paint/raster.c`. The existing fractional
scroll values, input cadence, frame cadence, rendering geometry, and fixed
resource thresholds are unchanged.

## Exact source and executable identity

| Item | SHA256 |
| --- | --- |
| Final span plus glyph source, `src/paint/raster.c` | `ab8490489c5c3f670d903ee0959ded347464b5b3bcc577007db164d0012cedf1` |
| Regression fixture, `tests/test_render_overdraw.c` | `c46fb31407ef24b9fa050b1aba964fb34d6cb707eb4be48c02f54a5806fe924f` |
| Immutable native glyph candidate | `4773b85d28d3b1a7c17a1beee5225abc5141bb4cec4b713d409464ebe42582e9` |
| Immutable unit fixture executable | `db6f38d5aeeda960ccf06fd044baefd09762bae20da04443791e8b42b0bd40b4` |
| Measured span-only native and rebuilt glyph reference | `970e6cafc95133ca62ec02f7c9deeaffe568f276dbafda39d117c4f04fc9217b` |
| Fixed lifecycle runner | `61f919ee0be6e2a972237d7ed7cf9e71f827ff3d9f6d2a8336cebca9fbd9cfb7` |
| Glyph resource receipt | `29b2175a2f2a3fae08dcf53b813d9e317a782e5728fe77b4365b368e19592b45` |

The glyph candidate is
`build-apple-load-20261029/lumen-glyph-cache-candidate-4773b85d28d3b1a7`.
The baseline was rebuilt after the new regression fixture was installed. Its
native bytes remained identical to the already measured span-only executable,
allowing that measurement to remain the controlled reference. The coordinator
verified both executable paths and both before/after baseline receipt hashes.
Production source and compiler dependency manifests differ only in
`src/paint/raster.c`. New legacy encoding changes were applied after the glyph
snapshot was frozen and are absent from this candidate.

## Pixel and lifetime review

Rectangle spans compute one premultiplied color for interior horizontal
pixels sharing the same coverage. Fractional edge calculations, rounded clips,
and the original alpha arithmetic remain intact. An independent frozen scalar
implementation checked 12,000 deterministic cases, including all opacity
values, fractional boundaries, thin rectangles, rounded paths, clipped
subviews, and nontrivial strides. Reference and span candidate passed 93/93
aggregate checks.

The glyph optimization uses 64 entries keyed by exact `Font *`, glyph ID, and
float scale, plus a 256-entry coverage/color lookup table. This adds about
2.5 KiB of bounded stack storage per raster invocation and no persistent
allocation. The color table preserves the original integer opacity division
and floating-point premultiplication order. A separate frozen scalar text
oracle checks 7,168 combinations across opacity, color, font, glyph, scale,
rounded clipping, cache collisions, and backing-store subviews. Repeated
translucent glyphs also match exactly. Before the implementation, only the two
cache-reuse assertions failed (95/97); the candidate passes 97/97.

Underlying glyph lifetime was reviewed independently of the frame-local cache.
`font_glyph` in `src/text/font.c` separately allocates each `Glyph` and its bitmap,
deep-copies FreeType bitmap data, and publishes the object under `font_mu`.
Later FreeType loads do not mutate the saved bitmap. `hm_put` in
`src/base/util.c` grows only the hash-entry array and copies pointer values;
it never relocates or frees the pointed-to glyph. Current font caches have no
eviction path. Font declaration/loading clears only the family-resolution
cache, leaving existing Font/Glyph allocations intact. A root-brokered whole-src
search found only new-variable-font map initialization, lookup, and insertion
as glyph-map accesses. This proof must be revisited if font eviction is added.

## Measured native results

The same fixed 1,875-event workload, final fractional scroll position, 1000×700
native viewport, 1 Hz CPU/RSS sampler, and 10% p95 scrolling ceiling were used.
No sampled diagnostic profile is counted as resource acceptance.

| Native candidate | Scroll p95 | Scroll mean | Settled RSS | Result |
| --- | ---: | ---: | ---: | --- |
| Stream/GPU reference `80a60752…` | 20.175% | See original receipt | See original receipt | CPU ceiling fails |
| Rectangle span `970e6caf…` | 16.935% | 12.348% | 152.656 MiB | CPU ceiling fails |
| Span plus glyph `4773b85d…` | 12.060% | 9.912% | 153.203 MiB | CPU ceiling fails |

The glyph resource coordinator ran from 23:31:23.333662 to 23:33:20.752734 UTC
(117.419 seconds). Scrolling supplied 35 observations over 33.995 seconds;
idle supplied 50 observations over 48.998 seconds. Idle CPU p95 was 0.991% and
settled RSS was unchanged. The captured page pixels and final fractional scroll
position matched the reference exactly. The PID-scoped native window capture
succeeded, and the native log contained no GPU validation, panic, or fatal
error. The sole reported acceptance issue was scrolling p95 above 10%.

The candidate build and unit run took 2.555 seconds. Their 1 Hz monitors were
active but these individual jobs were too short to provide meaningful interval
CPU estimates; their success is natural exit and unit output, not a CPU claim.

## Reproduction evidence

Under `output/session-102900614/`, preserve all original files in:

- `raster-span-reference-1` and `raster-span-candidate-1`.
- `scroll-raster-reference` and `scroll-raster-reference-control`.
- `scroll-raster-span` and `scroll-raster-span-control`.
- `glyph-cache-reference-1` and `glyph-cache-candidate-1`.
- `scroll-glyph-cache` and `scroll-glyph-cache-control`.

Each build directory contains the compiled-source and compiler/linker
dependency manifests. Each resource directory retains the original telemetry,
launch identity, binary before/after hashes, page screenshot, native capture,
and pixel comparison. Source inventories conservatively include all files
under `src`, so some archived WebTransport output files are a superset of actual
compiler inputs; their presence does not claim those logs were compiled.

The final combined browser still requires fresh fixed scrolling and
165-second navigation-soak runs on the exact same immutable final binary.
