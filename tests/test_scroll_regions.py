import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ScrollRegionTests(unittest.TestCase):
    def test_actual_scroll_raster_matches_full_frame(self):
        source = (ROOT / 'src/app/main.c').read_text()
        start = source.index('        if (blit) {', source.index('static void render('))
        end = source.index('        } else if (part && a->sdirty)', start)
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / 'scroll_raster.h').write_text(source[start:end] + '        }')
            exe = work / 'scroll-regions'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                '-Wno-misleading-indentation', '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all', '-I', str(work),
                str(ROOT / 'tests/scroll_regions.c'), str(ROOT / 'src/paint/raster.c'),
                str(ROOT / 'src/base/util.c'), '-lm', '-lz', '-pthread', '-o', str(exe)],
                check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

if __name__ == '__main__':
    unittest.main()
