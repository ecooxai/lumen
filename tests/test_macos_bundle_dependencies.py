"""Dependency relocation tests; synthetic files are not native Mac execution."""
from copy import deepcopy
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import macos_bundle_dependencies as bundle


class BundlePlanTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.executable = self.root / "source/lumen"
        self.v8 = self.root / "v8/libv8.dylib"
        self.icu = self.root / "icu/libicuuc.dylib"
        self.app = self.root / "out/Relocated.app"
        self.closure = {
            "executable": str(self.executable), "nonSystemClosureFullyResolved": True,
            "files": [
                {"path": str(self.executable), "sha256": "a" * 64, "dependencies": [
                    {"installName": "/opt/homebrew/opt/v8/lib/libv8.dylib", "resolved": str(self.v8)},
                    {"installName": "/usr/lib/libSystem.B.dylib", "systemSharedCache": True},
                ]},
                {"path": str(self.v8), "sha256": "b" * 64, "dependencies": [
                    {"installName": "@rpath/libicuuc.dylib", "resolved": str(self.icu)},
                ]},
                {"path": str(self.icu), "sha256": "c" * 64, "dependencies": []},
            ],
        }

    def test_every_non_system_edge_uses_bundle_relative_target(self):
        plan = bundle.plan_bundle(self.closure, self.app)
        by_source = {row["source"]: row for row in plan}
        self.assertEqual(by_source[str(self.executable)]["changes"], [
            ["/opt/homebrew/opt/v8/lib/libv8.dylib", "@executable_path/../Frameworks/libv8.dylib"]])
        self.assertEqual(by_source[str(self.v8)]["changes"], [
            ["@rpath/libicuuc.dylib", "@loader_path/libicuuc.dylib"]])
        self.assertEqual(by_source[str(self.v8)]["target"], str(self.app / "Contents/Frameworks/libv8.dylib"))
        self.assertFalse(any("libSystem" in pair[0] for row in plan for pair in row["changes"]))

    def test_same_install_basename_from_two_libraries_is_rejected(self):
        conflict = deepcopy(self.closure["files"][2])
        conflict["path"] = str(self.root / "different/libV8.dylib")
        self.closure["files"].append(conflict)
        with self.assertRaisesRegex(ValueError, "collision"):
            bundle.plan_bundle(self.closure, self.app)

    def test_missing_transitive_dependency_is_never_silently_skipped(self):
        self.closure["files"].pop()
        with self.assertRaisesRegex(ValueError, "missing"):
            bundle.plan_bundle(self.closure, self.app)

    def test_relative_unsafe_or_falsely_system_paths_are_rejected(self):
        for install in ["@rpath/host.dylib", "/usr/lib/../../opt/host.dylib", "host.dylib"]:
            with self.subTest(install=install):
                closure = deepcopy(self.closure)
                closure["files"][0]["dependencies"][1]["installName"] = install
                with self.assertRaisesRegex(ValueError, "system"):
                    bundle.plan_bundle(closure, self.app)

    def test_incomplete_closure_is_rejected(self):
        self.closure["nonSystemClosureFullyResolved"] = False
        with self.assertRaisesRegex(ValueError, "complete"):
            bundle.plan_bundle(self.closure, self.app)

    def test_outside_bundle_dependency_invalidates_final_audit(self):
        app_exe = self.app / "Contents/MacOS/lumen"
        for path in [self.v8, self.root / "out/Relocated.app-other/libv8.dylib"]:
            with self.subTest(path=path):
                closure = {"nonSystemClosureFullyResolved": True,
                           "files": [{"path": str(app_exe)}, {"path": str(path)}]}
                with self.assertRaisesRegex(ValueError, "outside"):
                    bundle.require_embedded_closure(closure, self.app)
        bundle.require_embedded_closure({"nonSystemClosureFullyResolved": True, "files": [
            {"path": str(app_exe)}, {"path": str(self.app / "Contents/Frameworks/libv8.dylib")},
        ]}, self.app)


if __name__ == "__main__":
    unittest.main()
