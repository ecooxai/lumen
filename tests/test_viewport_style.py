import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ViewportStyleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.executable = Path(cls.temporary.name) / "codex-viewport-style-test"
        sources = [ROOT / "tests/test_viewport_style.c"]
        for directory in ("base", "dom", "css"):
            sources.extend(sorted((ROOT / "src" / directory).glob("*.c")))
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-D_GNU_SOURCE",
                        "-fsanitize=undefined", "-fno-sanitize-recover=all",
                        "-I", str(ROOT / "src"), *map(str, sources),
                        "-licuuc", "-lm", "-lpthread", "-o", str(cls.executable)],
                       check=True, capture_output=True, text=True, timeout=90)

    def test_computed_viewport_units_follow_resize_without_dom_mutation(self):
        self.run_scenario("units")

    def test_media_rules_can_activate_and_deactivate_after_parse(self):
        self.run_scenario("media")

    def test_nested_media_requires_every_condition(self):
        self.run_scenario("nested")

    def test_inheritance_and_pseudo_styles_follow_resize(self):
        self.run_scenario("pseudo")

    def test_unchanged_viewport_does_not_restyle_or_reparse(self):
        self.run_scenario("stable")

    def test_documents_keep_independent_viewports(self):
        self.run_scenario("independent")

    def test_resolution_and_color_scheme_invalidate_style(self):
        self.run_scenario("environment")

    def test_initially_matching_orientation_rule_can_stop_matching(self):
        self.run_scenario("orientation")

    def test_inactive_media_does_not_eagerly_load_resources(self):
        self.run_scenario("resources")

    def test_new_stylesheet_conditions_refresh_at_unchanged_viewport(self):
        self.run_scenario("late-sheet")

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.executable), scenario], capture_output=True,
                                text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class ViewportWiringTests(unittest.TestCase):
    def test_render_updates_styles_before_layout(self):
        source = (ROOT / "src/app/main.c").read_text()
        section = source[source.index("if (a->t->relayout || !a->t->cur->L)"):]
        self.assertLess(section.index("page_viewport("), section.index("layout_run("))

    def test_sync_restyle_checks_viewport_before_dom_version_guard(self):
        source = (ROOT / "src/app/main.c").read_text()
        section = source[source.index("static void restyle(App *a) {"):]
        self.assertLess(section.index("page_viewport("), section.index("p->seen_ver"))

    def test_frame_updates_styles_before_layout(self):
        source = (ROOT / "src/app/main.c").read_text()
        section = source[source.index("static void frame_layout(Page *p) {"):]
        self.assertLess(section.index("page_viewport("), section.index("layout_run("))


if __name__ == "__main__":
    unittest.main()
