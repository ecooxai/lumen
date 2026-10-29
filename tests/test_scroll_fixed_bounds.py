import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ScrollFixedBoundsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        work = Path(cls.temporary.name)
        cls.executable = work / "codex-scroll-fixed-bounds"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=undefined,float-cast-overflow", "-fno-sanitize-recover=all",
                        "-I", str(work), str(ROOT / "tests/android/scroll_fixed_bounds_model.c"),
                        "-lm", "-o", str(cls.executable)], check=True, timeout=60)

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.executable), scenario], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_empty_fixed_list_at_supported_250_percent(self):
        self.run_scenario("empty-250")

    def test_empty_fixed_list_at_supported_300_percent(self):
        self.run_scenario("empty-300")

    def test_far_positive_fixed_bounds_are_clipped_before_cast(self):
        self.run_scenario("positive")

    def test_far_negative_fixed_bounds_are_clipped_before_cast(self):
        self.run_scenario("negative")

    def test_extreme_fixed_box_spanning_viewport(self):
        self.run_scenario("spanning")

    def test_upward_scroll_repaints_old_and_new_fixed_pixels(self):
        self.run_scenario("up")

    def test_downward_scroll_repaints_old_and_new_fixed_pixels(self):
        self.run_scenario("down")

    def test_fixed_pixels_shifted_in_from_below(self):
        self.run_scenario("below")

    def test_fixed_pixels_shifted_in_from_above(self):
        self.run_scenario("above")

    def test_zero_width_fixed_box_has_no_damage(self):
        self.run_scenario("empty-box")

    def test_supported_scales_and_scroll_directions(self):
        self.run_scenario("matrix")


if __name__ == "__main__":
    unittest.main()
