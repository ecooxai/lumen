import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("v8_prebuilt", ROOT / "tools/android/v8_prebuilt.py")
v8_prebuilt = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(v8_prebuilt)


class AndroidV8PrebuiltTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.sdk = Path(self.temporary.name) / "sdk"
        (self.sdk / "include").mkdir(parents=True)
        (self.sdk / "include/v8.h").write_bytes(b"test-only header")
        (self.sdk / "libv8.a").write_bytes(b"test-only archive")
        entry = "v8.h\0" + hashlib.sha256(b"test-only header").hexdigest() + "\n"
        self.manifest = {
            "schema": 1, "abi": "arm64-v8a", "min_api": 29,
            "version": "15.0.245.2",
            "files": {"libv8.a": hashlib.sha256(b"test-only archive").hexdigest()},
            "trees": {"include": hashlib.sha256(entry.encode()).hexdigest()},
        }

    def verify(self, **options):
        return v8_prebuilt.verify_sdk(self.sdk, self.manifest,
                                      options.get("abi", "arm64-v8a"), options.get("api", 29))

    def test_valid_pinned_sdk(self):
        self.assertEqual(self.verify()["version"], "15.0.245.2")
        self.assertEqual(self.verify(api=35)["min_api"], 29)

    def test_rejects_api28_native_tls(self):
        with self.assertRaisesRegex(ValueError, "API"):
            self.verify(api=28)

    def test_rejects_wrong_abi(self):
        with self.assertRaisesRegex(ValueError, "ABI"):
            self.verify(abi="x86_64")

    def test_rejects_archive_modification(self):
        (self.sdk / "libv8.a").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.verify()

    def test_rejects_header_modification(self):
        (self.sdk / "include/v8.h").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.verify()

    def test_rejects_added_shadow_header(self):
        (self.sdk / "include/memory").write_bytes(b"shadow STL")
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.verify()

    def test_rejects_missing_header(self):
        (self.sdk / "include/v8.h").unlink()
        with self.assertRaises(ValueError):
            self.verify()

    def test_rejects_missing_archive(self):
        (self.sdk / "libv8.a").unlink()
        with self.assertRaises(ValueError):
            self.verify()

    def test_rejects_symlink_file_and_directory(self):
        for name in ("libv8.a", "include"):
            with self.subTest(name=name):
                original = self.sdk / name
                moved = self.sdk.parent / (name + "-outside")
                original.rename(moved)
                original.symlink_to(moved)
                with self.assertRaisesRegex(ValueError, "symlink"):
                    self.verify()
                original.unlink()
                moved.rename(original)

    def test_rejects_nested_symlink(self):
        (self.sdk / "include/extra").symlink_to(self.sdk.parent)
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.verify()

    def test_rejects_unknown_schema(self):
        self.manifest["schema"] = 2
        with self.assertRaisesRegex(ValueError, "schema"):
            self.verify()

    def test_rejects_unsafe_manifest_paths(self):
        for name in ("../libv8.a", "/tmp/libv8.a", "include/../libv8.a"):
            with self.subTest(name=name):
                self.manifest["files"] = {name: "0" * 64}
                with self.assertRaisesRegex(ValueError, "path"):
                    self.verify()

    def test_tree_digest_is_stable_and_path_sensitive(self):
        self.assertEqual(v8_prebuilt.tree_digest(self.sdk / "include"),
                         self.manifest["trees"]["include"])
        (self.sdk / "include/v8.h").rename(self.sdk / "include/other.h")
        self.assertNotEqual(v8_prebuilt.tree_digest(self.sdk / "include"),
                            self.manifest["trees"]["include"])


if __name__ == "__main__":
    unittest.main()
