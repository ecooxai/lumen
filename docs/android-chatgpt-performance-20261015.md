# ChatGPT streaming and long conversations — 2026-10-15

Work in progress on MI8 / Android 15, Vulkan renderer. Google authentication and
signed-in streamed replies work. The requested CPU target (<50% during streaming
and interaction, including long conversations) is not yet established.

## Changes under test

- Separate stylesheet invalidation from ordinary DOM mutations; reuse tokenized
  class names instead of splitting/re-interning them on every selector match.
- Bound selector-match memoization; the latest experiment reuses entries across
  selector-neutral edits using separate DOM and selector generations.
- Filter CSS rules using a necessary class on the subject or its ancestry.
  Positive alternatives must all imply that class; negative/relational selectors
  and sibling boundaries are handled conservatively. Hash collisions cause extra
  matching, never skipped matches. Group unkeyed rules by that filter to skip
  entire irrelevant buckets.
- Cache only parsed query-selector syntax per document, with 64 bounded slots
  and a 4096-byte input limit per cached selector. Matching remains live across
  class, hover, scope and DOM changes; larger selectors use temporary storage.
- Make element scrolling invalidate paint without invalidating style/layout.
  Include this paint generation in iframe surface reuse checks.
- Propagate visible overflow through constrained flex wrappers, preserving
  clipping boundaries, fixed descendants and resize clamping.
- Implement shared sticky offsets for painting, DOM rectangles and hit tests.
  Tests cover top/bottom/horizontal offsets, containing-block limits, oversized
  boxes and translated ancestors. This is not full CSS positioning conformance.

## Measurements so far

CPU is aggregate application-process CPU sampled every second (100% = one core).
All browser threads are captured alongside it. This differs from a percentage
normalized across all eight cores. Sampling continues through every runtime test.

Separate live requests used the same prompt asking for a 1500-word, twenty-section
browser testing guide. Responses and server timing vary, so these are diagnostic
runs rather than a controlled benchmark. The first ~35 seconds are summarized;
subsequent idle samples are excluded.

| Candidate | Samples | Mean CPU | Peak CPU |
| --- | ---: | ---: | ---: |
| Original authentication build | 35 | 95.0% | 172.3% |
| Class tokens / stylesheet generations | 35 | 75.9% | 213.4% |
| Match memo / overflow / paint-only scrolling | 36 | 88.3% | 209.0% |
| Necessary ancestor class filter | 36 | 80.1% | 154.5% |

The last candidate's simpleperf trace attributes 39.0% inclusive samples to
style recalculation (previous candidate 69.5%), 7.7% to Element.matches calls,
and 5.5% to selector parsing. V8 worker compilation also contributes during
these newly loaded sessions. This motivated the syntax cache and rule buckets.

Original settled idle: 2.81% mean / 6.19% peak (15 samples, 494.5 MiB RSS).
Paint-only scrolling candidate settled idle: 3.73% mean / 5.10% peak
(30 samples, 554.9 MiB RSS). Neither is a long-session stability result.

## Validation and artifacts

20 targeted native tests pass: auth user agent, 12 dialog command cases, four
starting-style cases, DOM/style/query cache tracking, nested overflow and sticky
geometry. Native and Android builds use the mandatory shared CPU compilation
limiter. APK arm64/API29 dependency and v2 signature checks pass.

Current phone candidate: `build/codex-auth-20261015/lumen-empty-candidates-debug.apk`.
Matching unstripped library: `empty-candidates-symbols/libmain.so` in that directory.
Earlier APKs, matching libraries and simpleperf traces are retained there.
No app data was cleared. No credentials or complete OAuth URLs are recorded.

Runtime monitoring: `build/codex-vulkan-pacing-20261015/samples.jsonl` and `stage`.
Long same-conversation follow-up measurements and phone sticky/control validation
are still in progress. The previous overflow bug prevented effective scrolling
in the original baseline; its swipe CPU must not be presented as equivalent work.

## Same-conversation checks (before final optimization)

The sticky/query candidate reopened the original long test conversation and
completed two further long replies (three total). The composer remained visible,
accepted input, and the jump-to-latest button reached the end of the conversation.
These are genuine longer-session checks, not separate fresh chats.

| Action | Samples | Mean CPU | Peak CPU |
| --- | ---: | ---: | ---: |
| Typing follow-up 2, seven characters per ~0.7 sec | 18 | 26.9% | 92.3% |
| Streaming reply 2, first 35 seconds | 36 | 101.3% | 148.0% |
| Typing follow-up 3 at the same pace | 19 | 49.0% | 103.9% |
| Streaming reply 3, first 35 seconds | 37 | 105.4% | 167.3% |

Typing reply 3 was profiled (24 seconds including a short tail after typing):
43.5% of samples were in style recalculation, 28.7% in rule collection, 9.1% in
attribute lookup, 8.0% intrinsic size work and 9.4% rendering (inclusive costs
overlap). Reply 2 streaming attributed 44.9% to style recalculation. Thus history
scaling is still a real issue, not a passed test.

