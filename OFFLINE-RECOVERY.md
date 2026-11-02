# Recover Lumen source checkpoint 1703

The source archive includes `PUBLIC-HISTORY.bundle`, `RESTORE-GIT.md` and `ARCHIVE-PROVENANCE.json` in addition to this tracked public tree. Follow `RESTORE-GIT.md` from the extracted archive. It identifies the exact public commit and tree checked by a fresh offline clone.

```sh
git clone --branch gpt-6-astra-pro-builtin-agents-rubywebterm-linux-source1703-20261102 PUBLIC-HISTORY.bundle lumen-source
git -C lumen-source fsck --full
git -C lumen-source status --porcelain
git -C lumen-source rev-parse HEAD
git -C lumen-source rev-parse 'HEAD^{tree}'
```

Compare the resulting commit and tree with `ARCHIVE-PROVENANCE.json`. This verifies recovery of public source and history. It does not rebuild the browser or reproduce the private validation environment.
