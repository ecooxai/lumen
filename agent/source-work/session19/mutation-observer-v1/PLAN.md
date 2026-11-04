# MutationObserver registration and microtask correction

Normative source:https://dom.spec.whatwg.org/#mutation-observers, retrieved2026-11-04.
Invariants:one mutation record per interested observer, with oldValue retained when any
matching registration requests it; no mutation microtask for unrelated unobserved work;
transient additions queue cleanup even without a record; every transient points directly
to its original non-transient registration and is deduplicated by source/removed node.
Re-observe removes all transient registrations descending from the original source.

Only src/js/prelude/01-events.js notify() changes. Callback delivery implementation,
creation ordering, exception realms, parser-originated notification, and slotchange
scheduling are unchanged. No changes to upstream tests, expectations, skips or timeouts.
The independent frozen23URLobserver/slot neighbor cohort must retain all158oldassertions;
five known assertion gains across three files are expected but not claimed until measured.
Existing full1248URLregression gate remains required before broad qualification.
