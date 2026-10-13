import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_android_density import function


ROOT = Path(__file__).resolve().parents[1]


class AndroidPixelSettingsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        work = Path(cls.temporary.name)
        source = (ROOT / "src/app/main.c").read_text()
        extracted = function(source, "static const char *renderer_key(") + "\n"
        extracted += "\n".join(function(source, signature) for signature in (
            "static void pref_path(", "static void settings_save("))
        for signature, fallback in (
            ("static int pixel_scale_parse(", "static int pixel_scale_parse(const char *value) { return 0; }"),
            ("static void pixel_scale_load(", "static void pixel_scale_load(const char *line) {}"),
        ):
            extracted += "\n" + (function(source, signature) if signature in source else fallback)
        for signature in ("static void resolution_text(", "static void update_resolution_readout("):
            extracted += "\n" + function(source, signature)
        (work / "pixel_settings_functions.h").write_text(extracted)
        cls.executable = work / "codex-pixel-settings"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-D__ANDROID__",
                        "-fsanitize=undefined", "-fno-sanitize-recover=all",
                        "-I", str(work), "-I", str(ROOT / "src"), str(ROOT / "tests/android/pixel_settings_model.c"),
                        "-o", str(cls.executable)], check=True, timeout=90)

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.executable), scenario],
                                cwd=self.temporary.name, capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_scale_choices_and_malformed_values(self):
        self.run_scenario("parse")

    def test_load_rejects_invalid_preferences_without_changing_default(self):
        self.run_scenario("load")

    def test_saved_scale_round_trips(self):
        self.run_scenario("save")

    def test_existing_settings_tab_refreshes_selection_once_and_ignores_web_pages(self):
        self.run_scenario("readout")

    def test_startup_loads_before_navigation_and_settings_refresh_size(self):
        source = (ROOT / "src/app/main.c").read_text()
        self.assertIn("static int g_pixel_scale_pct = 200;", source)
        self.assertIn("pixel_scale_load(line)", source)
        apply = function(source, "static void apply_set(")
        self.assertIn('qparam(q, "pixel_scale"', apply)
        self.assertIn("pixel_scale_parse(v)", apply)
        self.assertIn("update_size(a)", apply)
        self.assertIn("android_touch_reset(&a->touch)", apply)
        startup = source[source.index("if (sf) fclose(sf);"):source.index("navigate(&a, start, true);")]
        self.assertIn("update_size(&a)", startup)

    def test_internal_readout_updates_without_polling_or_mutating_web_content(self):
        source = (ROOT / "src/app/main.c").read_text()
        self.assertIn("static void update_resolution_readout(", source)
        helper = function(source, "static void update_resolution_readout(")
        self.assertIn("internal_ui", helper)
        self.assertIn("strcmp", helper)
        self.assertIn("node_set_text_content", helper)
        self.assertIn("update_resolution_readout(a)", function(source, "static void render("))
        self.assertNotIn("SDL_GetWindowDisplayScale", function(source, "static void update_size("))


if __name__ == "__main__":
    unittest.main()
