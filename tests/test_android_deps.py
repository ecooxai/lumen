import importlib.util
import json
from pathlib import Path
import tempfile
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("android_deps", ROOT / "tools/android/build_deps.py")
android_deps = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(android_deps)


class AndroidDependencyTests(unittest.TestCase):
    def test_fresh_clone_has_a_clean_index_before_pin_checkout(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            subprocess.run(["git", "init", "-q", str(source)], check=True)
            (source / "fixture").write_text("dependency fixture\n")
            subprocess.run(["git", "-C", str(source), "add", "fixture"], check=True)
            subprocess.run(["git", "-C", str(source), "-c", "user.name=Codex Test",
                            "-c", "user.email=fixture@example.invalid", "commit", "-qm", "fixture"], check=True)
            plan = android_deps.build_plan(ROOT, root / "work", root / "ndk", "arm64-v8a", 2)
            command = plan["steps"][0]["argv"][:-2] + [str(source), str(root / "checkout")]
            subprocess.run(command, check=True, capture_output=True)
            for option in ([], ["--cached"]):
                result = subprocess.run(["git", "-C", str(root / "checkout"), "diff", *option, "--quiet"])
                self.assertEqual(result.returncode, 0)

    def test_manifest_covers_engine_dependencies(self):
        manifest = json.loads((ROOT / "android/native-deps/vcpkg.json").read_text())
        dependencies = {entry if isinstance(entry, str) else entry["name"] for entry in manifest["dependencies"]}
        self.assertTrue({"icu", "openssl", "zlib", "brotli", "zstd", "freetype", "harfbuzz",
                         "ffmpeg", "libpng", "libjpeg-turbo", "libwebp", "giflib"} <= dependencies)
        self.assertEqual(manifest["builtin-baseline"], android_deps.VCPKG_REVISION)
        ffmpeg = next(entry for entry in manifest["dependencies"] if isinstance(entry, dict) and entry["name"] == "ffmpeg")
        self.assertFalse(ffmpeg["default-features"])
        self.assertTrue({"avcodec", "avformat", "swscale", "swresample"} <= set(ffmpeg["features"]))
        self.assertTrue({"gpl", "nonfree", "ffmpeg", "ffplay"}.isdisjoint(ffmpeg["features"]))

    def test_plan_is_android_only_and_bounded(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            plan = android_deps.build_plan(ROOT, root / "work", root / "ndk", "arm64-v8a", 2)
            self.assertEqual(plan["abi"], "arm64-v8a")
            self.assertEqual(plan["environment"]["ANDROID_NDK_HOME"], str(root / "ndk"))
            self.assertEqual(plan["environment"]["VCPKG_MAX_CONCURRENCY"], "2")
            self.assertEqual(plan["prefix"], str(root / "work/prefix/arm64-v8a"))
            commands = [step["argv"] for step in plan["steps"]]
            self.assertTrue(any(android_deps.VCPKG_REVISION in command for command in commands))
            self.assertTrue(any("arm64-android" in command for command in commands))
            self.assertIn("V8", plan["remaining"])
            self.assertIn("wgpu-native", plan["remaining"])
            self.assertFalse((root / "work").exists())

    def test_invalid_abi_or_concurrency_is_rejected(self):
        for abi, jobs in [("x86", 2), ("arm64-v8a", 0), ("x86_64", 65)]:
            with self.assertRaises(ValueError):
                android_deps.build_plan(ROOT, Path("work"), Path("ndk"), abi, jobs)

    def test_ndk_requires_exact_version_and_compiler(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaises(ValueError):
                android_deps.validate_ndk(root, "arm64-v8a")
            (root / "source.properties").write_text("Pkg.Revision = 27.0.0\n")
            with self.assertRaises(ValueError):
                android_deps.validate_ndk(root, "arm64-v8a")
            (root / "source.properties").write_text("Pkg.Revision = 28.2.13676358\n")
            compiler = root / "toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang"
            compiler.parent.mkdir(parents=True)
            compiler.write_text("")
            compiler.chmod(0o755)
            android_deps.validate_ndk(root, "arm64-v8a")


if __name__ == "__main__":
    unittest.main()
