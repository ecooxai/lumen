# Lumen Linux — candidate 214

Work by **GPT-6 Astra Pro · Built-in agents · RubyWebterm**.

Qualification: **qualified_frozen_scopes_and_latest_nonurl640**.

## Fixed non-URL comparison

The starting and final browsers use the same 640-file selection. Reported assertion denominators remain distinct. This is a sample, not the entire WPT suite.

| Browser | Fully passing files | Passing reported assertions |
| --- | ---: | ---: |
| Lumen starting candidate 200 | 380/640 · 59.38% | 8,324/10,540 |
| Lumen candidate 214 | 401/640 · 62.66% | 8,764/10,540 |
| Chromium 156.0.8078.80 | 560/640 · 87.50% | 9,551/10,552 |


Exact starting-identity comparison: 440 gained passes; 0 prior-pass losses; 0 missing prior identities; 0 restored passing registrations. Whole-file gains/losses: 21/0.

Final harness outcomes: 624 OK, 15 TIMEOUT, 1 ERROR. Timeouts and errors remain in the denominator.


98/100 not achieved or certified; fixed non-URL samples and named focused scopes only

## Focused scopes

These scopes overlap each other and the broader sample; their counts must not be added. Each row names the candidate actually measured. Lower starting reported denominators reflect missing registrations after timeouts; restored registrations remain explicit. The CSS reference comparison also retains two encoding/name identity variants in each direction.

| Frozen whole-file scope | Latest measured Lumen | Starting Lumen | Current Chromium 156 |
| --- | --- | --- | --- |
| Parser options and related Trusted Types | candidate 214: 1,961/1,961 assertions; 18/18 whole files | 1,008/1,961 assertions; 3/18 whole files | Separate control modes |
| Ordinary script scheduling | candidate 214: 64/64 assertions; 35/35 whole files | 30/59 assertions; 13/35 whole files | 64/64 assertions; 35/35 whole files |
| Document input and document.write | candidate 214: 72/73 assertions; 71/72 whole files | 17/73 assertions; 14/72 whole files | 73/73 assertions; 72/72 whole files |
| Parser custom elements | candidate 213: 49/50 assertions; 12/13 whole files | 19/50 assertions; 3/13 whole files | 50/50 assertions; 13/13 whole files |
| Document and element creation | candidate 213: 64/64 assertions; 7/7 whole files | 24/64 assertions; 0/7 whole files | 58/64 assertions; 5/7 whole files |
| Template ownership and adoption | candidate 213: 50/50 assertions; 12/12 whole files | 26/50 assertions; 4/12 whole files | 46/50 assertions; 11/12 whole files |
| Node and Document cloning | candidate 213: 156/157 assertions; 8/9 whole files | 153/157 assertions; 6/9 whole files | 157/157 assertions; 9/9 whole files |
| CSS custom properties | candidate 214: 219/331 assertions; 17/39 whole files | 154/330 assertions; 10/39 whole files | 299/331 assertions; 35/39 whole files |
| Iframe source changes | candidate 214: 10/10 assertions; 6/6 whole files | 9/9 assertions; 5/6 whole files | 10/10 assertions; 6/6 whole files |
| DOMParser XML metadata | candidate 214: 20/20 assertions; 1/1 whole files | Existing 20/20 regressed to 4/20 in 211, then recovered | 8/20 assertions |

Native gate: **140/140**, actual exit 0, with the three dedicated URL tests deferred outside that denominator.

The CSS comparison gains 64 passing results among existing native identities and restores one after-refresh assertion, with no prior-pass loss or missing prior identity. Total passing reported assertions rise 154→219. The six iframe files now report and pass all 10 assertions. The XML file restores all 16 earlier losses and passes 20/20.

The parser-options scope includes newer draft features. Chromium 156 normal mode reports 433/1961 and 1/18 whole files; its separately labeled experimental mode reports 1218/1961 and 3/18. These feature-coverage differences do not establish overall browser superiority.

## What changed

- Shared Trusted Types parser-option handling across modern and legacy HTML sinks, with private configuration snapshots.
- Asynchronous classic-script scheduling, preparation state, ordered dynamic execution, parser/defer/load ordering and document-generation cancellation.
- Persistent document.write streams and genuine initial-document prefix parsing, including token and newline boundaries.
- Staged custom-element parser creation, native interface validation and exact-length internal is values without increasing the measured 304-byte Node structure.
- Real inert template-owner Documents, import/clone ownership, Document metadata and safe two-phase retirement.
- Token-aware CSS variable grammar and substitution, selected fallback cycle detection, computed-invalid resets, math whitespace and custom declaration block handling.
- Same-value iframe source assignments queue a real navigation while preserving srcdoc precedence and pending-load accounting.

## Remaining limitations

- The full WPT suite was not run; the 98/100 whole-browser goal remains unmet.
- URL conformance work and three dedicated native URL tests remain deferred as requested.
- Ordinary cascade origin/layer/rule rollback remains incomplete; the implemented CSS keyword correction does not provide full cascade rollback.
- The document-streaming scope retains a popup/opener context-group limitation.
- The clone scope retains inert Document.write/declarative-shadow-root behavior that is not implemented.
- A custom-element anchors expectation differs from the current existing-parent insertion rule; Chromium also differs from a related final-parent expectation.
- Initial prefix stylesheet prefetch can delay context creation, and document-written external blocking fetch remains synchronous.
- Bootstrap policy metadata is captured by a preliminary native parse, so policy arrival timing remains eager while author-visible DOM construction advances by prefixes.
- No blanket security audit or complete adversarial-security validation is claimed.
- Export source is mapped to the tested working inputs, but the public export was not independently rebuilt and is not a hermetic or portable-binary distribution.

## Reproduction and evidence

- Pinned WPT revision: 0e1c0bce3defd59a76cf2f351c7da5c5a42b26fb.
- Fixed 640 manifest SHA256: 8b5d0d491ce8b14e6ee8107d1213c10b90127a0193207d06057216bd54f3243f.
- Tested working source commit: ab44cc7f825279391a0f193e0b8a10b5ab151eb4.
- Immutable tested binary SHA256: 7ea0a9999ec521e90fdd2c25fa8485675abf4149aa7aaa7e1696b99bf8aae8ca.
- Recorded source-input manifest SHA256: 0bda0d47294b30cd710829a04d3d17b9193165440009e7b2fb8aea2d263cd956.
- Build jobs 2, native gate jobs 1, at most two browser workers, shared 4.8 CPU budget and 1-second resource monitoring.
- Exact assertion identities, harness outcomes, process exit status, source pins and owned-process/profile cleanup are retained in the local validation workspace.
- Public source includes the reviewed public Git ancestry and source mapping; private task text, operational records, profiles, inbox data, credentials and protected guard implementation are excluded.
- The public source snapshot was not independently rebuilt. It does not include prebuilt V8/wgpu dependencies and is not a hermetic or portable binary distribution.
