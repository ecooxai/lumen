import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_android_density import function


ROOT = Path(__file__).resolve().parents[1]


class AndroidRendererSwitchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        work = Path(cls.directory.name)
        source = (ROOT / "src/app/main.c").read_text()
        signature = "static void renderer_apply("
        extracted = function(source, signature) if signature in source else (
            "static void renderer_apply(App *app, GpuBackend backend) { (void)app; (void)backend; }")
        (work / "renderer_switch_function.h").write_text(extracted)
        cls.executable = work / "codex-agent-renderer-switch"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-I", str(work),
                        "-I", str(ROOT / "src"), "-fsanitize=undefined",
                        "-fno-sanitize-recover=all", str(ROOT / "tests/android/renderer_switch_model.c"),
                        "-o", str(cls.executable)], check=True, timeout=90)

    def test_switch_releases_then_creates_and_invalidates(self):
        self.scenario("switch")

    def test_software_and_failure_fallback_are_safe(self):
        self.scenario("fallback")

    def scenario(self, name):
        result = subprocess.run([str(self.executable), name], capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