An ancestor-only filter experiment, after reloading the three-reply conversation,
measured typing 74.1% / 143.4% peak (16 samples), streaming reply 4 106.3% / 204.0%
peak (36 samples). It replaced a useful subject filter for some unkeyed rules.
The next source keeps both necessary-class filters and adds nested subject
indexing and atom-based attribute lookup. It has not yet been measured here.


## Further long-session checks

The same conversation was extended to six long replies (each prompt requested
1,500 words and twenty numbered sections; actual output length varies).
The combined class filters, nested subject index and attribute-atom lookup build
averaged **107.90%**, peak **206.06%**, across 36 one-second samples for reply 5.
Its 35-second profile attributed **54.75%** to style recalculation, **33.28%** to
rule collection, and **27.89%** to selector matching. Geometry/computed-style
reads triggered most of these synchronous recalculations. CPU uses 100% per core.

CharacterData setters now preserve embedded NULs and avoid parent restyling for
ordinary nonempty text edits. This candidate conservatively invalidated the tree
on empty/nonempty transitions; the latest candidate indexes affected subjects
for `:empty`, ancestor `:has(:empty)`, and sibling selectors. CDATA now
participates in `:empty`. The focused regression checks verify layout generation,
zero element style matches on ordinary edits, and selector updates on transitions.
Reply 6 on this build still averaged **107.65%**, peak **200.00%** (36 samples).
Typing its prompt averaged **100.48%**, peak **129.70%** (14 samples); its typing
cadence was faster than earlier samples, so this is not a controlled speedup or
regression comparison. Neither result meets the target.

The paint-only scroll integration fixture exposed a separate iframe rendering
bug: `scrollTop` reached 240 but the parent page retained the red image. Iframe
image pointers remain stable, so damage hashes missed updated pixels. Mutable
images now carry a content version included in damage hashes, and a rendered
iframe bumps its enclosing document's paint generation. The fixture now paints
both parent and iframe green without forcing style/layout for scroll changes.
Native runtime monitoring sampled the process and all threads every second.

A further candidate retains the bounded selector-match cache across style passes
when only inline CSS or nonempty character data changes. Selectors that read
`[style]`, including nested forms, use the full DOM generation; other selectors
use a separate selector generation. Structural, class, general attribute and
state invalidations bump that generation. Sheet changes and forced style passes
reset the cache epoch. Tests cover custom-property changes, hover/class changes,
and adding a nested `[style]` selector. Android performance validation is pending.


## Six-reply history: controlled draft comparison

Using the same six completed replies, the same 177-character draft, seven
characters per batch and a 0.6-second pause plus ADB overhead:

| Candidate | Samples | Mean CPU | Peak CPU |
| --- | ---: | ---: | ---: |
| Text edit optimization, per-pass match cache | 19 | 54.7% | 180.8% |
| Persistent match cache | 19 | 65.4% | 114.0% |

Cache reuse alone did not improve mean typing CPU. The page was reloaded between
candidates, so hydration, thermal conditions and background page activity remain
possible confounders. Persistent-cache simpleperf still attributes substantial
time to style recalculation. These results do not establish the CPU target.

The next candidate adds necessary-class filtering inside nested selectors and
limits empty/nonempty text invalidation to the parent and indexed subjects of
`:empty`-dependent relational selectors. Native tests compare computed styles
against uncached matching through four empty/nonempty changes and text node
removal/insertion. All 20 focused tests passed before the insertion/removal
extension; the affected DOM/style test also passes with that extension. The
installed targeted-empty APK predates that final extension.

The scroll paint fixture also covers a nested iframe; parent, direct iframe and
nested iframe all reach the green area. Rendering uses image content versions
without turning scrolling into a DOM/style/layout mutation.


## Inheritance reuse candidate

The targeted-empty draft comparison averaged 73.0%, peak 129.6% (19 samples).
It did not establish an improvement. The trace attributed 57.7% to style
recalculation; debug tracing showed main-scroller inline-style changes restyling
4,944 elements in about 1.3 seconds per pass.

The next candidate tracks consulted custom-property names with a conservative
bloom filter. During selector-neutral passes, descendants whose regular inherited
values are unchanged and which do not consult the changed variables reuse their
computed style. Their immutable custom-property maps are rebased to the new
parent so inherited values remain current, including for getComputedStyle.
Explicit inheritance, container queries/units, style-attribute selector
dependencies and flattened maps conservatively take the ordinary cascade path.
Root font-size changes and ancestor display:none transitions disable reuse for
the remainder of the pass, preserving rem units and animation visibility.
Pseudo-element dependencies are included even when their current content is absent.

