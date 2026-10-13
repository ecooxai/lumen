import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_android_density import function


ROOT = Path(__file__).resolve().parents[1]


class AndroidNewTabTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        work = Path(cls.temporary.name)
        source = (ROOT / "src/app/main.c").read_text()
        extracted = "\n".join(function(source, signature) for signature in (
            "static bool qparam(", "static void sb_esc(", "static void ip_toggle(",
            "static char *internal_page("))
        if "internal_page(App *" not in extracted:
            extracted = extracted.replace("internal_page(const char *u)",
                                          "internal_page(App *a, const char *u)")
        for signature, fallback in (
            ("static float sidebar_width(", "static float sidebar_width(float requested) { return requested; }"),
            ("static bool workspace_action(", "static bool workspace_action(App *a, const char *url) { return false; }"),
        ):
            definition = source.find(signature)
            if definition >= 0 and source.find(";", definition) < source.find("{", definition):
                definition = source.find(signature, definition + len(signature))
            extracted += "\n" + (function(source[definition:], signature) if definition >= 0 else fallback)
        extracted += "\n" + function(source, "static float url_w(")
        extracted += "\n" + function(source, "static void tab_open(").replace("tab_open(", "real_tab_open(", 1)
        loop = source[source.index("int to = a.t->loading"):]
        hud = function(loop, "if (g_hud").replace("a.", "app->").replace("now_ms()", "model_now_ms()")
        extracted += "\nstatic void test_hud_tick(App *app) { int to = 1000; " + hud + " }"
        (work / "newtab_functions.h").write_text(extracted)
        cls.executables = {}
        for platform in ("android", "desktop"):
            executable = work / ("codex-newtab-" + platform)
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-D_GNU_SOURCE",
                            "-fsanitize=undefined", "-fno-sanitize-recover=all",
                            *(["-D__ANDROID__"] if platform == "android" else []),
                            "-I", str(work), "-I", str(ROOT / "src"),
                            str(ROOT / "tests/android/newtab_model.c"),
                            str(ROOT / "src/base/util.c"), "-lm", "-lpthread",
                            "-o", str(executable)], check=True, timeout=90)
            cls.executables[platform] = executable

    def run_scenario(self, scenario, platform="android"):
        result = subprocess.run([str(self.executables[platform]), scenario],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_default_is_workspace_home(self):
        self.run_scenario("default")

    def test_desktop_default_stays_bookmarks(self):
        self.run_scenario("default", "desktop")

    def test_other_sections_remain_available(self):
        self.run_scenario("sections")

    def test_workspace_names_are_escaped_accessible_labels_not_visible_text(self):
        self.run_scenario("icons")

    def test_full_width_android_and_unchanged_desktop_sidebar(self):
        self.run_scenario("sidebar")
        self.run_scenario("sidebar", "desktop")

    def test_phone_address_bar_does_not_reserve_desktop_space(self):
        self.run_scenario("toolbar")

    def test_new_tab_does_not_raise_phone_keyboard(self):
        self.run_scenario("keyboard")
        self.run_scenario("keyboard", "desktop")

    def test_workspace_actions_select_add_and_close(self):
        self.run_scenario("actions")

    def test_untrusted_and_malformed_actions_do_nothing(self):
        self.run_scenario("untrusted")

    def test_workspace_and_tab_limits_are_enforced(self):
        self.run_scenario("limits")

    def test_hidden_sidebar_does_not_sample_or_repaint_hud(self):
        self.run_scenario("hidden-hud")
        self.run_scenario("visible-hud", "desktop")

    def test_only_native_link_activation_executes_workspace_actions(self):
        source = (ROOT / "src/app/main.c").read_text()
        self.assertIn("workspace_action(a, h)", function(source, "static void click_page("))
        self.assertNotIn("workspace_action(", function(source, "static void navigate_ex("))
        self.assertIn("p->internal_ui = rq->html != NULL", source)
        self.assertIn("a.side = sidebar_width(", source)
        self.assertIn("a->side = sidebar_width(", function(source, "static void menu_cmd("))


if __name__ == "__main__":
    unittest.main()
