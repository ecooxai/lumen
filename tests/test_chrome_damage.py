import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ChromeDamageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        work = Path(cls.temporary.name)
        source = (ROOT / "src/app/main.c").read_text()
        start = source.index("            bool dmg =")
        end = source.index("\n        }\n        { struct DSig *t", start)
        (work / "chrome_damage_function.h").write_text(source[start:end])
        hash_start = source.index("static uint64_t hmix(")
        hash_end = source.index("static uint64_t ditem_hash(", hash_start)
        sig_start = source.index("static int dl_sigs(")
        sig_end = source.index("static uint64_t dl_hash(", sig_start)
        (work / "chrome_damage_hash.h").write_text(source[hash_start:hash_end])
        (work / "chrome_damage_signatures.h").write_text(source[sig_start:sig_end])
        cls.executable = work / "codex-chrome-damage-model"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=undefined,float-cast-overflow", "-fno-sanitize-recover=all",
                        "-I", str(work), str(ROOT / "tests/android/chrome_damage_model.c"),
                        "-lm", "-o", str(cls.executable)], check=True)

    def test_hud_change_preserves_page_pixels_and_recomposes_chrome(self):
        self.run_scenario(0)

    def test_unchanged_frame_skips_raster_and_upload(self):
        self.run_scenario(1)

    def test_page_damage_and_chrome_change_use_partial_page_raster(self):
        self.run_scenario(2)

    def test_large_page_damage_retains_full_raster(self):
        self.run_scenario(3)

    def test_removed_item_erases_old_bounds(self):
        self.run_scenario(4)

    def test_empty_display_lists_do_not_overflow_integer_bounds(self):
        self.run_scenario(5)

    def test_offscreen_damage_is_clipped_before_integer_conversion(self):
        self.run_scenario(6)

    def test_invalid_cache_requires_full_raster(self):
        self.run_scenario(7)

    def test_inserted_caret_does_not_damage_unchanged_fixed_controls(self):
        self.run_scenario(8)

    def test_removed_caret_does_not_damage_unchanged_fixed_controls(self):
        self.run_scenario(9)

    def test_inserted_offscreen_caret_skips_page_raster(self):
        self.run_scenario(10)

    def test_removed_offscreen_caret_skips_page_raster(self):
        self.run_scenario(11)

    def test_insertion_and_replacement_union_both_changes(self):
        self.run_scenario(12)

    def test_reordered_items_damage_both_old_and_new_coverage(self):
        self.run_scenario(13)

    def test_changed_layer_damages_unchanged_children(self):
        self.run_scenario(14)

    def test_duplicate_signatures_do_not_hide_inserted_bounds(self):
        self.run_scenario(15)

    def test_inserted_clip_invalidates_unchanged_suffix_context(self):
        self.run_scenario(16)

    def test_removed_clip_pop_invalidates_unchanged_suffix_context(self):
        self.run_scenario(17)

    def test_inserted_layer_invalidates_unchanged_suffix_context(self):
        self.run_scenario(18)

    def test_removed_layer_pop_invalidates_unchanged_suffix_context(self):
        self.run_scenario(19)

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.executable), str(scenario)], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
