import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class GpuUploadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.executable = Path(cls.temporary.name) / "codex-agent-gpu-upload"
        headers = Path(os.environ.get("LUMEN_TEST_WGPU_HEADERS",
                       ROOT / "build/codex-android-wgpu/prefix/include/webgpu"))
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-D__ANDROID__", "-ffunction-sections", "-fdata-sections",
                        "-fsanitize=undefined", "-fno-sanitize-recover=all",
                        "-I", str(ROOT / "tests/android/gpu_stubs"), "-I", str(headers),
                        str(ROOT / "tests/android/gpu_upload_model.c"),
                        "-Wl,--gc-sections", "-o", str(cls.executable)],
                       check=True, capture_output=True, text=True, timeout=60)

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.executable), scenario], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_page_upload_uses_portable_rgba_texture(self):
        self.run_scenario("page")

    def test_video_upload_uses_portable_rgba_texture(self):
        self.run_scenario("video")

    def test_shader_restores_bgra_channel_order_without_swizzling_alpha(self):
        self.run_scenario("shader")

    def test_same_dimensions_reuse_page_and_video_textures(self):
        self.run_scenario("reuse")

    def test_resize_releases_old_page_and_video_resources(self):
        self.run_scenario("resize")


if __name__ == "__main__":
    unittest.main()
