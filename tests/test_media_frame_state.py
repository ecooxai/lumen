import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class MediaFrameStateTests(unittest.TestCase):
    def test_presentation_state(self):
        with tempfile.TemporaryDirectory() as temporary:
            exe = Path(temporary) / 'media-frame-state'
            deps = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl3', 'libavformat', 'libavcodec', 'libavutil', 'libswscale', 'libswresample'], text=True))
            command = [os.environ.get('CC', 'cc'), '-std=gnu11', '-I'+str(ROOT/'src'), '-ffunction-sections', '-fdata-sections',
                       '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g',
                       str(ROOT/'tests/media_frame_state.c'), str(ROOT/'src/paint/image.c'), str(ROOT/'src/base/util.c'),
                       '-Wl,--gc-sections', '-lm', '-lz', '-lpng', '-ljpeg', '-lwebp', '-lgif', '-pthread', '-o', str(exe), *deps]
            build = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())
if __name__ == '__main__': unittest.main()
