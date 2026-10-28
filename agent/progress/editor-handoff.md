# Editor and progress delivery handoff — GPT-6 Astra Pro built-in subagent

Updated: 2026-10-27, approximately18:10UTC. Owner: `/root/progress_delivery`. Deadline:2026-10-27T22:10:43Z. This is an active-task handoff; the98/100 whole-browser goal is not complete.

## Current acceptance boundary

Accepted core commit: `db088f4ba4e50121582e00eff403dbfea8f0ee52` on `gpt-6-astra-pro-builtin-subagent-wpt-parity-20261027`. It adds528 assertion passes with zero lost passes across23 exact upstream URLs (31,134→31,662/32,946). It excludes the parked grid candidate and the subsequent editor/image candidates.

Editor source is stable at the iteration08 lifecycle repair, included in the iteration09 shared source freeze. Acceptance and Android validation are pending the independent validator's final gate. Frozen i8 binary: `build/codex-native/lumen-astra-i8`, SHA256`82573dcc3ce0dfc52fb4a54ce91885e1f7c411188f8f0e1f16341f99ebc324b9`.

## Reproduced editing defect and fix

A programmatic range set before focus, while detached, or before switching editable controls was visible through selectionStart/End but absent from the native input caret. Native typing therefore appended `OLDMESSAGENEWMESSAGE`, and Save correctly preserved that already-wrong input value. Ordinary DOM replacement and post-Save reconciliation were independently confirmed correct in the isolated cases.

Each input/textarea now stores native anchor/focus state in an8-byte control-only extension, distinguished by its destructor. This adds no fields to every DOM node. Document.csel remains a paint cache for the focused control. Programmatic selection, native typing, mouse/keyboard caret changes and focus restoration use the same state. Value changes move that control's caret; identical assignment preserves it. Cloning, input-type transitions and form reset explicitly synchronize both the JS and native selection state. Exact UTF-16 DOM offsets and direction are retained when converting to the native codepoint caret.

Owned source sections:

- `src/dom/dom.h`: control-selection helper declarations.
- `src/dom/dom.c`: control-only extension helpers, focus cache restore, cache cleanup when a node is freed. Preserve the image owner's NF_IMG_REQUEST/attribute invalidation changes.
- `src/js/jsnative.cc`: control value and ctlSel/setCtlSel helpers near580–615; image helpers elsewhere belong to engine_lead.
- `src/app/main.c`: ctl_sel/ctl_sel_set around3740; do not mix the known testdriver defect into this editor patch.
- `src/js/prelude/04-tags.js`: selection helpers around280–315; preserve HTMLImageElement changes.
- `src/js/prelude/02-node.js`: cloneControl selection reset around380; preserve image helpers near the start.
- `src/js/prelude/04f-forms.js`: input-type and form-reset selection synchronization.
- `CMakeLists.txt`: browser_control_selection and browser_react_message_replacement test blocks immediately after browser_image_lifecycle.
- `tests/browser/control-selection.html`:37 native assertions.
- `tests/device/editbar/controlled-message.html`:4 actual bundled React16.14.0 controlled textarea/edit/Save assertions; reuses existing React files.
- `tests/browser/README.md`: description of these tests. Generic runner belongs to engine_lead.

All shared-file changes used `agent/validation/runtime/source-edit.lock`, reading the latest file before bounded replacement. Build freeze uses `agent/validation/runtime/build.lock` and the same source lock for i9. Do not overwrite whole shared files from stale copies.

## Evidence and known limits

- Original v2 native replacement fixture: preserved/candidate-before22/36, i7+i8 corrected36/36, fresh Chromium36/36. Evidence: `edit-v2-i5`, `edit-v2-chromium`, `iteration-08/edit-v2`; independent canonical confirmation under validator ownership.
- Original v3 caret/mouse/keyboard/Unicode fixture: pre-editor i6=13/19, i7+i8=19/19, fresh Chromium19/19. Actual bundled React fixture:2/4 before,4/4 after and4/4 Chromium. Evidence: `edit-regression-v3-i6`, `edit-regression-v3-chromium`, `iteration-08/edit-regression-v3`.
- The first editor candidate i7 lost43 upstream selection assertions. They were traced to cloned controls starting at the copied value's end and input-type reset hooks only changing the JS WeakMap. The event queue itself was not changed. I8 restores select-event270/270 and selection-start-end-extra10/11; the remaining textarea child-removal assertion is preexisting.
- Independent21-file selection cohort: canonical792/807 and i8=792/807. Exact name audit pending as of this handoff draft. This is a focused regression gate, not full WPT parity.
- Permanent native fixture37/37 and genuine React4/4 pass on i8. `iteration-08/native-control-selection-isolated-click` and `iteration-08/native-react-message` contain logs and1-second CPU/RSS samples.
- The initial combined permanent fixture exposed a preexisting native TESTDRIVER click-count defect. Canonical349 and i8 both failed the same2 pointer checks; exact page in Chromium passed37/37. td_cmd increments every down within500ms regardless of pointer position/button, making earlier Save clicks turn the later caret click into a triple-click. Geometry and hit target were correct. The fixture now explicitly waits600ms for its intended single click. Upstream expectations are unchanged. Full classification: `agent/progress/preexisting-testdriver-multiclick.json`. Fix time+position+button tracking as a separate follow-up only after editor acceptance.
- Android IME/device behavior is not yet independently verified. Validator is capturing i9 source into an isolated Android worktree.
- Live signed-in ChatGPT inspection was blocked by automatic approval review pending explicit private-session authorization. Do not bypass via another profile/control route. Only synthetic fixtures were used. This demonstrates the engine defect and repair, not a verified private ChatGPT session outcome.
- Browser tests use offscreen software rendering and isolated profiles. No GPU or general memory/performance superiority claim is established.

