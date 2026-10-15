import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_android_density import function

ROOT = Path(__file__).resolve().parents[1]

class YoutubeControlTests(unittest.TestCase):
    def test_visibility_timer_targets_only_youtube_players(self):
        source = (ROOT / 'src/app/main.c').read_text()
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            implementation = '\n'.join(function(source, name) for name in (
                'static bool is_youtube(', 'static bool youtube_controls_target(',
                'static void page_move(', 'static void page_move_keep('))
            (work / 'controls.h').write_text(implementation)
            executable = work / 'controls'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                            '-Werror', '-fsanitize=address,undefined', '-I', str(work),
                            str(ROOT / 'tests/youtube_controls.c'), '-o', str(executable)],
                           check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_android_taps_wake_controls_before_click(self):
        source = (ROOT / 'src/app/main.c').read_text()
        start = source.index('if (touch_tap && a.t->cur && is_youtube')
        self.assertLess(source.index('page_move(&a', start), source.index('click_page(&a', start))
        self.assertIn('ev.button.y > BAR', source[start:source.index('page_move(&a', start)])

if __name__ == '__main__':
    unittest.main()