Tests compare incremental results against a full cascade, including variables,
local overrides, pseudo content, explicit inheritance, [style] selectors and
container style queries. A forty-level custom-property chain covers flattening.
The 20 focused native checks pass. ASan/UBSan also uncovered an empty qsort call
and leaked parsed inline declarations when nodes were freed; both are fixed in
source, and DOM/style tests now pass with address, undefined-behavior and leak
checking enabled. The current mutation-trace APK includes both fixes, root font/visibility
corrections, and the inheritance optimization. Twenty focused native tests pass
on this revision; root-context tests also pass under ASan/UBSan/leak checking.


## Inheritance experiment results and keyboard separation

With the same six-reply history and original three-second keyboard settle:

| Candidate | Samples | Mean CPU | Peak CPU |
| --- | ---: | ---: | ---: |
| Inherited custom maps, full-chain comparisons | 20 | 102.5% | 133.3% |
| Local-map comparisons / narrower container fallback | 19 | 81.5% | 188.1% |

The first version spent 28.3% of profile samples in hash lookup; restricting
comparisons to local maps removed that additional hotspot. However, costly main
scroller passes still matched 4,944 elements and reused none: the selector
generation had changed, making inheritance reuse ineligible. This is diagnostic
evidence, not an improvement claim. A bounded, explicitly armed debug trace now
records mutation kinds and attribute names, excluding values and page text.

A separate local-map test waited fifteen seconds after opening the keyboard:
19 typing samples averaged **44.95%**, peak **102.0%**. The draft words were visually verified; punctuation fidelity was not established. Its profile still attributed 42.35% to style
recalculation and 25.62% to getBoundingClientRect-style synchronous reads.
This isolates settled typing better but is not an identical A/B comparison
against the three-second runs. Neither peaks nor ordinary typing meet the target.
At that point, the seventh streamed reply had not been tested.


## Selective attribute invalidation

A bounded on-device trace identified `data-scroll-from-end` changes on the main
scroller: set and remove each matched 4,952 elements, taking approximately 1.2
seconds. A focus-mode attribute on html also matched 5,571 elements. In contrast,
a selector-neutral inline-style pass with the root-context correction matched
1,346 and reused 3,598 elements. The traced settled draft run averaged 48.18%,
peak 102.04%; logging and page reload differences make this diagnostic only.

The next candidate indexes attribute references in all selector compounds and
nested selector lists. Changes to data-/aria- attributes on styled connected HTML
elements mark potential stylesheet subjects across the tree, including shadow
trees. Subjects without a necessary key fall back to broad invalidation; stale
indexes and other attribute types keep the existing path. Attribute-dependent
generated text is tracked independently, including pseudos and inline variables.
DOM/selector generations and mutation notifications still advance, so queries
observe live values. No site-specific attribute names are hard-coded.

Regression tests compare incremental styles against forced recomputation for
ancestor, sibling, :has, :not, :is and nth-of selectors, mixed attribute case and
classes beginning with brackets. Other cases cover generated text, a newly added
stylesheet, shadow-host attributes and unused attributes. A 1,000-element history
fixture matches just its indicator for a scroll-state attribute change. Twenty
focused native tests and ASan/UBSan/leak checks pass. The parent/direct/nested
iframe scroll-paint fixture remains green (five native process CPU samples:
5.19% mean, 21.97% peak; not an Android performance comparison).

Android arm64/API29 dependency and v2 signature checks pass. The new APK is
installed, preserving app data. Live long-session measurements are pending.


## Seventh reply and attribute-subject correction

The first selective-attribute candidate completed the seventh long reply in the
same conversation. Settled typing averaged 48.62% / 123.47% peak (19 samples);
the first 35 seconds of streaming averaged 107.67% / 201.98% peak (36 samples).
Settled idle afterward averaged 4.67% / 6.12% peak (30 samples). The streaming
profile attributed 71.38% inclusive samples to style recalculation. The target
is still unmet. Completion and response controls were visually verified.

Tracing exposed overbroad invalidation for self-only attribute selectors without
a necessary subject key: these marked the entire document. The next candidate
separates self-only references from ancestor/sibling/relational references. It
also separates inheritance invalidation from selector-cache invalidation for
precisely indexed attribute changes, permitting unaffected descendants to reuse
styles while queries and changed selector matches remain fresh. A root attribute
fixture with 1,000 children matches only the root and reuses all children.
Incremental/full-cascade cases, all 20 focused native tests and ASan/UBSan/leak
checks pass. The arm64/API29 and APK v2-signature checks pass; the candidate is
installed with app data preserved. Its live measurements are pending.


## Eighth reply and container-scoped emptiness invalidation

The self-attribute candidate completed reply 8 (storage/permissions). Settled
typing averaged 43.83% / 104.12% peak (18 samples, a different 170-character
chapter prompt); the first 35 seconds of streaming averaged 106.01% / 224.24%
peak (36 samples). The profile still attributes 72.86% inclusive samples to
style recalculation. Frame-pointer unwinding was poor (88% error callchains),
so exact attribution needs caution. Traced passes still matched 6,825 or about
4,635 elements from html. A later 31-sample idle window averaged 10.38%, peak
100%, with 585.7 MiB RSS; it included a late spike and is not steady-idle proof.
The CPU target remains unmet. Reply completion and controls were checked.

