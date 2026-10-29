"""Signing metadata and publication tests; all certificates/UDIDs are inert fixtures.

No Apple identity is created, imported, or used. The Xcode process boundary is
mocked here; native archive/export and device install require separate gates.
"""
from copy import deepcopy
from datetime import datetime, timedelta, timezone
import hashlib
import importlib.util
from pathlib import Path
import plistlib
import struct
import tempfile
import unittest
from unittest import mock
import zipfile

MODULE = Path(__file__).resolve().parents[1] / "ios" / "package-ipa.py"
SPEC = importlib.util.spec_from_file_location("package_ipa", MODULE)
ipa = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ipa)

NOW = datetime(2026, 10, 18, tzinfo=timezone.utc)
CERT = b"inert unit-test certificate bytes, not a signing identity"
FINGERPRINT = hashlib.sha1(CERT).hexdigest().upper()
PROFILE = {
    "Platform": ["iOS"], "UUID": "00000000-0000-0000-0000-000000000001",
    "TeamIdentifier": ["TEAM000001"], "ApplicationIdentifierPrefix": ["LEGACY0001"],
    "ExpirationDate": (NOW + timedelta(days=7)).replace(tzinfo=None),
    "DeveloperCertificates": [CERT], "ProvisionedDevices": ["fixture-phone", "fixture-mac-provisioning-udid"],
    "Entitlements": {"application-identifier": "LEGACY0001.com.ecooxai.lumen",
                     "com.apple.developer.team-identifier": "TEAM000001", "get-task-allow": True},
}


def plan(profile=None, **kwargs):
    return ipa.signing_plan(PROFILE if profile is None else profile, "com.ecooxai.lumen", FINGERPRINT, now=NOW, **kwargs)


def device_app(root: Path, platform=2, *, dependencies=()) -> Path:
    app = root / "Lumen.app"
    app.mkdir()
    info = {"CFBundleSupportedPlatforms": ["iPhoneOS"], "CFBundleExecutable": "Lumen",
            "CFBundleIdentifier": "com.ecooxai.lumen", "UIDeviceFamily": [1, 2]}
    with (app / "Info.plist").open("wb") as target:
        plistlib.dump(info, target)
    # Only a header is needed to test platform inspection; this is not runnable.
    commands = [struct.pack("<6I", 0x32, 24, platform, 0x00100000, 0x001A0000, 0)]
    for kind, name in dependencies:
        encoded = name.encode("utf-8") + b"\0"
        size = (24 + len(encoded) + 7) & ~7
        commands.append(struct.pack("<6I", kind, size, 24, 0, 0, 0) + encoded.ljust(size - 24, b"\0"))
    (app / "Lumen").write_bytes(struct.pack("<8I", 0xFEEDFACF, 0x0100000C, 0, 2,
                                            len(commands), sum(map(len, commands)), 0, 0)
                               + b"".join(commands))
    (app / "Lumen").chmod(0o755)
    (app / "cacert.pem").write_text("inert resource fixture\n")
    for name in ["icudtl.dat", "AppIcon60x60@3x.png", "AppIcon83.5x83.5@2x~ipad.png",
                 "AppIcon76x76@2x~ipad.png", "AppIcon60x60@2x.png"]:
        (app / name).write_bytes(b"inert non-runnable resource fixture")
    return app


