import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_android_density import function


ROOT = Path(__file__).resolve().parents[1]


class AndroidScrollTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        work = Path(cls.directory.name)
        source = (ROOT / "src/app/main.c").read_text()
        signature = "static bool android_scroll_apply("
        extracted = function(source, signature) if signature in source else (
            function(source, "static void wheel_apply(") +
            "\nstatic bool android_scroll_apply(App *app, float x, float y, float dx, float dy) {"
            " wheel_apply(app, x, y, dx, dy); return true; }\n")
        helpers = "\n".join(function(source, signature) for signature in (
            "static float tab_zoom(", "static float page_scale(", "static float page_x(", "static float page_y("))
        (work / "scroll_functions.h").write_text(helpers + "\n" + extracted)
        cls.executable = work / "codex-agent-android-scroll"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-fsanitize=undefined",
                        "-fno-sanitize-recover=all", "-I", str(work),
                        str(ROOT / "tests/android/scroll_model.c"), "-lm",
                        "-o", str(cls.executable)], check=True, timeout=90)

    def scenario(self, name):
        result = subprocess.run([str(self.executable), name], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_root_scroll_clamps_and_preserves_full_damage(self):
        self.scenario("clamp")

    def test_boundary_and_zero_motion_do_not_repaint(self):
        self.scenario("boundary")

    def test_nested_scroll_receives_anchor_and_delta(self):
        self.scenario("nested")

    def test_invalid_or_unavailable_page_stops(self):
        self.scenario("invalid")

    def test_root_scroll_stays_on_physical_pixel_grid(self):
        self.scenario("pixels")

    def test_zoom_preserves_finger_distance_and_nested_hit_coordinates(self):
        self.scenario("zoom")

    def test_event_timestamps_drive_velocity_and_cancellation(self):
        source = (ROOT / "src/app/main.c").read_text()
        touch = function(source, "static bool android_touch_input(")
        for expected in ("android_fling_begin", "android_fling_sample", "android_fling_release",
                         "android_fling_reset", "event->tfinger.timestamp", "android_scroll_apply"):
            self.assertIn(expected, touch)
        self.assertIn("static void android_fling_tick(", source)
        tick = function(source, "static void android_fling_tick(")
        for expected in ("touch_generation", "touch_tab", "android_fling_step", "android_scroll_apply"):
            self.assertIn(expected, tick)

    def test_only_active_fling_requests_frames_after_events(self):
        source = (ROOT / "src/app/main.c").read_text()
        startup = source[source.index("int main("):]
        self.assertIn("to = android_fling_wait_ms(&a.fling, now_ms(), to);", startup)
        self.assertLess(startup.index("android_fling_wait_ms(&a.fling"),
                        startup.index("SDL_WaitEventTimeout"))
        self.assertLess(startup.index("SDL_PollEvent"), startup.index("android_fling_tick(&a)"))
        self.assertIn("android_fling_reset", function(source, "static void apply_set("))


if __name__ == "__main__":
    unittest.main()
