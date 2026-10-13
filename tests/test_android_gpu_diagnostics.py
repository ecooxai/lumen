from pathlib import Path
import unittest

from test_android_density import function


ROOT = Path(__file__).resolve().parents[1]


class AndroidGpuDiagnosticsTests(unittest.TestCase):
    def test_android_gpu_errors_reach_logcat(self):
        source = (ROOT / "src/gpu/gpu.c").read_text()
        self.assertIn("#include <android/log.h>", source)
        self.assertIn("__android_log_vprint", source)
        for signature in ("static void on_adapter(", "static void on_device(", "static void on_error("):
            callback = function(source, signature)
            self.assertIn("gpu_log(", callback)
            self.assertNotIn("fprintf(stderr", callback)

    def test_native_diagnostics_are_registered_before_instance_creation(self):
        source = (ROOT / "src/gpu/gpu.c").read_text()
        create = function(source, "Gpu *gpu_create_backend(")
        self.assertIn("wgpuSetLogCallback", create)
        self.assertLess(create.index("wgpuSetLogCallback"), create.index("wgpuCreateInstance"))
        self.assertIn("wgpuSetLogLevel(WGPULogLevel_Warn)", create)
        self.assertIn("gpu_log(", function(source, "static void on_log("))

    def test_driver_validation_is_enabled_only_in_debug_builds(self):
        source = (ROOT / "src/gpu/gpu.c").read_text()
        create = function(source, "Gpu *gpu_create_backend(")
        self.assertIn("#ifndef NDEBUG\n    ex.flags = WGPUInstanceFlag_Validation;\n#endif", create)
        self.assertLess(create.index("ex.flags ="), create.index("wgpuCreateInstance"))


if __name__ == "__main__":
    unittest.main()
