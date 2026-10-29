import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PageComposeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        work = Path(cls.temporary.name)
        source = (ROOT / "src/app/main.c").read_text()
        start = source.index("static void compose_page(")
        end = source.index("\n}\n", start) + 3
        (work / "page_compose_function.h").write_text(source[start:end])
        cls.executable = work / "codex-page-compose-model"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=undefined", "-fno-sanitize-recover=all",
                        "-I", str(work), str(ROOT / "tests/android/page_compose_model.c"),
                        "-o", str(cls.executable)], check=True)

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.executable), scenario], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_full_width_uses_one_copy(self):
        self.run_scenario("full")

    def test_partial_rows_use_one_copy(self):
        self.run_scenario("partial")

    def test_bottom_clipped_to_frame(self):
        self.run_scenario("clipped")

    def test_empty_damage_does_not_copy(self):
        self.run_scenario("empty")

    def test_reversed_damage_does_not_copy(self):
        self.run_scenario("reversed")

    def test_skipped_frame_does_not_copy(self):
        self.run_scenario("skip")

    def test_sidebar_preserves_neighbor_pixels(self):
        self.run_scenario("sidebar")

    def test_equal_padded_strides_preserve_padding(self):
        self.run_scenario("padded")

    def test_page_padding_preserved(self):
        self.run_scenario("page-padding")

    def test_frame_padding_preserved(self):
        self.run_scenario("frame-padding")

    def test_single_row(self):
        self.run_scenario("single")

    def test_toolbar_fills_frame(self):
        self.run_scenario("toolbar")

    def test_pixel_equivalence_across_sizes_and_damage(self):
        self.run_scenario("matrix")


if __name__ == "__main__":
    unittest.main()
