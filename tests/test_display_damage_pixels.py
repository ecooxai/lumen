import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DisplayDamagePixelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        work = Path(cls.temporary.name)
        source = (ROOT / "src/app/main.c").read_text()
        start = source.index("struct DSig {")
        end = source.index("\nstatic PaintProfile", start)
        (work / "display_signatures.h").write_text(source[start:end])
        start = source.index("            bool dmg =")
        end = source.index("\n        }\n        { struct DSig *t", start)
        (work / "display_damage.h").write_text(source[start:end])
        cls.executable = work / "codex-agent-display-damage-pixels"
        subprocess.run([
            os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra",
            "-Wno-misleading-indentation", "-Wno-missing-field-initializers",
            "-fsanitize=undefined,float-cast-overflow",
            "-fno-sanitize-recover=all", "-I", str(work),
            str(ROOT / "tests/android/display_damage_pixels.c"),
            str(ROOT / "src/paint/raster.c"), str(ROOT / "src/base/util.c"),
            "-lm", "-lz", "-pthread", "-o", str(cls.executable),
        ], check=True, capture_output=True, text=True, timeout=60)

    def scenario(self, name):
        result = subprocess.run([str(self.executable), name], capture_output=True,
                                text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_visible_caret_partial_raster_matches_full_pixels(self):
        self.scenario("visible")

    def test_hidden_caret_no_raster_matches_full_pixels(self):
        self.scenario("hidden")

    def test_clip_changes_and_caret_match_full_pixels(self):
        self.scenario("clip")

    def test_opacity_changes_and_caret_match_full_pixels(self):
        self.scenario("layer")


if __name__ == "__main__":
    unittest.main()