Source review found another overbroad path: relational :empty dependencies with
no rightmost subject key marked every element, discarding required containers.
The next candidate retains independent compounds/combinators in a conservative
selector with emptiness-dependent tests removed. Entire functional tests are
removed when necessary, including under negation, so both old and new matches
remain candidates. A [data-spacing] sibling fixture now recalculates only two
elements alongside 1,000 unrelated history elements. Nested/negative/nth-of
comparison cases and ASan/UBSan/leak checks pass.

Punctuation loss was also traced to KeyboardEvent.keyCode using Unicode values:
a period became 46 (Delete), and an apostrophe became 39 (ArrowRight). Native
events now map physical US-layout key codes, while synthetic event legacy
fields default to zero. A real V8 contenteditable test verifies punctuation
survives legacy navigation/Delete handlers, plus Delete and constructor fields.
It passes. The installed candidate includes both fixes; phone validation is
pending. APK dependency and v2-signature checks pass.

The empty-candidate revision passes all 23 focused native cases, including
keyboard editing and viewport tests. Android live tests are pending.


## Empty-candidate phone results (incomplete streaming validation)

Settled typing with eight completed replies averaged 40.00% / 102.91% peak
(18 samples). All periods in the new networking/offline draft were visibly
preserved. The following request ended with “This response couldn’t load.”
Its 36 post-submit samples averaged 108.13% / 181.00% peak, but this is a
failed-request diagnostic, not a successful streaming benchmark. Its profile
attributed 46.07% inclusive samples to style recalculation. A late diagnostic
was armed after the failure and missed streaming mutations. Retrying normally
in the same conversation; eight completed long replies remain confirmed.

Source review also preserved bare :host when removing an emptiness-dependent
argument, so the candidate selector can still cross the shadow boundary. A
light-DOM :empty change affecting a shadow descendant passes with ASan/UBSan
and leak checks. The currently installed APK predates this small correction.


## Ninth completed reply and child-list invalidation

The retry completed the networking/offline chapter in the same conversation;
nine completed long replies are visually confirmed. Its diagnostic streaming
window averaged 104.94% / 184.85% peak (36 samples), with bounded mutation/style
logging enabled. This is not a controlled performance comparison. Style
recalculation accounted for 44.30% inclusive profile samples. The trace no
longer shows the entire history being restyled: emptiness candidates affected
zero elements, while repeated cascades within the current reply grew from
124 to 1,512 elements. A later untouched 30-second idle window averaged 4.47% /
6.06% peak, RSS 618.4 MiB. The CPU target remains unmet.

The next source candidate coalesces child-list dependency invalidation per
style pass. Structural selector candidates retain independent containers,
remove dependent functional tests conservatively, and collapse sibling
requirements to their shared ancestor. Ordinary sibling/position dependencies
are restricted to changed parent subtrees; :has and focus-within candidates
cover the whole document. Emptiness retains its separate transition index.
New or moved subtrees force selector recalculation; unchanged siblings may
reuse inherited styles. Allocating/configuring detached nodes no longer
disables inheritance reuse for the connected document.

Twenty-eight selector scenarios compare incremental styles with a full cascade
after insertions, moves, removals, focus removal and text replacement. A
streaming fixture with 1,000 existing inline children and 1,000 history elements
recalculates two elements when adding an inline, and still propagates inherited
color changes. ASan/UBSan/leak checks pass. Native/Android validation and phone
measurements are in progress.

The child-list candidate passes all 23 focused native cases, the parent/direct/
nested iframe scroll-paint fixture, and Android dependency/signature checks.
Installed with app data preserved: `lumen-tree-dependencies-debug.apk`, matching
`tree-dependencies-symbols/libmain.so` under `build/codex-auth-20261015`.
Phone measurements are pending after conversation hydration.


## Rejected first child-list candidate; relational anchors

The first child-list candidate regressed badly on the phone: the 18-sample
queued-typing window averaged 97.46% / 104.08% peak. Input remained visibly
backlogged after the typing script ended. The profile attributes 97.04%
inclusive samples to style recalculation, 88.81% to recalc and 8.21% to tree
invalidation. The diagnostic F8 event remained queued; no mutation trace was
obtained. The browser was stopped rather than left processing the backlog.
No tenth prompt was sent. This candidate is not an acceptable phone build.

The correction anchors relational dependencies to the changed ancestors or
preceding siblings/changed-parent children when :has uses a leading sibling
relation. Separate candidates form a union when multiple compounds depend
on :has. Non-subject functional dependencies conservatively retain the broad
fallback. Local position/sibling invalidation remains separate. A new unkeyed
:has fixture catches the prior whole-document invalidation with 2,000 existing
elements and 40 incremental insertions. Additional selector comparisons cover
sibling :has, nested negation, multiple dependent compounds and nth-of :has.
Validation and phone measurements of this correction are pending.