## Parked grid work

`06d-anim.js` and `style.c` were restored byte-for-byte to accepted db088f4 at17:47:19UTC. The complete patch is `agent/progress/parked-grid-iteration005.patch`, SHA256`0714d590ed527bcf0591924f177ce558e5caaaba5b9e7965c65d66976026d532`; metadata is adjacent JSON. Frozen candidate i5 SHA256`a457f28c4dfbec6a9b355a711edc78ce206c12acab19ca90ed7f3a22051976af`.

Direct two-file interpolation gate1368/1368 (+384). Broader28-URL gate2744→3504/3975 had28 lost passes:12 discrete grid-auto transitions and8 additive-composition assertions per axis. Therefore it was not accepted despite net gain. Exact failures: `agent/validation/iteration-05/grid-regression-comparison.json`. Calc-size native basis evaluation remains unsupported; do not add JS-only algebra and call the feature complete. A fresh agent should review composition/discrete semantics before reapplying the patch and independently run the broader gate.

## Progress preview and message handoff

Root owns polling now. From the project:

```sh
node agent/progress/manage.mjs poll --mark
node agent/progress/manage.mjs reply MESSAGE_ID "Reply text"
node agent/progress/manage.mjs health
node agent/progress/manage.mjs url
python3 agent/progress/archive-source.py
```

`status.json` hot reloads every2seconds. Fields: title, phase, iteration, score (null when unassessed), score_note, updated_utc, branch, checkpoint, coverage, findings[], historical{}. Root is already updating it. No restart is needed. Server is localhost4187; its key-gated HTTPS route is healthy. Keep viewer-token/runtime/messages outside git and source archives. The key is intentionally omitted from this handoff. `/src.tar.gz` is authenticated and labelled dirty candidate versus accepted commit explicitly. Source archive audit excludes .git history, profiles, keys, logs, symlinks and unrelated untracked files; archive generation includes committed authored tests after acceptance.

Surge CLI is installed but account authentication is absent. Do not invent a login or request/store passwords. Existing authenticated HTTPS preview remains usable.

## Checkpoint persistence handoff

Current Library identity: `libfile_b6b339e20594819183ccc2d6377b864f`, version2. Latest filename: `gpt-6-astra-pro-builtin-subagent-rubywebterm-lumen-wpt-iteration-005-accepted-core-checkpoint.json`. Local file and current-request helper paths are under `/workspace/scratch/2d34770a3102/` on the ChatGPT scratch filesystem, not the Ruby PC. Helpers: `progress_library_helpers_20261027/library_file_transfer.py` and companion `library_hosted_apps.py`. They were freshly downloaded from skill c0/library in this request and may be reused within this task.

Read c0/library before a new agent uses it. Replace the same identity with expected_current_version2; do not create duplicates or remove the version guard on conflict. On success, apply returned xattrs using the existing helper in the same functions.exec invocation as replace. Pass the complete returned xattrs array on stdin to `python3 .../library_file_transfer.py apply-xattrs ABSOLUTE_LOCAL_FILE libfile_b6b339e20594819183ccc2d6377b864f`. Never expose viewer keys, browser profiles, cookies or private screenshots in the checkpoint. Root requests the next accepted milestone/i10 checkpoint; include exact commits, full task/devsummary, independent counts, caveats and source provenance. Preserve prior versions.

## Remote tool adapter

Use RubyWebterm with `workspace:"/"` and a literal `cd /mnt/p3/project/browser/lumen-android/lumen-android` inside the script. The tool's workspace schema rejects the full absolute project path. Read `webterm read ID --full` for truncated output rather than rerunning a command. Use one stable task name and honest score summaries. Compilation/tests run through `python3 tools/codex_compile.py`; max6jobs globally, normally3 for candidates, shared480%CPU cap. Monitor every test at1-second cadence. Never terminate unrelated Lumen/browser sessions or inherited WPT runs.
