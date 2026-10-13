import ast
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class InternalResponsiveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        work = Path(cls.temporary.name)
        source = (ROOT / "src/app/main.c").read_text()
        start = source.index("    sb_puts(&b,", source.index("static char *internal_page("))
        ending = source.index("</style>", start)
        ending = source.index(");", ending)
        html = "".join(ast.literal_eval(literal) for literal in
                       re.findall(r'"(?:[^"\\]|\\.)*"', source[start:ending]))
        cls.stylesheet = work / "internal.css"
        cls.stylesheet.write_text(html.split("<style>", 1)[1].split("</style>", 1)[0])
        cls.executable = work / "codex-internal-responsive"
        sources = [ROOT / "tests/test_internal_responsive.c"]
        for directory in ("base", "dom", "html", "css"):
            sources.extend(sorted((ROOT / "src" / directory).glob("*.c")))
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-D_GNU_SOURCE",
                        "-fsanitize=undefined", "-fno-sanitize-recover=all",
                        "-I", str(ROOT / "src"), *map(str, sources), "-licuuc",
                        "-lm", "-lpthread", "-o", str(cls.executable)],
                       check=True, timeout=90)

    def test_portrait_navigation_and_content_stack(self):
        self.run_scenario("portrait")

    def test_wide_viewport_retains_sidebar(self):
        self.run_scenario("wide")

    def test_rotation_recomputes_responsive_styles_without_dom_changes(self):
        self.run_scenario("rotation")

    def test_phone_workspace_tiles_and_landscape_navigation(self):
        self.run_scenario("phone")

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.executable), str(self.stylesheet), scenario],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