The relational-anchor correction passes ASan/UBSan/leak checks, all 23 focused
native tests, and Android dependency/v2-signature verification. Installed as
`lumen-tree-anchored-debug.apk`, matching `tree-anchored-symbols/libmain.so`.
The rejected candidate was replaced with app data preserved. Debug F9 now
disarms the bounded F8 traces so a diagnostic can precede an unlogged test.
Phone measurements are pending after hydration.

The anchored candidate also failed the short phone probe: the trace still
marked about 4,307 elements on each change (1,423 tree candidates), with
about 1.1 seconds per cascade. F9 disarmed the trace; the app was stopped.
No new long prompt was submitted. The next source correction preserves
necessary subject keys from the original selectors, anchors positional tests
to direct children of changed parents, and carries anchors across collapsed
sibling chains. It also avoids dependency scans when only detached trees
changed, rejects local rules outside changed subtrees before key checks, and
checks anchor markers before more expensive selector conditions. Bounded F8
rule-shape diagnostics now omit attribute/declaration values, page text and
URLs. New tests cover nested positive selector keys, body-level popup updates
with an unkeyed :first-child rule, and detached fragment construction.

The detached-fragment regression initially failed: detached construction also
advanced the global :empty generation, so skipping only tree candidates was
insufficient. Tree/emptiness generations now advance only for connected
mutations; detached parent subtrees retain forced selector recalculation.
This last correction still needs validation before installation.

The corrected connected-generation revision passes the complete style
regression suite under ASan/UBSan with leak detection (1.08 seconds). The
Android build now includes this correction; phone results remain pending.

### Nested positive structural dependencies

The tree-keyed build passed 23 focused native tests (2.00 s), sanitizer
coverage, APK closure/signature validation and the scroll fixture. Its loading
trace nevertheless exposed an unkeyed `:where(div[data-radix-popper-content-wrapper]:has(> div[data-side]) [data-radix-collection-item]:not([data-fill]))`
dependency widening to every element, including html/head. This is a rejected
performance candidate; no new streaming measurement or tenth prompt was sent.

The next revision recursively preserves positive `:is`/`:where` alternatives
and their independent attributes/combinators, while still widening dependent
negative tests conservatively. Nested arrays are owned by the dependency index;
immutable stylesheet payloads remain borrowed. Added the observed selector to
the 2,000-element repeated-insertion regression plus four semantic variants
(47 selector scenarios). ASan/UBSan/leak suite passes in 1.22 s.

The positive-alternative phone probe reduced structural candidates marking
elements from ~4,300 to 17–18. Cascades still recomputed 912 nodes on ordinary
changes (and 4,307 on a coalesced selector change). Full draft 10 remained
backlogged: 19 samples, 94.65974% mean /104.04040% peak. Its complete text was
visually confirmed before Send. Reply 10 completed with answer controls visible
after its final testing-checklist heading: 36 samples, 107.89864% mean /190.90909%
peak. No diagnostic logs were enabled for these profiles. The reply profile
attributed 68.37% to style_recalc, 56.20% recalc, 11.90% structural invalidation.
This remains a rejected performance candidate, despite semantic test passes.

The next change records conservative class/id/tag/attribute keys from inserted
and removed connected subtrees, including removed keys before nodes are freed.
Simple descendant/child :has dependencies are skipped when none of their required
witness keys changed. Positional, sibling, focus, nested :has and style-attribute
cases retain conservative handling; coalesced selector changes disable filtering.
The filter is evaluated once per pass, not for every document element. Added
six semantic cases (53 total) and a thousand-element history with non-reusable
style-attribute-dependent descendants, plus real witness insertion/removal.
The initial cost assertion required revisiting history; it was corrected to
allow skipping unchanged history entirely. Latest sanitizer rerun pending.

Witness-filter revision: sanitizer PASS1.14 s; Android build PASS16.61 s; native23/23 PASS2.07 s. APK ARM64/API29 closure and v2 signature PASS, installed with session intact. Live results pending.

The witness candidate's short probe matched56/reused62 elements instead of
912. Full draft11 nevertheless averaged98.81722% (17 samples, peak105.10204%).
Its profile was87.53% style_recalc,63.66% recalc,23.43% dependency scanning.
The complete draft was visually confirmed before Send. Request11 FAILED with
“This response couldn’t load”; its36-sample104.73516% mean/201.96078% peak
is not a successful streaming benchmark. Ten replies remain completed.

The next revision partitions global versus changed-subtree candidates so
unrelated history never loops over local-only dependencies. Focus-only
dependencies are skipped when no focused node moved/was removed and no selector
change occurred. Simple :has witness filters now allow :focus/:focus-visible
(the subject itself), retaining conservative :focus-within handling. Added
nested focus semantics and focused-node removal against a thousand-element
history (55 selector scenarios total). Sanitizer PASS1.13s, Android build pending.

