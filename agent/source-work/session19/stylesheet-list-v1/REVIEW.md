# StyleSheetList draft

Prepared with GPT-6 Astra Pro, built-in agents and RubyWebTerm.
Draft only: no canonical source mutation or browser qualification.

Three unchanged selected URLs have seven archived shared assertion failures,
all reporting that styleSheets.item is not a function:
StyleSheetList.html, ttwf-cssom-doc-ext-load-count.html, and
ttwf-cssom-doc-ext-load-tree-order.html under css/cssom.

## Small production changes

-02-node.js: optional explicit proxy-handler argument to liveList. All previous
callers retain exactly the existing default NodeList/HTMLCollection choice.
-06b-activate.js: one cached live StyleSheetList per Document/ShadowRoot, branded
item/length, unsignedlong conversion before reading potentially mutated items,
readonly indexed behavior, iterator, and indexed-only handler. No Array class
or HTMLCollection named lookup is used. Existing sheetFor identity and query
membership/tree order/mirror exclusion are preserved.
-08-window.js: exposes StyleSheetList through the existing interface export
table; its existing finalizer supplies the interface toStringTag.

## Invalidation evidence and limits

N.version reads the owner document's dom_version (jsnative.cc). Native
mark_impl increments that version for insert/remove/reorder/text changes;
mark_attribute increments it directly or routes to mark_impl, including rel
membership changes (src/dom/dom.c). Existing scopeSheets membership is solely
the DOM query of style/link[rel~=stylesheet], minus internal mirrors. It has
no loaded-state or sheet_version predicate. Async stylesheet data completion
therefore does not change the membership predicate, and cached entries remain
the same sheetFor objects. Internal mirror flags are installed before N.insert;
mirror insertion/removal uses native mutation paths.

This intentionally preserves existing link-load membership and CSSOM rule
loading limitations. It does not expand adopted-sheet or network loading
semantics. Tests must include holding the same list across async link insertion
and completion/error, rel changes, removal/reinsertion, reorder and shadow-root
changes to verify no new staleness.

## Required evidence

Unchanged three targets plus constructable/shadow collection neighbors.
Independent same-object/list-and-sheet identity, iterator/non-Array, readonly
indexes/length, null item versus undefined bracket, missing argument/receiver
branding, unsignedlong conversions including mutation during valueOf, no named
lookup, async insertion/removal, tree order and mirror exclusion controls.
Existing NodeList/HTMLCollection neighbors must retain behavior. No prior-pass
or harness losses. Do not mix this functional patch into the isolated sampler
or realm-performance candidate before its measurements are sealed.

## Indexed descriptor review correction

The StyleSheetList-only handler now follows WebIDL sections 3.9.1–3.9.7: supported indexed deletion fails, unsupported indexed deletion succeeds, only canonical array indices below 2^32−1 enter indexed paths, and non-array numeric-looking properties retain ordinary expando behavior. Unsupported indexed get/has preserve prototype lookup. PreventExtensions returns false so future live membership remains representable. Existing NodeList and HTMLCollection handlers are unchanged. Primary source: https://webidl.spec.whatwg.org/?ca=drs- . The prior draft/manifest/syntax receipt are retained under versions/before-indexed-descriptor-review/. Independent browser controls remain pending.

## Genuine cross-realm receiver correction

The existing native N.realmOf creation-context bridge returns a private frozen REALM hook object only for same-origin realms. A single01-events hook delegates to the originating realm's private StyleSheetList brand/map and returns its live getter.06b validates local genuine receivers first, then that private foreign hook, rejecting structural/prototype/Proxy forgeries without public markers. item validates the receiver before argument conversion, then reads live membership after conversion; length uses the same genuine receiver path. Brand add/has and private list-map get are captured intrinsics. No native bridge or shared mutable registry was added. Existing NodeList/HTMLCollection behavior stays unchanged.

The revised bases are exact iteration20 snapshot31dbb594ee8ca772c01b5b566225fd2497a445bf;08 contains the separately isolated shape batching draft. This List patch changes only its interface export line in08. If shape batching is rejected, rebase that one hunk explicitly; do not overwrite the complete candidate file. Prior reviewed drafts/bases remain under versions/before-cross-realm-review/.

## Interface-local factory correction

Source review found that sharing generic listItems would let NodeList.length accept a StyleSheetList. The final revision removes the02-node.js patch entirely and uses an interface-local private items map, cache, and proxy factory. All map/brand operations, factory Object.create/Proxy references and reflection operations are captured during prelude initialization. The same document N.version lazy membership and existing query/tree-order/mirror behavior are preserved. Indexed recognition uses local primitive operations; no shared liveIdx/liveHandler/listOf state is used. This keeps wrong-interface receivers rejected and makes global WeakMap/WeakSet method replacement unable to forge or disrupt this private collection association. The private same-origin REALM hook is retained.

Actual changes are now only01 private hook,06b interface/local collection,08 interface export. Original02 and ordinary NodeList/HTMLCollection source remain byte-for-byte unchanged. Independent wrong-interface, forged-receiver, foreign genuine receiver and global WeakMap-mutation controls are required before acceptance.

## Array append review correction

The three array append sites now use existing private apush (captured Reflect.defineProperty/CreateDataProperty semantics), preserving the original filter/map behavior under author-installed Array.prototype numeric setters. No new general helper or unrelated mutation was introduced.
