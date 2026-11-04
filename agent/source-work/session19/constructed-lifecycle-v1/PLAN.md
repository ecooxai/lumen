# Constructed stylesheet lifecycle slice

Approved by root after exact candidate1903 native218 gate release.
The frozen sheets58 cohort includes all selected stylesheet/import/media/external-load
neighbors; runtime acceptance requires no previously passing assertion or URL losses.

The construction flag is private and persists when the owner element is detached.
Regular sheet replacement must preserve its old rules: replaceSync throws NotAllowedError,
while replace returns a rejected Promise. Constructed insertRule rejects a parsed import
with SyntaxError, and replacement drops top-level imports before mirroring CSS into DOM.
Disabled constructed sheets retain the same live rule list but apply no mirror text;
reenabling restores the latest rules through the existing application path.

Keep old owner-generation detachment and Promise thenable assimilation unchanged.
No adoption-order, cross-document, native cascade, asynchronous parsing, or imported-sheet
object expansion belongs to this change. The regular-sheet replacement WPT still has an
independent imported child stylesheet getter limitation; no full URL gain is assumed.

Primary specification: https://drafts.csswg.org/cssom/#the-cssstylesheet-interface
and https://drafts.csswg.org/cssom/#the-stylesheet-interface .
