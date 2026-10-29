import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_android_density import function


ROOT = Path(__file__).resolve().parents[1]


class AndroidRendererTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        work = Path(cls.directory.name)
        main = (ROOT / "src/app/main.c").read_text()
        gpu = (ROOT / "src/gpu/gpu.c").read_text()
        header = (ROOT / "src/gpu/gpu.h").read_text()
        extracted = '#include "gpu/gpu.h"\n'
        if "GPU_BACKEND_VULKAN" not in header:
            extracted += "typedef enum { GPU_BACKEND_AUTO, GPU_BACKEND_VULKAN, GPU_BACKEND_GL, GPU_BACKEND_CPU } GpuBackend;\n"
        extracted += "static GpuBackend g_renderer = GPU_BACKEND_VULKAN;\n"
        for source, signature, fallback in (
            (gpu, "static WGPUInstanceBackend gpu_backend_flags(", "static WGPUInstanceBackend gpu_backend_flags(GpuBackend backend) { (void)backend; return WGPUInstanceBackend_Primary; }"),
            (main, "static int renderer_parse(", "static int renderer_parse(const char *value) { (void)value; return 0; }"),
            (main, "static const char *renderer_key(", "static const char *renderer_key(GpuBackend backend) { (void)backend; return \"auto\"; }"),
            (main, "static void renderer_load(", "static void renderer_load(const char *line) { (void)line; }"),
            (main, "static void pref_path(", ""),
            (main, "static void settings_save(", ""),
        ):
            extracted += "\n" + (function(source, signature) if signature in source else fallback)
        (work / "renderer_functions.h").write_text(extracted)
        cls.executable = work / "codex-agent-android-renderer"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-D__ANDROID__",
                        "-fsanitize=undefined", "-fno-sanitize-recover=all",
                        "-I", str(work), "-I", str(ROOT / "src"),
                        "-I", str(ROOT / "build/codex-android-wgpu/prefix/include/webgpu"),
                        str(ROOT / "tests/android/renderer_model.c"), "-o", str(cls.executable)],
                       check=True, timeout=90)

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.executable), scenario], cwd=self.directory.name,
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_backend_flags_select_vulkan_gl_and_software_explicitly(self):
        self.run_scenario("flags")

    def test_renderer_preferences_are_strict_and_round_trip(self):
        self.run_scenario("preferences")

    def test_renderer_selection_is_persisted(self):
        self.run_scenario("save")

    def test_android_defaults_to_vulkan_and_loads_before_gpu_creation(self):
        source = (ROOT / "src/app/main.c").read_text()
        self.assertIn("static GpuBackend g_renderer = GPU_BACKEND_VULKAN;", source)
        startup = source[source.index("int main("):]
        self.assertLess(startup.index("renderer_read_preferences()"), startup.index("gpu_create_backend("))
        self.assertIn("g_renderer", startup[startup.index("gpu_create_backend("):startup.index("gpu_create_backend(") + 100])

    def test_settings_offer_backends_and_report_active_renderer(self):
        source = (ROOT / "src/app/main.c").read_text()
        self.assertIn("Graphics backend", source)
        self.assertIn("update_renderer_readout(a)", function(source, "static void render("))
        self.assertIn('qparam(q, "renderer"', function(source, "static void apply_set("))
        self.assertIn("gpu_backend_name", source)
        self.assertIn("raster", function(source, "static char *internal_page("))

    def test_backend_switch_releases_old_gpu_and_invalidates_cached_frame(self):
        source = (ROOT / "src/app/main.c").read_text()
        self.assertIn("static void renderer_apply(", source)
        apply = function(source, "static void renderer_apply(")
        self.assertLess(apply.index("gpu_destroy("), apply.index("gpu_create_backend("))
        for expected in ("SDL_DestroyWindowSurface", "media_yuv", "vonly", "gvid_ok", "osig_ok", "dirty"):
            self.assertIn(expected, apply)


if __name__ == "__main__":
    unittest.main()
