import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PositionedWidthTests(unittest.TestCase):
    def test_percentages_use_containing_block_not_inset_available_space(self):
        with tempfile.TemporaryDirectory() as tmp:
            executable = Path(tmp) / 'positioned-width'
            sources = [ROOT / 'tests/test_positioned_width.c', ROOT / 'src/text/font.c']
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
