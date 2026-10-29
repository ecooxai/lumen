import os
import json
import re
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
                'static void page_move(', 'static void page_touch_controls(App *a, float x, float y) {',
                'static void page_move_keep('))
            (work / 'controls.h').write_text(implementation)
            executable = work / 'controls'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                            '-Werror', '-fsanitize=address,undefined', '-I', str(work),
                            str(ROOT / 'tests/youtube_controls.c'), '-o', str(executable)],
                           check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_mobile_hold_is_deferred_expires_and_extends(self):
        source = (ROOT / 'src/app/main.c').read_text()
        helper = function(source, 'static void page_touch_controls(App *a, float x, float y) {')
        script = helper.split('js_eval(p->js,', 1)[1].split(',\n        "lumen:touch-controls"', 1)[0]
        script = ''.join(json.loads(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"', script))
        result = subprocess.run(['node', str(ROOT / 'tests/youtube_touch_hold.js'), script],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

if __name__ == '__main__':
    unittest.main()
