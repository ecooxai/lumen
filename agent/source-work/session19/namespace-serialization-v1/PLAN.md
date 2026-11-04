# Candidate1901 namespace serialization slice

Root cause: native parse_compound discarded explicit wildcard/named namespace prefixes;
serializer omitted all prefixes and all non-standalone universals. CSSOM did not pass
stylesheet namespace declarations to native serialization. Null-namespace nsk already
existed but serializer ignored it.

Changes: retain type/universal prefix atoms in existing SimpleSel.raw; no structure growth.
Add context-aware serializer and native binding receiving private prefix/URI pairs.
CSSOM collects parsed namespace declarations privately, propagates context through nested
rule parsing, and refreshes it on insert/delete/replace/load. Same-default non-null aliases
and wildcard under absent default omit redundant prefixes; explicit null namespace emits|.
Namespace prefixes in selector-list pseudo arguments share context. Matching behavior and
native parser validity remain unchanged; this does not implement namespace-aware cascade.
Attribute namespace serialization is outside this slice.

Primary normative source:https://drafts.csswg.org/cssom/#serializing-selectors
Sections5.2(simple selectors, universal omission, namespace prefixes), retrieved2026-11-04.
Whole upstream target:css/cssom/serialize-namespaced-type-selectors.html,60assertions.
Auditor froze136URLneighbors before change; all historical PASS identities must remain.
Recommend independent extra controls for CSSStyleRule.selectorText, insert/delete namespace
context, nested groups, escaped type prefixes, null-URI namespace aliases, and same default
URI aliases. Do not count those controls as upstream WPT. No build/test performed by author.

Changed owned files:src/css/css.h,src/css/parse.c,src/js/jsnative.cc,
src/js/prelude/06b-activate.js. Environment agent exclusively owns03-element.js.
