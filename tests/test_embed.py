import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class EmbeddedAssetsTest(unittest.TestCase):
    def test_generated_assets_match_sources(self):
        with tempfile.TemporaryDirectory(prefix="codex-lumen-embed-") as temporary:
            output = pathlib.Path(temporary)
            subprocess.run([
                sys.executable, str(ROOT / "tools/generate_embedded.py"),
                "--root", str(ROOT), "--output-dir", str(output),
            ], check=True, capture_output=True)
            expected = b"".join(path.read_bytes() for path in sorted((ROOT / "src/js/prelude").glob("*.js")))
            for path in sorted((ROOT / "src/js/shared").glob("*.js")):
                marker = b"/*@shared " + path.name.encode() + b"*/"
                self.assertEqual(expected.count(marker), 1)
                expected = expected.replace(marker, path.read_bytes())
                self.assertEqual((output / (path.stem + "_shared.inc")).read_bytes(), b'R"JS(' + path.read_bytes() + b')JS"')
            self.assertEqual((output / "prelude.inc").read_bytes(), b'R"JS(' + expected + b')JS"')
            values = (output / "icon.inc").read_text().replace("\n", "").split(",")
            actual = bytes(int(value.strip(), 16) for value in values if value.strip())
            self.assertEqual(actual, (ROOT / "assets/icon/lumen-256.png").read_bytes())

    def test_missing_sources_fail_without_partial_outputs(self):
        with tempfile.TemporaryDirectory(prefix="codex-lumen-embed-") as temporary:
            output = pathlib.Path(temporary) / "output"
            result = subprocess.run([
                sys.executable, str(ROOT / "tools/generate_embedded.py"),
                "--root", temporary, "--output-dir", str(output),
            ], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((output / "prelude.inc").exists())
            self.assertFalse((output / "icon.inc").exists())


if __name__ == "__main__":
    unittest.main()