class ProfileTests(unittest.TestCase):
    def test_legacy_prefix_can_differ_from_team(self):
        result = plan(jit=True, device_udid="fixture-mac-provisioning-udid")
        self.assertEqual(result["team"], "TEAM000001")
        self.assertEqual(result["method"], "debugging")
        options = ipa.export_options(result)
        self.assertEqual(options["thinning"], "<none>")
        self.assertEqual(options["provisioningProfiles"], {"com.ecooxai.lumen": PROFILE["UUID"]})
        self.assertEqual(options["signingCertificate"], FINGERPRINT)

    def test_app_identifier_scope(self):
        for pattern, allowed in [("com.ecooxai.*", True), ("*", True), ("com.ecooxai.other", False),
                                 ("com.ecoo.*", False), ("com.*.lumen", False), ("com.ecooxai*", False)]:
            with self.subTest(pattern=pattern):
                profile = deepcopy(PROFILE)
                profile["Entitlements"]["application-identifier"] = "LEGACY0001." + pattern
                if allowed:
                    plan(profile)
                else:
                    with self.assertRaises(ipa.PackagingError):
                        plan(profile)

    def test_expired_wrong_team_prefix_certificate_or_platform(self):
        changes = [lambda p: p.update(ExpirationDate=NOW),
                   lambda p: p["Entitlements"].update({"com.apple.developer.team-identifier": "OTHERTEAM0"}),
                   lambda p: p.update(ApplicationIdentifierPrefix=["WRONG00000"]),
                   lambda p: p.update(DeveloperCertificates=[b"different inert certificate"]),
                   lambda p: p.update(Platform=["OSX"])]
        for change in changes:
            profile = deepcopy(PROFILE); change(profile)
            with self.subTest(profile_field=change), self.assertRaises(ipa.PackagingError):
                plan(profile)

    def test_malformed_metadata_fails_with_diagnostic(self):
        for key, value in [("Platform", None), ("ApplicationIdentifierPrefix", None),
                           ("DeveloperCertificates", None), ("ProvisionedDevices", [None]),
                           ("TeamIdentifier", []), ("Entitlements", None), ("ExpirationDate", "later")]:
            profile = deepcopy(PROFILE); profile[key] = value
            with self.subTest(key=key), self.assertRaises(ipa.PackagingError):
                plan(profile)

    def test_device_membership_and_distribution_jit_rejected(self):
        with self.assertRaisesRegex(ipa.PackagingError, "Provisioning UDID"):
            plan(device_udid="fixture-hardware-uuid-is-not-provisioning-udid")
        profile = deepcopy(PROFILE); profile["Entitlements"]["get-task-allow"] = False
        self.assertEqual(plan(profile)["method"], "release-testing")
        with self.assertRaisesRegex(ipa.PackagingError, "get-task-allow"):
            plan(profile, jit=True)

    def test_enterprise_and_app_store_are_distinct(self):
        profile = deepcopy(PROFILE)
        profile["Entitlements"]["get-task-allow"] = False
        profile.pop("ProvisionedDevices")
        with self.assertRaisesRegex(ipa.PackagingError, "App Store"):
            plan(profile)
        profile["ProvisionsAllDevices"] = True
        self.assertEqual(plan(profile, device_udid="fixture-device")["method"], "enterprise")

    def test_identity_selection_requires_valid_unambiguous_keychain_entry(self):
        second = "A" * 40
        output = f'  1) {FINGERPRINT} "Apple Development: Fixture"\n  2) {second} "Apple Development: Fixture"\n'
        self.assertEqual(ipa.select_identity(output, FINGERPRINT.lower()), FINGERPRINT)
        with self.assertRaisesRegex(ipa.PackagingError, "ambiguous"):
            ipa.select_identity(output, "Apple Development: Fixture")
        with self.assertRaisesRegex(ipa.PackagingError, "No valid"):
            ipa.select_identity("0 valid identities found\n", FINGERPRINT)
        with self.assertRaisesRegex(ipa.PackagingError, "not in"):
            ipa.select_identity(output, "nonexistent identity")


