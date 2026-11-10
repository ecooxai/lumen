# Recover Lumen source checkpoint 2105

The source archive includes `PUBLIC-HISTORY.bundle`, `RESTORE-GIT.md` and `ARCHIVE-PROVENANCE.json` in addition to this tracked public tree. Follow `RESTORE-GIT.md` from the extracted archive; it identifies the public commit and tree verified by a fresh offline clone.

```sh
git clone --branch gpt-6-astra-pro_builtin-agents_rubywebterm_linux-source2105-20261110 PUBLIC-HISTORY.bundle lumen-source
git -C lumen-source fsck --full
git -C lumen-source status --porcelain
git -C lumen-source rev-parse HEAD
git -C lumen-source rev-parse 'HEAD^{tree}'
```

Compare the commit/tree with `ARCHIVE-PROVENANCE.json`, then compare the source blobs and modes with `SOURCE-MANIFEST.json`. The tested engine commit 20a7dd4b is provenance from the canonical project, not a private commit imported into this public history. This verifies source/history recovery; it does not rebuild the browser or reproduce the private validation environment. External native/V8 dependencies are documented separately and are not inside the source archive.
