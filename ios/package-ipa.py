#!/usr/bin/env python3
"""Package Lumen's device app; Apple signing/export is performed by Xcode.

Profile inspection is a diagnostic preflight, not signature or entitlement
authorization. Xcode and the destination OS remain authoritative. No identity
or provisioning profile is imported, and no platform headers are modified.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import os
from pathlib import Path
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile


class PackagingError(Exception):
    pass


def require_device_app(app: Path) -> dict:
    try:
        with (app / "Info.plist").open("rb") as source:
            info = plistlib.load(source)
    except (OSError, plistlib.InvalidFileException, ValueError) as exc:
        raise PackagingError("Cannot read the app's Info.plist.") from exc
    if not isinstance(info, dict) or info.get("CFBundleSupportedPlatforms") != ["iPhoneOS"]:
        raise PackagingError("IPA packaging requires a device app built with PLAT=ios, not a simulator or Catalyst app.")
    if info.get("CFBundleExecutable") != "Lumen" or not isinstance(info.get("CFBundleIdentifier"), str):
        raise PackagingError("Expected the Lumen executable and a bundle identifier in Info.plist.")
    families = info.get("UIDeviceFamily")
    if not isinstance(families, list) or 1 not in families or 2 not in families:
        raise PackagingError("The device app must support both iPhone and iPad (UIDeviceFamily 1,2).")
    # Read only load commands, never the large executable body. LC_BUILD_VERSION
    # distinguishes arm64 iOS from arm64 Simulator/Mac Catalyst binaries.
    try:
        with (app / "Lumen").open("rb") as source:
            head = source.read(32)
            if len(head) != 32:
                raise PackagingError("The Lumen executable has no valid Mach-O header.")
            magic, cpu, _, filetype, count, size, _, _ = struct.unpack("<8I", head)
            if magic != 0xFEEDFACF or cpu != 0x0100000C or filetype != 2:
                raise PackagingError("Expected the arm64 device Mach-O executable produced by PLAT=ios.")
            if count > size // 8 or size > os.fstat(source.fileno()).st_size - 32:
                raise PackagingError("The executable's load-command bounds are invalid.")
            platform = None
            left = size
            for _ in range(count):
                command = source.read(8)
                if len(command) != 8:
                    raise PackagingError("The executable has a truncated load command.")
                kind, length = struct.unpack("<2I", command)
                if length < 8 or length > left:
                    raise PackagingError("The executable has an invalid load-command size.")
                if kind == 0x32:  # LC_BUILD_VERSION
                    if length < 24 or platform is not None:
                        raise PackagingError("The executable has an invalid build-version command.")
                    platform = struct.unpack("<I", source.read(4))[0]
                    source.seek(length - 12, 1)
                else:
                    source.seek(length - 8, 1)
                left -= length
            if platform != 2:  # PLATFORM_IOS
                raise PackagingError("The executable is not an iOS device binary; simulator and Catalyst IPAs are unsupported.")
    except (OSError, struct.error) as exc:
        raise PackagingError("Cannot read the device executable's platform metadata.") from exc
    return info


def select_identity(output: str, requested: str) -> str:
    valid = re.findall(r'^\s*\d+\)\s+([0-9A-Fa-f]{40})\s+"([^"\n]+)"\s*$', output, re.MULTILINE)
    if not valid:
        raise PackagingError("No valid Apple code-signing identities are available. Create or import one with Xcode first.")
    fingerprint = requested.upper() if re.fullmatch(r"[0-9A-Fa-f]{40}", requested) else None
    matches = {digest.upper() for digest, name in valid if digest.upper() == fingerprint or name == requested}
    if not matches:
        raise PackagingError("The requested signing identity is not in 'security find-identity -v -p codesigning'.")
    if len(matches) != 1:
        raise PackagingError("That identity name is ambiguous; select its 40-character SHA-1 fingerprint.")
    return matches.pop()


def signing_plan(profile: dict, bundle_id: str, certificate: str, *, jit: bool = False,
                 device_udid: str = "", now: datetime | None = None) -> dict:
    if not isinstance(profile, dict) or not isinstance(profile.get("Platform"), list) or "iOS" not in profile["Platform"]:
        raise PackagingError("The supplied provisioning profile is not an iOS profile.")
    expiry = profile.get("ExpirationDate")
    if not isinstance(expiry, datetime):
        raise PackagingError("The provisioning profile has no readable expiration date.")
    expiry = expiry.replace(tzinfo=timezone.utc) if expiry.tzinfo is None else expiry.astimezone(timezone.utc)
    now = now or datetime.now(timezone.utc)
    now = now.replace(tzinfo=timezone.utc) if now.tzinfo is None else now
    if expiry <= now:
        raise PackagingError("The provisioning profile has expired; renew it in the Apple Developer account.")
    teams = profile.get("TeamIdentifier")
    if not isinstance(teams, list) or len(teams) != 1 or not isinstance(teams[0], str) or not teams[0]:
        raise PackagingError("The profile must identify one development team.")
    team = teams[0]
    entitlements = profile.get("Entitlements")
    if not isinstance(entitlements, dict):
        raise PackagingError("The profile has no readable entitlements.")
    if entitlements.get("com.apple.developer.team-identifier", team) != team:
        raise PackagingError("The profile's team entitlement does not match its TeamIdentifier.")
    app_id = entitlements.get("application-identifier", "")
    if not isinstance(app_id, str):
        raise PackagingError("The profile has no readable application identifier.")
    prefix, dot, pattern = app_id.partition(".")
    prefixes = profile.get("ApplicationIdentifierPrefix")
    if not dot or not isinstance(prefixes, list) or prefix not in prefixes:
        raise PackagingError("The profile's application identifier prefix is inconsistent.")
    if "*" not in pattern:
        matches = pattern == bundle_id
    elif pattern == "*":
        matches = True
    else:
        matches = pattern.count("*") == 1 and pattern.endswith(".*") and bundle_id.startswith(pattern[:-1])
    if not matches:
        raise PackagingError(f"The provisioning profile does not authorize the bundle identifier {bundle_id}.")
    certificates = profile.get("DeveloperCertificates", [])
    if not isinstance(certificates, list):
        raise PackagingError("The profile has no readable signing certificate list.")
    allowed = {hashlib.sha1(der).hexdigest().upper() for der in certificates if isinstance(der, bytes)}
    if certificate.upper() not in allowed:
        raise PackagingError("The selected signing certificate is not included in this provisioning profile.")
    devices = profile.get("ProvisionedDevices", [])
    if not isinstance(devices, list) or any(not isinstance(device, str) for device in devices):
        raise PackagingError("The profile has no readable provisioned device list.")
    enterprise = profile.get("ProvisionsAllDevices") is True
    if device_udid and not enterprise and device_udid not in devices:
        raise PackagingError("The requested device is absent from the profile. For a Mac, register its Provisioning UDID in the Apple Developer account.")
    debug = entitlements.get("get-task-allow") is True
    if jit and not debug:
        raise PackagingError("JIT=1 requires a development profile authorizing get-task-allow; use JIT=0 for distribution.")
    if debug and devices:
        method = "debugging"
    elif enterprise:
        method = "enterprise"
    elif devices:
        method = "release-testing"
    else:
        raise PackagingError("This profile is not for direct device installation. Use Xcode's App Store distribution workflow instead.")
    uuid = profile.get("UUID")
    if not isinstance(uuid, str) or not uuid:
        raise PackagingError("The provisioning profile has no UUID for Xcode to select.")
    return {"team": team, "uuid": uuid, "certificate": certificate.upper(), "method": method,
            "bundle_id": bundle_id, "expires": expiry.isoformat()}


def command_output(command: list[str]) -> bytes:
    try:
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise PackagingError(f"Could not run {command[0]}; use the macOS/Xcode development environment.") from exc
    if result.returncode:
        # In particular, never forward a decoded profile/certificate payload.
        raise PackagingError(f"{command[0]} could not inspect signing metadata (exit {result.returncode}).")
    return result.stdout


def export_options(plan: dict) -> dict:
    return {"method": plan["method"], "destination": "export", "teamID": plan["team"],
            "signingStyle": "manual", "signingCertificate": plan["certificate"],
            "provisioningProfiles": {plan["bundle_id"]: plan["uuid"]},
            "thinning": "<none>", "manageAppVersionAndBuildNumber": False}


def atomic_copy(source: Path, output: Path) -> None:
    fd, temporary = tempfile.mkstemp(prefix="." + output.name + ".", dir=output.parent)
    try:
        with os.fdopen(fd, "wb") as target, source.open("rb") as original:
            shutil.copyfileobj(original, target, 1024 * 1024)
        os.replace(temporary, output)
    finally:
        Path(temporary).unlink(missing_ok=True)


def package_local(app: Path, output: Path) -> None:
    fd, temporary = tempfile.mkstemp(prefix="." + output.name + ".", dir=output.parent)
    os.close(fd)
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for source in sorted(app.rglob("*")):
                if source.is_symlink():
                    raise PackagingError("Unexpected symlink in the flat device app; refusing to copy an external resource.")
                if source.is_file():
                    archive.write(source, "Payload/Lumen.app/" + source.relative_to(app).as_posix())
        os.replace(temporary, output)
    finally:
        Path(temporary).unlink(missing_ok=True)


def run_xcode(command: list[str], log: Path, stage: str) -> None:
    print(f"Xcode {stage}: {log}", flush=True)
    with log.open("wb") as output:
        try:
            result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, check=False)
        except OSError as exc:
            raise PackagingError("xcodebuild is unavailable; select the full Xcode installation.") from exc
    if result.returncode:
        raise PackagingError(f"Xcode {stage} failed (exit {result.returncode}); see {log}. Import the matching profile into Xcode before retrying.")


def package_signed(app: Path, output: Path, plan: dict, *, jit: bool) -> None:
    ios = Path(__file__).resolve().parent
    project = ios / "signing" / "Lumen.xcodeproj"
    logs = output.parent / "signing"
    logs.mkdir(exist_ok=True)
    options = logs / (output.stem + ".ExportOptions.plist")
    with options.open("wb") as target:
        plistlib.dump(export_options(plan), target)
    # Temporary archives/DerivedData cannot accumulate across repeated exports;
    # retain the complete Xcode logs and export configuration for review.
    with tempfile.TemporaryDirectory(prefix=".lumen-signing-", dir=output.parent) as temporary:
        work = Path(temporary)
        archive = work / "Lumen.xcarchive"
        command = ["xcodebuild", "-project", str(project), "-scheme", "Lumen", "-configuration", "Release",
                   "-destination", "generic/platform=iOS", "-jobs", "1", "-derivedDataPath", str(work / "DerivedData"),
                   "-archivePath", str(archive), "LUMEN_PREBUILT_APP=" + str(app),
                   "CODE_SIGN_STYLE=Manual", "CODE_SIGNING_ALLOWED=YES", "CODE_SIGNING_REQUIRED=YES",
                   "PRODUCT_BUNDLE_IDENTIFIER=" + plan["bundle_id"], "DEVELOPMENT_TEAM=" + plan["team"],
                   "CODE_SIGN_IDENTITY=" + plan["certificate"], "PROVISIONING_PROFILE_SPECIFIER=" + plan["uuid"],
                   "CODE_SIGN_ENTITLEMENTS=" + (str(ios / "jit-ios.entitlements") if jit else ""), "archive"]
        run_xcode(command, logs / (output.stem + ".archive.log"), "archive")
        exported = work / "export"
        run_xcode(["xcodebuild", "-exportArchive", "-archivePath", str(archive), "-exportPath", str(exported),
                   "-exportOptionsPlist", str(options)], logs / (output.stem + ".export.log"), "export")
        packages = list(exported.glob("*.ipa"))
        if len(packages) != 1:
            raise PackagingError("Xcode export did not produce exactly one IPA; the previous output was retained.")
        atomic_copy(packages[0], output)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--identity", default=os.environ.get("IOS_SIGNING_IDENTITY", ""))
    parser.add_argument("--profile", default=os.environ.get("IOS_PROVISIONING_PROFILE", ""))
    parser.add_argument("--device-udid", default=os.environ.get("IOS_DEVICE_UDID", ""))
    parser.add_argument("--jit", action="store_true")
    parser.add_argument("--check-only", action="store_true", help="Validate metadata without packaging, signing or exporting.")
    args = parser.parse_args(argv)
    try:
        if bool(args.identity) != bool(args.profile):
            raise PackagingError("Supply both IOS_SIGNING_IDENTITY and IOS_PROVISIONING_PROFILE, or neither for a local unprovisioned IPA.")
        app, output = args.app.expanduser().resolve(), args.output.expanduser().resolve()
        if output == app or output.is_relative_to(app):
            raise PackagingError("The IPA output must be outside the input app bundle.")
        info = require_device_app(app)
        plan = None
        if args.identity:
            identity = select_identity(command_output(["security", "find-identity", "-v", "-p", "codesigning"]).decode("utf-8", "replace"), args.identity)
            decoded = command_output(["security", "cms", "-D", "-i", str(Path(args.profile).expanduser().resolve())])
            try:
                profile = plistlib.loads(decoded)
            except (plistlib.InvalidFileException, ValueError) as exc:
                raise PackagingError("security cms did not decode a valid provisioning profile.") from exc
            plan = signing_plan(profile, info["CFBundleIdentifier"], identity, jit=args.jit, device_udid=args.device_udid)
            print(f"Signing preflight: team {plan['team']}, method {plan['method']}, profile expires {plan['expires']}.")
        elif args.device_udid:
            raise PackagingError("Device membership requires an Apple signing identity and provisioning profile.")
        if args.check_only:
            print("Metadata preflight passed; Xcode signing/export and device installation were not run.")
            return 0
        output.parent.mkdir(parents=True, exist_ok=True)
        with (output.parent / ".lumen-ipa.lock").open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            if plan:
                package_signed(app, output, plan, jit=args.jit)
                print(f"Created Xcode-signed universal device IPA: {output}")
                print("Installation and runtime behavior still require validation on the provisioned iPhone/iPad/Mac.")
            else:
                package_local(app, output)
                print(f"Created local, unprovisioned IPA: {output}")
                print("This package is not Apple-provisioned for device/Mac installation. Set IOS_SIGNING_IDENTITY and IOS_PROVISIONING_PROFILE for Xcode export.")
        return 0
    except (PackagingError, OSError) as exc:
        print(f"IPA packaging: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
