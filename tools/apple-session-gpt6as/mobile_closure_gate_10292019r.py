#!/usr/bin/env python3
"""Read-only, bounded Apple dependency and signing counts; no identities emitted."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import os
import re
import subprocess
import time

ROOT = Path("/Users/devin/repos/lumen")
HOME_DIR = Path.home()
OUT = ROOT / "out/10292019r-apple/mobile-closure-gate"
START = time.monotonic()


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def main():
    if Path.cwd().resolve() != ROOT or os.uname().sysname != "Darwin":
        raise SystemExit("Canonical native Mac repository required")
    OUT.mkdir(parents=True, exist_ok=False)
    report = {
        "startedUTC": datetime.now(timezone.utc).isoformat(),
        "scope": "Read-only expected mobile dependency closures, historical device paths, and accessible signing counts",
        "signing": {}, "dependencies": [], "v8": [], "historicalDevices": [],
        "noDeviceOrIPAAcceptance": True,
    }
    # Capture only in memory, report counts; names, hashes and certificate details
    # from security are never printed or written to the receipt.
    begun = time.monotonic()
    try:
        result = subprocess.run(["/usr/bin/security", "find-identity", "-v", "-p", "codesigning"],
                                capture_output=True, text=True, timeout=12)
        match = re.search(r"(\d+) valid identities found", result.stdout)
        report["signing"] = {
            "command": "security find-identity -v -p codesigning",
            "exitCode": result.returncode,
            "accessibleValidIdentityCount": int(match.group(1)) if match and result.returncode == 0 else None,
            "durationSeconds": round(time.monotonic() - begun, 3),
            "scope": "Current user's accessible default keychain search list; no private keys read/exported",
        }
    except subprocess.TimeoutExpired:
        report["signing"] = {"error": "find-identity timed out after 12 seconds", "accessibleValidIdentityCount": None}
    profile_roots = [
        HOME_DIR / "Library/MobileDevice/Provisioning Profiles",
        HOME_DIR / "Library/Developer/Xcode/UserData/Provisioning Profiles",
    ]
    report["signing"]["profileLocations"] = []
    for directory in profile_roots:
        row = {"path": str(directory), "exists": directory.is_dir(), "profileCount": 0, "readableProfileCount": 0}
        if directory.is_dir():
            try:
                profiles = [p for p in directory.iterdir() if p.is_file() and p.suffix in (".mobileprovision", ".provisionprofile")]
                row["profileCount"] = len(profiles)
                row["readableProfileCount"] = sum(os.access(p, os.R_OK) for p in profiles)
            except OSError as error:
                row["error"] = type(error).__name__
        report["signing"]["profileLocations"].append(row)
    headers = ["SDL3/SDL.h", "webgpu/webgpu.h", "openssl/ssl.h", "ft2build.h",
               "harfbuzz/hb.h", "png.h", "jpeglib.h", "webp/decode.h", "gif_lib.h",
               "libavcodec/avcodec.h", "dav1d/dav1d.h"]
    archives = ["libSDL3.a", "libwgpu_native.a", "libssl.a", "libcrypto.a", "libbrotlidec.a",
                "libbrotlicommon.a", "libzstd.a", "libfreetype.a", "libharfbuzz.a", "libpng.a",
                "libjpeg.a", "libwebp.a", "libwebpdemux.a", "libsharpyuv.a", "libgif.a",
                "libavcodec.a", "libavformat.a", "libavutil.a", "libswscale.a", "libswresample.a", "libdav1d.a"]
    for platform in ("ios", "sim", "cat"):
        for base in (ROOT / "build-apple-toolchain" / platform, HOME_DIR / "ios-deps" / platform):
            report["dependencies"].append({
                "platform": platform, "prefix": str(base), "exists": base.is_dir(),
                "headersPresent": [name for name in headers if (base / "include" / name).is_file()
                                   or (name == "ft2build.h" and (base / "include/freetype2/ft2build.h").is_file())],
                "archivesPresent": [name for name in archives if (base / "lib" / name).is_file()],
                "archivesMissing": [name for name in archives if not (base / "lib" / name).is_file()],
                "platformABIValidated": False,
            })
        rtc = ROOT / "build-deps/rtc" / platform
        wt = ROOT / "build-deps/webtransport" / platform
        for kind, base, files in [
            ("RTC", rtc, ["include/rtc/rtc.h", "lib/libdatachannel.a", "lib/libjuice.a", "lib/libusrsctp.a", "manifest.json"]),
            ("WebTransport", wt, ["include/lumen_wt.h", "lib/liblumen_webtransport_native.a", "manifest.json"]),
        ]:
            report["dependencies"].append({"platform": platform, "kind": kind, "prefix": str(base),
                "filesPresent": [name for name in files if (base / name).is_file()],
                "filesMissing": [name for name in files if not (base / name).is_file()],
                "platformABIValidated": False})
    for base in (ROOT / "build-apple-toolchain/v8", HOME_DIR / "src/v8ios/v8"):
        version = {}
        header = base / "include/v8-version.h"
        if header.is_file():
            text = header.read_text(errors="replace")
            version = {name: int(value) for name, value in re.findall(
                r"^#define\s+(V8_(?:MAJOR_VERSION|MINOR_VERSION|BUILD_NUMBER|PATCH_LEVEL))\s+(\d+)", text, re.M)}
        for platform in ("ios-device", "ios-simulator", "catalyst"):
            target = base / "out" / platform
            monolith = target / "obj/libv8_monolith.a"
            args = target / "args.gn"
            report["v8"].append({
                "prefix": str(base), "target": platform, "publicHeadersExist": (base / "include/v8.h").is_file(),
                "headerVersion": version, "requiredVersion": "15.6.75.16",
                "monolithExists": monolith.is_file(), "monolithBytes": monolith.stat().st_size if monolith.is_file() else None,
                "monolithSHA256": digest(monolith) if monolith.is_file() else None,
                "ninjaClosureExists": (target / "obj/v8_monolith.ninja").is_file(),
                "buildArgsExist": args.is_file(),
                "buildArgsSHA256": digest(args) if args.is_file() else None,
                "platformABIValidated": False,
            })
    for udid in ("A77F68E4-972B-472B-80B8-F65E71D361E5", "3DD498EB-4464-4ED7-9CD3-7667889DD1F6"):
        device = HOME_DIR / "Library/Developer/CoreSimulator/Devices" / udid
        report["historicalDevices"].append({"udid": udid, "devicePlistExists": (device / "device.plist").is_file(),
            "appBundleDirectoryExists": (device / "data/Containers/Bundle/Application").is_dir(),
            "legacyApplicationsDirectoryExists": (device / "data/Applications").is_dir()})
    report["requiredBuildWork"] = [
        "Restore compatible arm64 iOS and arm64 iOS Simulator V8 15.6.75.16 headers, archives and its Rust/Temporal link closure, preserving current jitless Wasm/DrumBrake support.",
        "Restore each target's SDL3, wgpu, graphics/codecs, fonts, TLS/compression, RTC and WebTransport dependency closures with provenance; native macOS dylibs are not iOS archives.",
        "Repair the observed zero-byte Xcode vendor resource through a genuine Xcode installation; direct SDK syntax checks alone do not qualify archive/export tooling.",
        "Validate available Apple signing identity/profile matching before Xcode archive/export; Simulator runs are separate from one signed device IPA on iPhone/iPad/iOS App on Mac.",
    ]
    report["commandsAfterDependenciesAndToolchainRestored"] = [
        "make -j3 PLAT=sim MODE=release RTC=1 WT=1 IOSDEPS=$PWD/build-apple-toolchain/sim V8DIR=$PWD/build-apple-toolchain/v8 V8OUT=$PWD/build-apple-toolchain/v8/out/ios-simulator app",
        "make -j3 PLAT=ios MODE=release RTC=1 WT=1 IOSDEPS=$PWD/build-apple-toolchain/ios V8DIR=$PWD/build-apple-toolchain/v8 V8OUT=$PWD/build-apple-toolchain/v8/out/ios-device app",
        "python3 ios/package-ipa.py --app build-ios/Lumen.app --output out/Lumen-10.292019r-device.ipa --identity <matching-identity> --profile <matching-profile> --check-only",
        "python3 ios/package-ipa.py --app build-ios/Lumen.app --output out/Lumen-10.292019r-device.ipa --identity <matching-identity> --profile <matching-profile>",
    ]
    report["finishedUTC"] = datetime.now(timezone.utc).isoformat()
    report["durationSeconds"] = round(time.monotonic() - START, 3)
    (OUT / "receipt.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"receipt": str((OUT / "receipt.json").relative_to(ROOT)),
        "validAccessibleSigningIdentities": report["signing"].get("accessibleValidIdentityCount"),
        "profilesFound": sum(row["profileCount"] for row in report["signing"]["profileLocations"]),
        "v8MonolithsFound": sum(row["monolithExists"] for row in report["v8"]),
        "mobileStaticArchivesFound": sum(len(row.get("archivesPresent", [])) for row in report["dependencies"]),
        "durationSeconds": report["durationSeconds"]}), flush=True)


if __name__ == "__main__":
    main()