### Focus filtering results and live response investigation

Native 23/23 passed (2.06s), sanitizer passed (1.13s), Android build passed
(23.70s), ARM64/API29 closure and APK signature passed. Installed tree-focus.
Typing draft12: 19 samples, mean49.137763%, peak105.825243%. Draft appeared
completely and correctly. Profile: style_recalc50.37%, recalc30.87%, tree
invalidation18.21%, collect16.49%, n_rect14.18%, rendering7.9%. Short probe
matched8 elements per normal edit, versus thousands in earlier candidates.
This is not a controlled A/B; conversation length and server behavior differ.

Answer11, which failed live, was recovered complete after reload: eleven long
answers now persisted in the same conversation. Request12 also displayed
“This response couldn’t load.” Its 37-sample mean105.062150%, peak186.792453%
is not a successful-stream benchmark. No thirteenth prompt sent yet.

Added bounded opt-in debug transport counters for the next reproduction.
F7 arms120seconds, F9 disarms. Only numeric IDs, statuses, encoding category,
chunk/byte counts, duration and max main-thread queue latency are logged.
No request URL, account identifiers, headers or payloads are logged. Cause of
the failed live replies is not yet established. Target remains unmet.

### Attribute-dependent candidate filtering

Added per-attribute candidate selectors preserving independent container and
positive :is/:where/:has constraints; dependent negative/positional tests are
widened conservatively, and :host retains the shadow boundary. Eight additional
incremental-versus-full-cascade cases plus a 1,000-element history fixture pass;
composer attribute toggles match at most2 elements. Native23/23 PASS2.02s,
ASan/UBSan/leak PASS1.24s, Android build PASS18.52s, APK/signature PASS.
APK retained as lumen-attribute-candidates-debug.apk; not yet phone-tested.

Transport-only diagnostic APK installed successfully; request12 recovered
complete after reload (twelve long completed answers). Draft13 on this build
averaged53.733509%, peak102.020202%,19samples. Request13 trace in progress.
Early trace confirms gzip SSE200, with unrelated response callbacks waiting
as long as5.6seconds on the main-thread queue. This is not yet a failure cause.

Request13 completed live, with conclusion and response controls visible.
Stream mean105.811190%, peak195.833333%,36samples. Transport: gzip SSE200,
66chunks,620591decodedbytes,30.284seconds, no reported error/cancellation,
max main-thread queue delay5.617seconds. Thirteen long replies now completed.
Previous live failures did not reproduce in this run. Installing attribute
candidate APK for on-device comparison; performance target remains unmet.

Attribute candidate phone results: draft14 mean53.988742%, peak106.185567%,
19samples; complete draft correct. Reply14 completed LIVE: mean106.143834%,
peak161.224490%,36samples; gzip SSE200,64chunks,816261bytes,30.414seconds,
no transport error/cancellation, max stream queue delay6.026seconds. Fourteen
long replies complete. No material whole-interaction CPU improvement established
from attribute filtering. Typing profile: style48.97%, recalc29.02%, tree
invalidation18.67%, collect17.68%, n_rect15.25%.

Next candidate batches consecutive queued chunks for the same request, at most
256KiB per merged callback. It adds no wait, preserves response/event order, and
does not combine across requests. Real loopback HTTP test verifies complete byte
ordering, bounded batching (100queued chunks become2callbacks), immediate paced
delivery, and truncated chunked-body error after all preceding bytes. Native
PASS0.26s, ASan/UBSan/leak PASS0.29s. Android build in progress.

Stream batching Android build PASS19.37s, APK ARM64/API29 closure/signature
PASS, installed-r preserving conversation/login. Network regression3/3 PASS
0.27s; fetch/preflight4/4 PASS. Current phone rehydrating the fourteen-reply
conversation. New candidate artifact lumen-stream-batch-debug.apk and matching
stream-batch-symbols/libmain.so. No CPU result claimed for batching yet.


### Batching phone results and coalesced selector diagnosis

Draft15:20samples, mean59.571108%, peak104.123711%. Reply15 completed LIVE;
35-second profile window:36samples, mean107.729382%, peak197.058824%.
Transport gzip SSE200,40delivered batched callbacks/971589bytes/40.774seconds,
no error/cancellation, max queue10.003seconds. No CPU improvement established.
Profile:59.38% style_recalc,46.24% recalc,26.34% collect,12.87% structural
invalidation. Different answer/history lengths prevent controlled A/B claims.

Draft16:13samples, mean74.080835%, peak102.970297%; complete draft verified.
Reply16 completed LIVE with final response controls. Bounded style logging
was enabled, so this is a diagnostic run. Sixteen completed replies now in
one conversation. Trace found multiple7,000-element cascades after coalesced
selector/tree changes, and600–1,000 matches even with inheritance reuse.
An unrelated class change disables the :has witness filter globally, including
body:has(> .task-list-item-drag-ghost), unnecessarily dirtying the entire body.

