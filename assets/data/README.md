# Public Suffix List

`public_suffix_list.dat` is a pinned upstream Public Suffix List, including ICANN and PRIVATE rules, used only for BroadcastChannel storage-site calculation. The exact upstream version, commit, retrieval URL and UTC time, byte count, and SHA-256 digest are recorded in `public_suffix_list.metadata.json`. The complete MPL-2.0 license is retained in `public_suffix_list.LICENSE`.

The build verifies the list and license digests, embeds the UTF-8 resource in the native binary, and parses it once with libpsl. Linux reads the embedded bytes directly from memory. Other desktop platforms with libpsl use an unlinked temporary stream. There are no runtime network updates or paths back into the build tree. Builds without a usable libpsl context conservatively partition by the complete canonical host; they do not guess registrable domains.

To update this dependency, download the official list from https://publicsuffix.org/list/public_suffix_list.dat, retain its license, update the version/commit/retrieval metadata and digests, and run the native `broadcast_site` gate plus the paired browser partition fixtures. Commit the list, license, metadata, and validation evidence together. A different PSL revision from Chromium is a documented compatibility variable, not proof of exact browser parity.
