# Range family draft — source review required

Prepared with GPT-6 Astra Pro, built-in agents and RubyWebTerm.
No canonical file has been changed. The running1906 replay is unaffected.

## Proposed separate commits

1. Core native parser, native computed endpoint slots, actual CSSOM shorthand
   expansion, getters/priority/removal, and lossless declaration serialization.
   Apply style.c.patch, css.h.patch,03-element.core.patch and06b-activate.core.patch.
   Source steward owns the compatible scalar native bridge in jsnative-bridge.txt.
2. Direct start/end CSSOM validation using the shared parser, as the small
   03-element.longhand-validation.patch and06b-activate.longhand-validation.patch.

The complete .candidate files include both layers. The manifest pins bases/results.
The native API is char-pointer-only; do not include full css.h in jsnative.cc.

## Semantics

- Concrete and CSS-wide shorthand declarations create two actual longhand map
  entries. length/item therefore reflect real storage. Invalid input is parsed
  before mutation; importance is applied independently per longhand.
- Shorthand getters can compress an unnamed explicit100% end, but serialized
  attribute/rule text packs a shorthand only if reparsing preserves both stored
  values. Otherwise genuine longhands are serialized.
- A named start with omitted0% and an unnamed end requires an explicit start0%
  when joined, e.g.entry0%90%. Packing cannot cross an intervening all declaration.
- The native family parser uses CSS tokens, keeps dimension boundaries through
  escape decoding, accepts negative offsets, and only treats lexical zero as
  unitless zero. Numeric underflow cannot turn a nonzero unitless token valid.
- Native declaration application stores separate computed start/end values in
  gen_n. ACtx resolves font/viewport/container units using existing helpers.
  Inheritance copies parent computed values, initial/unset yields normal, and
  all resets the family only in the actual cascade pass, excluding unrelated
  late containment/interpolate-size projections.
- Unresolved percentage/nonlinear math retains computed relative-length tokens
  through the established grid_computed_tokens helper. No timeline percentage
  basis is invented. Nonfinite math retains valid function syntax rather than
  emitting invalid nanpx/infinitypx tokens.
- Native computed getters read actual endpoint slots and derive shorthand;
  they do not read raw inline declarations or synthesize declaration counts.

## Explicit limits / remaining gates

Pending shorthand substitutions keep the existing opaque single-key CSSOM path.
This patch does not claim complete pending-shorthand expansion, animation
sampling, timeline attachment/layout behavior, or broader animation reset-only
CSSOM architecture. Relative math outside existing native evaluators retains
an unresolved expression; the patch does not claim complete math evaluation.

Review ownership and provenance first, then an exact candidate build/startup,
native218, unchanged related range WPT plus numeric/animation neighbors and
independent range14 / storage / cascade / font / ambiguity controls. Prior-pass
identity loss or harness regression disqualifies the candidate. No fixture,
timeout, runner or expectation changes are included.

Primary grammar: https://drafts.csswg.org/scroll-animations-1/#propdef-animation-range
unchanged controls: css/css-animations/parsing/animation-range-{shorthand,start-valid,start-invalid,start-computed,end-valid,end-invalid,end-computed}.html

JavaScript syntax checks pass through node--check stdin under codex_compile.
The earlier .candidate-extension invocation failed before parsing and is retained
in terminal26703; syntax-check.json contains the corrected bounded checks.
Native C has not yet been compiled; that remains the test steward's queued gate.

## Pre-integration review correction

The initial draft passed a property-name string to animRangePart, whose actual
second parameter is boolean. Independent source-steward review caught this
before integration; the corrected call compares the property with
animation-range-end explicitly and trims each comma-list layer. Original draft review messages and tool outputs remain retained; the latest
syntax receipt is stored separately. No production result
is attributed to the defective draft.

Independent review also identified the generic ACtx root-rem default of16px.
The range branch now substitutes the final root font size only when computing
root range offsets. Font-size itself retains its existing initial-rem rules;
both alen and unresolved-token conversion receive the corrected range context.
The pre-correction draft is archived under versions/before-root-rem-review.

The new computed range slots also participate in the existing parent-sensitive
invalidation path. A gen_n pointer inequality gates comparisons of only the two
range endpoints; absent values compare as normal. This fixes explicit inherited
ranges after a parent offset change without marking all styles dynamic or
changing generic cache architecture. Require a dynamic parent-offset control.

The shorthand-join helper trims each item of both comma lists before testing
named-start ambiguity. tomSplitTop intentionally preserves whitespace; second
and later named starts must therefore receive the same explicit0% guard as the
first layer. A multilayer independent control is requested. Previous draft
saved under versions/before-list-trim-review.
