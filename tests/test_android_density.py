import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    while source.index(";", start) < opening:
        start = source.index(signature, start + len(signature))
        opening = source.index("{", start)
    depth = 1
    ending = opening + 1
    while depth:
        depth += (source[ending] == "{") - (source[ending] == "}")
        ending += 1
    return source[start:ending]


class AndroidDensityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        work = Path(cls.temporary.name)
        source = (ROOT / "src/app/main.c").read_text()
        extracted = function(source, "static void update_size(App *a)")
        for signature in ("static bool pixel_scale_valid(", "static void resolution_text("):
            if signature in source:
                extracted = function(source, signature) + "\n" + extracted
        cls.has_input_adapter = "static void input_coordinates(" in source
        if cls.has_input_adapter:
            extracted += "\n#define HAVE_INPUT_ADAPTER 1\n" + function(source, "static void input_coordinates(")
        (work / "density_functions.h").write_text(extracted)
        cls.executables = {}
        for platform in ("android", "desktop"):
            executable = work / ("codex-density-" + platform)
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11",
                            "-fsanitize=undefined,float-cast-overflow", "-fno-sanitize-recover=all",
                            *(["-D__ANDROID__"] if platform == "android" else []),
                            "-I", str(work), str(ROOT / "tests/android/density_model.c"),
                            "-lm", "-o", str(executable)], check=True, timeout=90)
            cls.executables[platform] = executable

    def run_scenario(self, scenario, platform="android"):
        result = subprocess.run([str(self.executables[platform]), scenario],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_portrait_uses_default_two_physical_pixels_per_layout_pixel(self):
        self.run_scenario("portrait")

    def test_landscape_preserves_physical_surface_size(self):
        self.run_scenario("landscape")

    def test_keyboard_resize_preserves_density(self):
        self.run_scenario("keyboard")

    def test_system_density_never_changes_app_scale(self):
        self.run_scenario("invalid")

    def test_user_scale_change_invalidates_partial_presentation(self):
        self.run_scenario("change")

    def test_one_to_one_pixels_ignore_sdl_logical_size(self):
        self.run_scenario("physical")

    def test_resolution_readout_matches_screen_and_framebuffer(self):
        self.run_scenario("resolution")

    def test_desktop_retains_sdl_window_coordinates(self):
        self.run_scenario("desktop", "desktop")

    def test_pointer_positions_and_relative_motion_use_css_units(self):
        self.assertTrue(self.has_input_adapter, "Missing input coordinate adapter")
        self.run_scenario("pointer")

    def test_wheel_location_scales_but_scroll_ticks_do_not(self):
        self.assertTrue(self.has_input_adapter, "Missing input coordinate adapter")
        self.run_scenario("wheel")

    def test_unrelated_events_remain_unchanged(self):
        self.assertTrue(self.has_input_adapter, "Missing input coordinate adapter")
        self.run_scenario("unrelated")


class DensityWiringTests(unittest.TestCase):
    def test_adapter_runs_before_event_hit_testing(self):
        source = (ROOT / "src/app/main.c").read_text()
        loop = source[source.index("bool wheel0 = ev.type"):]
        self.assertIn("input_coordinates(&a, &ev)", loop)
        self.assertLess(loop.index("input_coordinates(&a, &ev)"), loop.index("switch (ev.type)"))

    def test_display_scale_changes_refresh_metrics(self):
        source = (ROOT / "src/app/main.c").read_text()
        self.assertRegex(source, r"case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:\s*update_size\(&a\)")

    def test_canvas_caches_include_scale(self):
        source = (ROOT / "src/app/main.c").read_text()
        self.assertIn("a->frame.scale != a->scale", source)
        self.assertIn("a->page.scale != page_scale(a)", source)
        self.assertIn("p->cv.scale != s", source)

    def test_css_viewports_receive_device_pixel_ratio(self):
        source = (ROOT / "src/app/main.c").read_text()
        helper = function(source, "static bool page_viewport(")
        self.assertIn("page->e->media.dpr", helper)
        self.assertIn("page_viewport(p, p->fw, p->fh, frame_scale(p))", source)
        self.assertIn("page_viewport(p, page_width(a), page_height(a), page_scale(a))", source)


if __name__ == "__main__":
    unittest.main()
