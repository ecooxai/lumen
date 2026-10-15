import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class RasterLayerTests(unittest.TestCase):
    def test_partial_layers_match_full_pixels_with_bounded_storage(self):
        with tempfile.TemporaryDirectory() as temporary:
            exe = Path(temporary) / 'layers'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                            '-Wno-misleading-indentation', '-fsanitize=address,undefined',
                            '-fno-sanitize-recover=all', '-g',
                            str(ROOT / 'tests/raster_layers.c'), str(ROOT / 'src/base/util.c'),
                            '-lm', '-lz', '-pthread', '-o', str(exe)], check=True,
                           capture_output=True, text=True, timeout=60)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

if __name__ == '__main__':
    unittest.main()