Next candidate records changed class tokens and IDs before mutation/removal,
and indexes class/ID dependencies throughout each structural selector. When
all coalesced selector changes are keyed and unrelated to a candidate, its
existing witness/focus filter remains usable. Other attribute/state changes
retain conservative fallback. The inheritance reuse gate itself is unchanged.
Added1,000-element history cost checks and coalesced ancestor/witness/ID/
attribute-selector/focus semantics. Native validation in progress.

Coalesced-selector tests exposed a removal case: a previously matching ancestor
class can disappear in the same batch as a child insertion. Added lazily widened
class/ID candidates which drop that changed constraint and structural locality
when necessary, while retaining independent selector structure. Affected local
candidates become global for that pass. Native style tests PASS0.18s, sanitizer
PASS1.82s, Android PASS35.26s. APK ARM64/API29 closure and signature PASS.
Installed artifact lumen-coalesced-keys-debug.apk with coalesced-keys-symbols.
Phone rehydrating the existing sixteen-reply conversation; no phone CPU claim yet.
Native regression23/23 PASS2.81s; native parent/direct/nested scrolling fixture PASS.
After hydration,15one-second samples averaged0.333400%CPU, RSS593.9MiB.
This is a short quiet window, not evidence of active-interaction improvement.


Coalesced-key phone result: draft17 mean62.862374%, peak138.834951%,17samples;
full draft correct. Reply17 35-second window mean105.979029%, peak176.237624%,
36samples. SSE200,28batched callbacks,620268bytes,33.777seconds,no reported
error/cancellation,maxqueue5.581seconds. Final-answer inspection in progress.
Typing profile55.75%style_recalc,39.85%recalc,22.63%collect,14.94%tree invalidation.
No material active-interaction CPU improvement demonstrated by this candidate.

Next revision avoids marking an unchanged parent's computed style dirty merely
because a child was inserted/removed. Layout and descendant-style dirtiness
still propagate; actual structural selector subjects are selected by the index.
The parent's own :empty transitions still dirty it explicitly. DOM-only clients
without a style engine keep conservative behavior. Existing pending dirty flags
are preserved. Added a body-portal cost case with1000 non-reusable history styles
and coalesced class edits: insertion at most2matches, removal0matches. Existing
semantic tree/shadow/emptiness tests pass. Two old assertions requiring revisiting
unchanged history now permit skipping that work. Native style PASS0.20s,
sanitizer PASS1.34s, Android PASS16.67s, APK closure/signature PASS.
Artifact lumen-child-layout-debug.apk and child-layout-symbols; not yet installed.
Reply17 conclusion and final controls confirmed LIVE after scrolling through
the answer. Seventeen completed replies. Jump-to-bottom button was not visible
on this run; inspection used touch scrolling. The site reports files/images/data
analysis temporarily usage-limited, while text chatting remains available.
Child-layout native regression23/23 PASS2.08s; native parent/direct/nested
scrolling fixture PASS. APK installed-r preserving login and conversation.
Phone rehydrating before draft18/reply18 measurements.


Child-layout phone: short settled 15-second window mean0.398072%CPU,
RSS558.5MiB. Draft18 mean52.408202%,peak132.653061%,18samples; complete text
verified. Reply18 completed LIVE with conclusion and final controls, now18
completed long replies in one conversation. Stream35-second window mean
104.221057%,peak182.828283%,36samples. GzipSSE200,45batched callbacks,
638611bytes/34.301seconds,maxqueue8.223seconds,no error/cancellation.
Typing profile43.85%style_recalc,30.48%recalc,14.57%collect,12.57%tree invalidation.
Stream profile43.11%style_recalc,28.54%recalc,15.09%collect,14.39%tree invalidation.
Different answers/history prevent a controlled comparison. Target remains unmet.

Next experiment extends the existing versioned selector cache around structural
and emptiness invalidation. Native style PASS0.19s, sanitizer PASS1.57s,
Android PASS26.59s. Artifact lumen-invalidation-cache-debug.apk and matching
invalidation-cache-symbols retained; not installed.

Fresh bounded child-layout typing trace found a5507-element cascade: coalesced
class changes widened a .prose ancestor constraint because a separate [class]
test in the selector changed. Follow-up retains unchanged positive class/ID
constraints through an invalidation-only key predicate. Changed keys widen as
before; negative tests remain conservative. Regression cost coverage includes
the observed .prose/:where sibling/:not class-attribute pattern. Validation ongoing.

Unchanged-key revision: native style PASS0.18s, ASan/UBSan/leak PASS1.80s,
Android build PASS19.06s, APK closure/signature PASS. Artifact
lumen-unchanged-keys-debug.apk and unchanged-keys-symbols. Baseline fixed-history
draft child-layout-fixed-history-a averaged59.661807%CPU,peak101.960784%,20samples;
complete draft visually verified, cleared before installing replacement APK.
Same conversation retains18completed replies; comparison draft will be identical.

