import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class GpuLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.executable = Path(cls.temporary.name) / "codex-gpu-lifecycle-model"
        headers = Path(os.environ.get("LUMEN_TEST_WGPU_HEADERS",
                       ROOT / "build/codex-android-wgpu/prefix/include/webgpu"))
        result = subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                                 "-D__ANDROID__", "-ffunction-sections", "-fdata-sections",
                                 "-fsanitize=undefined", "-fno-sanitize-recover=all",
                                 "-I", str(ROOT / "tests/android/gpu_stubs"), "-I", str(headers),
                                 str(ROOT / "tests/android/gpu_lifecycle_model.c"),
                                 "-Wl,--gc-sections", "-o", str(cls.executable)],
                                capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_suspend_releases_surface_but_retains_device_and_cache(self):
        self.run_scenario(0)

    def test_resize_while_suspended_does_not_configure_absent_surface(self):
        self.run_scenario(1)

    def test_create_failure_releases_native_window_reference(self):
        self.run_scenario(2)

    def test_capability_failure_releases_surface_and_native_reference(self):
        self.run_scenario(3)

    def test_incompatible_format_does_not_reuse_pipeline(self):
        self.run_scenario(4)

    def test_invalid_arguments_fail_without_acquiring_surface(self):
        self.run_scenario(5)

    def test_repeated_resume_rebinds_new_window_without_reference_leaks(self):
        self.run_scenario(6)

    def test_zero_extent_unconfigures_before_later_resize(self):
        self.run_scenario(7)

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.executable), str(scenario)], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
