# Deferred transition-origin research — UNINTEGRATED

This attachment preserves expensive implementation and review work. It is **not applied to candidate1913, not compiled as a native candidate, not runtime-qualified, and not an authentication/performance fix**. Do not apply the patch set directly to the final release merely because intermediate static checks passed.

## Exact provenance

Base private development commit: `fa69ebd287c91270a19dafc45d0711fe27fa7b11` (candidate1912).
Manifest SHA256: `a35b66fcfc4ef2652e1629f5211a2b817cee0b229479cb8995c783e25774520a`.
Membership receipt SHA256: `cda67ae1632149bd5e27fbc2cbeea087de5c0ddc146aa836e02568973c10ac2e`.

The six unified patches contain only project-owned source. `manifest.json` records every exact base, proposed output and patch SHA256. The original base commit is retained in canonical Git; public attachments must preserve reconstructible base source or an approved mapping without publishing private history. Candidate1913 instead uses a separate static-only importance assembly, so the native style patch is not a clean semantic addition to that later source.

## Intended producer → bridge → consumer change

The proposal separated ordinary and transition contribution strings inside the private native animation bridge. Native parsing rejected authored important declarations before applying a trusted internal transition tag. Existing Node storage remained unchanged. Native declaration assembly would put ordinary animations below important declarations and genuinely owned transitions above them, including family replays.

JavaScript privately distinguished permanent CSS-transition class from current owning-element state, using captured native WeakSet/WeakMap/WeakRef operations. Transition class precedes ordinary effects in composite order; the cascade origin follows the last actual per-property contributor, even if its sampled value equals the previous value. Public cancel/finish differ from engine disassociation. New weak-owner scans considered replacement, unrendered/disconnected state, post-style synchronization, transition-property changes and replacement eligibility. Channel-cache metadata was moved away from author-observable wrapper calls.

## Review outcome and blocking cases

Independent review checked the private two-channel signatures, declaration ownership/freeing, rejection of author-provided priority, clearing both channels, source order, default animation path, weak record retention, captured intrinsics and new lifecycle boundaries. Fragment syntax checks were retained. **The complete proposal never received runtime qualification or native build acceptance.**

The unresolved blocker is correct transition-property membership after aliases, shorthands and logical/physical mapping. CSSOM storage child lists do not prove writing-mode-sensitive membership. Retaining every same-family uncertain case can keep a disassociated transition at the newly introduced higher origin; rejecting every raw-name mismatch can cancel a still-matching transition. Examples include logical margin-inline-start versus margin, and horizontal margin-top versus margin-inline. A full native flow/property mapping must establish the distinction; string-prefix guesses are insufficient.

A separate frozen opacity→font/replay control with stylesheet opacity!important yields0.8 PASS on both prior1912 and referenceChromium. A proposed0.5 result would be a new prior-pass regression. The corrected mixed-effect control expects0.8 by the highest-contributing-effect rule; prior1912 returns0.9. An earlier mixed0.5 oracle was found incorrect and retained as diagnostic only, not rewritten into a passing target.

Root selected the bounded static-only fallback rather than extend this proposal into a wider transition-lifecycle/flow rewrite. Candidate1913 preserves the entire previous declaration assembly whenever anim_decls exists. Consequently this research must not be described as delivered transition support, nor may newly exposed wrong-origin effects be dismissed automatically as pre-existing.

## Future qualification requirements

Re-review exact reconstructed source and all private producer/bridge/consumer callers, including empty/cancellation paths and Android-compatible declarations. Prove author keyframes, priorities, prototypes and getters cannot forge class/ownership metadata. Freeze deterministic mixed-effect, publiccancel/finish, replacement/replay, detach/reinsert, shorthand/alias and logical/physical controls before execution. Retain all original baselines and corrected-oracle history. Require a new never-used candidate ID, exact-source build/native218, unchanged relevant WPT and every prior-pass identity before full acceptance.

Primary specifications: CSS Transitions2 `#animation-cascade-level` and `#animation-composite-order`; Web Animations1 `#applying-the-composited-result` and `#replacing-animations`; CSS Logical1 `#box`; CSS Fonts4 `#font-prop`. The proposal/limitations JSON files retain the exact source URLs and scope decisions.

No X/authentication evidence, profiles, credentials, downloaded Google script, private screenshots or neighboring runtime files belong in this attachment.

## Portable experimental reconstruction

Use a separate disposable copy of the public source. Verify all six `public_source_sha256_required` values in `RECONSTRUCTION.json` before changing anything. The five non-style files already match the experimental base. Apply `restore-experimental-base-style.c.patch` with `patch -p1 --fuzz=0` to restore only `src/css/style.c`, then verify every `experimental_base_sha256`. Apply the six research patches listed in `manifest.json`, and verify every `unintegrated_proposed_sha256`. This operation deliberately removes the static-only fallback in the required source snapshot from the experimental copy before adding the deferred proposal. It must never be run against the installed release or treated as an accepted fix. No private Git history is needed. The reconstructed bytes were verified in a temporary directory; they were not built or runtime-qualified.
