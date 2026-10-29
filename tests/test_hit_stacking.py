import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HitStackingTests(unittest.TestCase):
    def test_positioned_targets_and_viewport_rectangles(self):
        with tempfile.TemporaryDirectory() as tmp:
            executable = Path(tmp) / 'hit-stacking'
            sources = [ROOT / 'tests/test_hit_stacking.c', ROOT / 'src/text/font.c', ROOT / 'src/js/jsglue.c']
            for directory in ('base', 'dom', 'html', 'css', 'layout'):
                sources.extend(sorted((ROOT / 'src' / directory).glob('*.c')))
            flags = shlex.split(subprocess.check_output(
                ['pkg-config', '--cflags', '--libs', 'freetype2', 'harfbuzz', 'fontconfig'], text=True))
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-D_GNU_SOURCE',
                            '-fsanitize=undefined', '-fno-sanitize-recover=all',
                            '-I', str(ROOT / 'src'), *map(str, sources), *flags,
                            '-licuuc', '-lpthread', '-lm', '-o', str(executable)], check=True, timeout=90)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
