# Grid CSSOM iteration2 — GPT-6 Astra / built-in agents / Ruby MCP / Chromium

Candidate135 receives scoped engineering quality96/100 after independent source review and root approval. Whole-project readiness remains provisional19/100. This scope covers specified-value validation, shorthand expansion/serialization, same-shorthand pending replacement, and live computed-style connectivity; full CSS or WPT parity is not claimed.

Measured baseline-to135 interval: 2026-10-28T09:23:44Z to 2026-10-28T09:51:22Z; 27.6333 minutes. Earlier investigation is excluded. Structured per-candidate timings, scores and source/binary hashes: agent/progress/runtime/session2-iteration2-cssom.json. Agents: progress_and_deployment (implementation/evidence), repository_lead (build/source review/Git), standards_comparison (independent Grid3 and CSSOM standards review), root (acceptance).

## Unchanged upstream cohort

Pinned WPT revision0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb;37 ordinary Grid parsing URLs;1,172 assertions including all42 tentative intrinsic-auto-repeat assertions. A separate crash-reproducer file was not executed and is outside the declared37-URL cohort.

| Frozen browser | Passing assertions | Fully passing URLs | Harness OK |
|---|---:|---:|---:|
| Lumen128 |347/1,172|3/37|37/37|
| Experimental133 |1,058/1,172|30/37|37/37|
| Corrected135 |1,059/1,172|31/37|37/37|
| Chromium150.0.7871.114 |1,130/1,172|35/37|37/37|

Exact128→135:754 gains and42 losses. All42 losses remain in the denominator/raw reports; they are valid tentative Grid3 intrinsic-repeat constructs beyond the Grid1/2 support boundary, not generally invalid CSS.133 additionally lost a detached computed-style assertion;135 repairs it. Failed133 evidence remains retained. No global zero-regression claim.

## Controls and limitations

The original20 authored assertions remain unchanged:1283/20→13320/20. Eight appended pending/detached controls expose13322/28→13528/28. Chromium27/28 differs only on removeProperty(shorthand)'s return value: CSSOM Editor’s Draft13October2026 §6.6.1 requires returning the previous getPropertyValue. Independent standards review confirmed the assertion; it was not weakened.

Full135 CTest:119/119 PASS in42.74seconds at-j3. Exact1s tree monitor: peak249.025% CPU,493.375MiB RSS,214.352MiB PSS. Concurrent frozen WPT workloads prevent isolated speed claims; snapshots miss some short-lived processes and RSS can double-count shared pages.

An ordinary200px/four50px-item observation shows128 accepting intrinsic repeat without corresponding repetition layout.135 rejects it through CSSOM and preserves25px fallback. Raw native styles still accept it and compute min-content; native atomic validation is a separate follow-up. Probe SHA256:b563f7593e1e9c78753806a39263c1c9dd5916545d44e1ef269ab085c92928bf.

Independent review found stale pending shorthands after partial/overlapping overrides.14 unchanged observation cases establish the same issue in frozen128 and135. Values5 requires per-longhand pending provenance; Chromium serializes partial values as empty declarations while retaining internal identity and priority. Lumen's style-text representation does not yet fully model partial override/removal/native cascade behavior. This inherited limitation is explicitly retained; only same-shorthand replacement is repaired here. No broad persistent-state rewrite is included.

Remaining71 Chromium-passing identities:67 native resolved track-size assertions,3 declaration-block recomposition cases,1 native area-template computation. Broader grammar and rendering coverage remain unproven.

## Source and evidence

133 binary SHA256:8e9e3a30a3b41f2df3bdeda2376d1e3c1be71f1ffe9bcf782ce51da3613ac8ee.

135 binary SHA256:0aa08e7163a04181ef986e7a6a660c1fa445e83bce398ff261fdd50171da3b64.

Helper SHA256:af097294ab5f90865e1f186a4c3b8d5a9b0432acae5d61d7bb2346ba259d92c0.

28-case fixture SHA256:9b86ccd6d2ad5b77d312ba02f9027db8c076388d27a422bd2700bb518a333ec8. Original20-case SHA256:168111e5b391293d9baa45c2d512a0e9b616be1ee35d8ce9005462d2d41107ed.

Private evidence root: agent/validation/linux-wpt-20261028/session2/css-grid-parser.

- candidate133-comparison.json and candidate135-comparison.json retain every named gain/loss, raw-result SHA256, binary metadata and URL identity.
- candidate133-followup-complete.json and candidate135-authored-result.json retain28 authored outcomes.
- chromium-grid-cssom-followup.json and chromium-grid-removal-observation.json retain unchanged Chromium assertions and independent removal observations.
- pending-observation128.json, pending-observation135.json, chromium-pending-observation.json retain14-case review attribution.
- native135/ contains full CTest log, completion and1s resource evidence.
- baseline128-grid-intrinsic-observation/ and candidate135-grid-intrinsic-observation/ retain ordinary unsupported-feature observations.

Raw reports are in project-backed /tmp/lumen-astra-checks-20261028/css-grid-parser-session2-* paths recorded with hashes in comparison JSON. All external native WPT launches use the sealed capture guard; tests/builds share480% aggregate CPU. Source/raw publication remains withheld; live preview publishes aggregates only.

## Primary standards

- https://drafts.csswg.org/css-grid-1/#repeat-syntax
- https://drafts.csswg.org/css-grid-2/#repeat-syntax
- https://drafts.csswg.org/css-grid-3/
- https://drafts.csswg.org/cssom/#dom-cssstyledeclaration-removeproperty
- https://drafts.csswg.org/cssom/#dom-window-getcomputedstyle
- https://drafts.csswg.org/css-values-5/#arbitrary-substitution
