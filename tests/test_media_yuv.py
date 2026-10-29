import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class MediaYuvTests(unittest.TestCase):
    def test_packing_and_raster_fallback(self):
        with tempfile.TemporaryDirectory() as temporary:
            exe = Path(temporary) / 'media-yuv'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                            '-Wno-misleading-indentation', '-Wno-unused-parameter',
                            '-Wno-missing-field-initializers', '-ffunction-sections', '-fdata-sections',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g',
                            str(ROOT / 'tests/media_yuv.c'), str(ROOT / 'src/paint/image.c'),
                            str(ROOT / 'src/base/util.c'), '-Wl,--gc-sections', '-lm', '-lz',
                            '-pthread', '-o', str(exe)], check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

if __name__ == '__main__':
    unittest.main()