Unchanged-keys install-r PASS, native regression23/23 PASS2.06s,
parent/direct/nested scroll fixture PASS. Fixed-history baseline profile:
49.80%style_recalc,29.93%recalc,18.62%structural invalidation,15.11%collect,
6.05%layout_run. Awaiting hydration before the matching candidate draft.

Same18-reply-history, same draft candidate test unchanged-keys-fixed-history-b:
20samples,mean42.694148%,peak102.061856%. Full text correct. Baseline59.661807%
mean; this pair improved28.44%, but peak remains above50%, and reload/ad content
is a confound. Typing profile41.62%style,21.86%recalc,18.55%tree invalidation,
9.72%collect. Reply19 completed LIVE with conclusion/controls;mean106.384030%,
peak179.591837%,36samples. SSE200,gzip,42batched callbacks,719247bytes,
33.152seconds,maxqueue10.711seconds,no error/cancellation. Stream profile43.88%
style,26.80%recalc,16.86%invalidation,14.54%collect. No streaming CPU gain.

A new native cost case places1000history elements inside .prose; it reproduced
unnecessary broad invalidation (assertion failed). Indexing case-sensitive
[class~=token] and [id=value] by the relevant token/ID fixes it. Other operators
and case-insensitive variants remain conservative. Added semantic comparisons
for token/ID removal and case-insensitive forms. Native PASS0.22s, sanitizer
PASS1.80s, Android PASS17.39s; token-keys APK/symbols retained, not installed.
Next change permits keyed :has witnesses with hover/active states: independent
state changes disable the key filter, while unrelated insertions cannot change
those flags. Added state activation/deactivation/removal and history cost checks.

Hover/active-key refinement nativePASS0.19s,sanitizerPASS2.68s,AndroidPASS24.95s;
APK ARM64/API29 closure/signaturePASS. Artifact lumen-hover-keys-debug.apk with
hover-keys-symbols. Unchanged-keys baseline history19-a typing mean64.933533%,
peak103.030303%,18samples; full draft correct. Cleared and installed replacement
APK with install-r preserving login/history. After reply19, a30second idle
window on unchanged-keys averaged7.464972%,peak10%,RSS578.4MiB.

Hover-keys native regression23/23PASS2.08s,parent/direct/nested scrollingPASS,
install-rPASS. After hydration a15sample quiet window averaged0.467367%CPU,
peak1.020408%,RSS560.8MiB; not directly comparable to the old post-stream window.
Baseline history19-a profile51.84%style,27.95%recalc,22.52%tree invalidation,
15.23%collect,4.90%layout. Candidate identical draft measurement in progress.

Hover-keys same19-reply-history draft: mean39.629295%CPU,peak100.990099%,18samples,
full text verified. Baseline64.933533%, reload/ad confounds remain. Profile34.74%
style,19.31%tree invalidation (8.91%self),14.46%recalc. Reply20 completed LIVE
with conclusion and final controls. Stream mean108.031710%,peak183.505155%,
36samples. No streaming CPU gain. Now20completed long replies in one session.
Next experiment indexes active structural candidates by subject class/ID/tag;
changed-key subjects retain universal fallback. Adds2000history/1200rule cost
coverage and coalesced key/type/universal semantic comparisons.

Subject-index native stylePASS0.21s,sanitizerPASS1.61s,AndroidPASS18.84s.
APKclosure/signaturePASS,install-rPASS. Artifact lumen-subject-index-debug.apk,
subject-index-symbols. Matching baseline hover-keys-history20-a:19samples,
mean54.981299%,peak101.020408%,fulltextcorrect then cleared. Baseline profile
41.33%style,23.95%tree invalidation (11.59%self),15.80%recalc.
Reply20 transport:SSE200,gzip,30batchedcallbacks,720059bytes/28.899seconds,
maxqueue6.454seconds,noerror/cancellation. Reply20profile51.10%style,37.15%recalc,
19.27%collect,13.60%invalidation. A later30sample quiet window included a107%
spike(mean10.90%,RSS583.4MiB); do not claim stable low idle CPU.

Subject-index native regression23/23PASS2.10s; parent/direct/nested scrolling
fixturePASS. Full conversation rehydration still slow; input benchmark pending.

Subject-index identical20history draft:19samples,mean47.987059%,peak103.061224%,
fullcorrect. Baseline54.981299%; mean12.72%lower in this pair, peaks remain high.
Typing profile48.04%style,29.08%invalidation,24.68%bucket helper(13.87%self),
17.52%recalc. Relative invalidation share is still high. Reply21 stream mean
107.638836%,peak179%,36samples; completion verification pending. No material
streaming CPU improvement. Next capture bounded style trace on settled page.
