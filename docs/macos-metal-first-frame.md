# macOS Metal first frame

The browser uses a project-local build of the existing wgpu-native C API when
`build-deps/wgpu/macos/manifest.json` exists. `WGPU_PREFIX` remains an explicit
Makefile override. The builder does not modify Homebrew or the iOS prebuilt
archives.

## Why this backport exists

wgpu's macOS occlusion workaround can return `Occluded` before a newly created
window has ever presented. AppKit may not set its visible occlusion bit until
that first real drawable is presented. Lume's retained-damage retry prevents a
busy loop, but cannot guarantee that this initial dependency cycle resolves.
The observed native status was the documented extension value `0x30001`.

The fix is the narrowly adapted upstream commit
[05ae83af5ed55e826b91ea339f4210d586dfa337](https://github.com/gfx-rs/wgpu/commit/05ae83af5ed55e826b91ea339f4210d586dfa337),
merged in [wgpu PR 10302](https://github.com/gfx-rs/wgpu/pull/10302). Each surface
remembers whether its host window has actually been visible. It may acquire a
real drawable for initial presentation; after observing a visible window,
subsequent genuine occlusion still stops acquisition. No status is treated as
success, and the app still retains damage and backs off after a failed acquire.

## Reproducible build

```sh
python3 tools/build-wgpu.py --jobs 2
make -j2
```

The builder pins wgpu-native `v29.0.1.1` at
`6aed50955d934ac36049ba8d002034841633ae02`, its original Cargo.lock, and WebGPU C
headers at `673658bc2bd70ec39fc55ebe6bb0173cf6d0a603`. That original lock selects
wgpu-hal/core/naga `29.0.3`. The HAL crate archive is verified against its locked
SHA-256 before the patch is applied to a separate prepared source tree. Cargo
builds with `--locked`; unrelated dependency versions and upstream default
features are preserved. The backport adapts the newer upstream atomic import to
29.0.3's existing `core::sync::atomic` and existing native layer traversal.

The original checkout remains clean. A source manifest detects edits to a
prepared build tree; an installed manifest records hashes, target, compiler,
patch commit, crate checksum and exact lockfiles. Builds use at most the
requested Cargo jobs. Both the shared and static libraries remain in the
project. The shared library receives a normal local development signature.

## Validation boundaries

A successful shader readback does not establish window presentation. Acceptance
requires actual owned-window compositor captures showing the browser and the
changing canvas, with successful surface acquisitions/submissions. Repeat the
startup with `LUMEN_MAC_VISIBILITY=0`, since diagnostic reads can change timing.
After the window has become visible, use normal window hide/recover actions to
check observed occlusion, bounded idle work and restored visible pixels.

`tools/mkdevapp.py` creates a local development bundle; its exact project-local
library path must remain available. A distributable release must embed and sign
its dependencies through the normal packaging flow. This macOS-only dependency
patch does not establish iOS-on-Mac signing eligibility or device capture support.

## Observed validation on this Mac

The pinned dependency build and fresh browser relink completed successfully.
Actual native tests passed 41 presenter assertions, 31 retry assertions and
33 WebGPU/garbage-collection checks; 29 real native data-channel/resource
checks remained green. Two separate normal development-bundle launches with
the visibility diagnostic disabled presented browser chrome and the correct
green then magenta canvas in owned-window compositor captures.

A background hide-and-unhide control retained the earlier visible frame and
reported `Occluded`. In the subsequent controlled
recovery, normal activation made the owned application active and frontmost;
the next genuine submission displayed the magenta phase, and surface submits
increased from six to seven. This supports normal foreground recovery, not a
claim that background unhide alone establishes visible recovery. The first unsuccessful
background control is retained in the independent verification evidence.

These are functional presentation observations on the test Mac. Performance
comparisons, physical devices and distributable signing are separate gates.
