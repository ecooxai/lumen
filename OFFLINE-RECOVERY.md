# Offline recovery of the reviewed Lumen source

This package contains the tested source plus a self-contained public-history Git bundle. Private task history, profiles, credentials, dependency caches and the private guarded WPT machinery are excluded.

After extracting `src.tar.gz`, enter `lumen-linux-source234` and recover the repository:

```sh
git clone ./gpt-6-astra-pro-builtin-rubywebterm-lumen-linux-source-i234.bundle ../lumen-source234-recovered
git -C ../lumen-source234-recovered fsck --full
git -C ../lumen-source234-recovered status --porcelain
git -C ../lumen-source234-recovered rev-parse HEAD
git -C ../lumen-source234-recovered rev-parse HEAD^{tree}
```

Compare the final two values and bundle checksum with the separately packaged `PUBLIC-GIT-RECOVERY.json` and `SHA256SUMS`. The checked-out branch is `gpt-6-astra-pro-builtin-rubywebterm-linux-containment-lifecycle-source234-20261031`. The delivery receipt records an actual fresh offline restore and exact tree/blob/mode checks.

The public Git commit is a curated export of tested working source `206bb89d161d91278c56899a3f664a99480374f8`; those commit identifiers are intentionally different. `SOURCE-MANIFEST.json` identifies all 650 recorded build inputs and additional reviewed files. This is not an independent or hermetic rebuild.

Build and runtime prerequisites, the 147 selected native tests, the three deferred URL names and the exact development-executable limitations are documented in `docs/gpt-6-astra-pro-builtin-rubywebterm-linux-build.md`. External libraries are not bundled. The unchanged WPT selection is reported with its revision and denominators; the private capture/guard environment is not part of this source package.