class PackagingTests(unittest.TestCase):
    def test_device_dependency_closure_accepts_system_libraries(self):
        with tempfile.TemporaryDirectory() as temporary:
            app = device_app(Path(temporary), dependencies=[
                (0xC, "/usr/lib/libSystem.B.dylib"),
                (0xC, "/System/Library/Frameworks/UIKit.framework/UIKit"),
                (0x80000018, "/System/Library/Frameworks/Metal.framework/Metal"),
            ])
            self.assertEqual(ipa.require_device_app(app)["CFBundleIdentifier"], "com.ecooxai.lumen")

    def test_device_dependency_closure_rejects_uncopied_libraries(self):
        for kind in [0xC, 0x80000018, 0x8000001F, 0x20, 0x80000023]:
            for name in ["/opt/homebrew/opt/v8/lib/libv8.dylib", "@rpath/libv8.dylib",
                         "@executable_path/Frameworks/libv8.dylib", "libv8.dylib",
                         "/usr/lib/../../opt/external.dylib"]:
                with self.subTest(kind=kind, name=name), tempfile.TemporaryDirectory() as temporary:
                    app = device_app(Path(temporary), dependencies=[(kind, name)])
                    with self.assertRaisesRegex(ipa.PackagingError, "dependency"):
                        ipa.require_device_app(app)

    def test_missing_empty_or_linked_runtime_resources_fail_before_export(self):
        for name in ["icudtl.dat", "cacert.pem", "AppIcon60x60@3x.png"]:
            for state in ["absent", "empty", "symlink"]:
                with self.subTest(name=name, state=state), tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary); app = device_app(root)
                    target = app / name
                    target.unlink()
                    if state == "empty":
                        target.touch()
                    elif state == "symlink":
                        outside = root / "outside"
                        outside.write_bytes(b"resource not owned by the app")
                        target.symlink_to(outside)
                    output = root / "Lumen.ipa"
                    output.write_bytes(b"prior artifact")
                    with mock.patch.dict("os.environ", {}, clear=True), mock.patch.object(ipa, "package_local") as package:
                        result = ipa.main(["--app", str(app), "--output", str(output), "--check-only"])
                    self.assertEqual(result, 2)
                    self.assertEqual(output.read_bytes(), b"prior artifact")
                    package.assert_not_called()

    def test_invalid_dependency_bounds_and_hidden_load_commands_fail_closed(self):
        for mutation in ["offset", "terminator", "count"]:
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                app = device_app(Path(temporary), dependencies=[(0xC, "/usr/lib/libSystem.B.dylib")])
                data = bytearray((app / "Lumen").read_bytes())
                command_at = 32 + 24
                if mutation == "offset":
                    struct.pack_into("<I", data, command_at + 8, 1)
                elif mutation == "terminator":
                    data[command_at + 24:] = b"x" * (len(data) - command_at - 24)
                else:
                    struct.pack_into("<I", data, 16, 1)
                (app / "Lumen").write_bytes(data)
                with self.assertRaises(ipa.PackagingError):
                    ipa.require_device_app(app)

    def test_external_dependency_stops_before_signing_and_preserves_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = device_app(root, dependencies=[(0xC, "/opt/homebrew/lib/libv8.dylib")])
            output = root / "Lumen.ipa"
            output.write_bytes(b"prior artifact")
            with mock.patch.dict("os.environ", {}, clear=True), mock.patch.object(ipa, "command_output") as command:
                result = ipa.main(["--app", str(app), "--output", str(output), "--identity", FINGERPRINT,
                                   "--profile", str(root / "unused.mobileprovision")])
            self.assertEqual(result, 2)
            self.assertEqual(output.read_bytes(), b"prior artifact")
            command.assert_not_called()

    def test_platform_header_rejects_disguised_simulator_and_catalyst(self):
        for platform in [2, 6, 7]:
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as temporary:
                app = device_app(Path(temporary), platform)
                if platform == 2:
                    self.assertEqual(ipa.require_device_app(app)["CFBundleIdentifier"], "com.ecooxai.lumen")
                else:
                    with self.assertRaisesRegex(ipa.PackagingError, "not an iOS device"):
                        ipa.require_device_app(app)

    def test_local_archive_preserves_executable_and_rejects_external_symlink(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); app = device_app(root); output = root / "Lumen.ipa"
            ipa.package_local(app, output)
            with zipfile.ZipFile(output) as archive:
                self.assertIn("Payload/Lumen.app/Info.plist", archive.namelist())
                executable = archive.getinfo("Payload/Lumen.app/Lumen")
                self.assertTrue((executable.external_attr >> 16) & 0o111)
                self.assertEqual(archive.read(executable), (app / "Lumen").read_bytes())
            original = output.read_bytes()
            (root / "outside").write_text("must not be archived")
            (app / "unexpected-link").symlink_to(root / "outside")
            with self.assertRaises(ipa.PackagingError):
                ipa.package_local(app, output)
            self.assertEqual(output.read_bytes(), original)

    def test_xcode_export_is_authoritative_and_failure_keeps_prior_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); app = device_app(root); output = root / "Lumen.ipa"
            output.write_bytes(b"prior successful artifact")
            commands = []

            def xcode(command, log, stage):
                commands.append(command)
                if stage == "export":
                    destination = Path(command[command.index("-exportPath") + 1]); destination.mkdir()
                    (destination / "Lumen.ipa").write_bytes(b"artifact produced by Xcode")

            with mock.patch.object(ipa, "run_xcode", side_effect=xcode):
                ipa.package_signed(app, output, plan(), jit=False)
            self.assertEqual(output.read_bytes(), b"artifact produced by Xcode")
            self.assertEqual(len(commands), 2)
            self.assertIn("CODE_SIGNING_ALLOWED=YES", commands[0])
            self.assertIn("PROVISIONING_PROFILE_SPECIFIER=" + PROFILE["UUID"], commands[0])
            self.assertIn("-exportArchive", commands[1])
            self.assertFalse(list(root.glob(".lumen-signing-*")))
            with mock.patch.object(ipa, "run_xcode", side_effect=ipa.PackagingError("Xcode refused signing")):
                with self.assertRaisesRegex(ipa.PackagingError, "refused"):
                    ipa.package_signed(app, output, plan(), jit=False)
            self.assertEqual(output.read_bytes(), b"artifact produced by Xcode")
            self.assertFalse(list(root.glob(".lumen-signing-*")))

    def test_incomplete_signing_options_do_not_run_external_commands(self):
        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(ipa, "command_output") as command:
            root = Path(temporary); app = device_app(root); output = root / "Lumen.ipa"
            with mock.patch.dict("os.environ", {}, clear=True):
                result = ipa.main(["--app", str(app), "--output", str(output), "--identity", FINGERPRINT])
            self.assertEqual(result, 2)
            self.assertFalse(output.exists())
            command.assert_not_called()


if __name__ == "__main__":
    unittest.main()
