import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_android_density import function


ROOT = Path(__file__).resolve().parents[1]


class AndroidFocusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        work = Path(cls.temporary.name)
        header = ROOT / 'src/app/android_focus.h'
        implementation = (f'#include "{header}"\n' if header.exists() else
                          'static bool android_focus_scroll(Layout *layout, Box *focus, float *scroll) {\n'
                          '    (void)layout; (void)focus; (void)scroll; return false;\n}\n')
        (work / 'focus_functions.h').write_text(implementation)
        cls.executable = work / 'codex-agent-android-focus'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-missing-field-initializers',
                        '-fsanitize=undefined,float-cast-overflow', '-fno-sanitize-recover=all',
                        '-I', str(work), str(ROOT / 'tests/android/focus_model.c'),
                        '-lm', '-o', str(cls.executable)], check=True, capture_output=True,
                       text=True, timeout=60)

    def scenario(self, name):
        result = subprocess.run([str(self.executable), name], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_keyboard_shrink_reveals_lower_input(self):
        self.scenario('keyboard')

    def test_visible_input_does_not_move(self):
        self.scenario('visible')

    def test_input_above_viewport_scrolls_up(self):
        self.scenario('above')

    def test_scroll_clamps_at_document_end(self):
        self.scenario('end')

    def test_nested_scroller_reveals_before_root(self):
        self.scenario('nested')

    def test_nested_scroller_clamp_then_root_reveal(self):
        self.scenario('nested-root')

    def test_reverse_flex_scroller_uses_negative_scroll_top(self):
        self.scenario('reverse')

    def test_fixed_input_never_moves_document(self):
        self.scenario('fixed')

    def test_scroller_inside_fixed_overlay(self):
        self.scenario('fixed-nested')

    def test_absolute_containing_block_bypasses_scroller(self):
        self.scenario('absolute')

    def test_horizontal_nested_scroll(self):
        self.scenario('horizontal')

    def test_oversized_editable_stays_stable(self):
        self.scenario('oversized')

    def test_border_and_translation_are_respected(self):
        self.scenario('translation')

    def test_invalid_geometry_does_not_change_scroll(self):
        self.scenario('invalid')

    def test_reveal_is_idempotent_and_keyboard_dismissal_does_not_jump(self):
        self.scenario('repeat')


class AndroidFocusWiringTests(unittest.TestCase):
    def test_reveal_uses_fresh_layout_before_display_list(self):
        source = (ROOT / 'src/app/main.c').read_text()
        render = function(source, 'static void render(')
        self.assertIn('android_reveal_focus(a)', render)
        self.assertLess(render.index('layout_run('), render.index('android_reveal_focus(a)'))
        self.assertLess(render.index('android_reveal_focus(a)'), render.index('dl_build('))

    def test_resize_and_explicit_focus_schedule_one_shot_reveal(self):
        source = (ROOT / 'src/app/main.c').read_text()
        self.assertIn('focus_reveal = true', function(source, 'static void update_size(App *a)'))
        self.assertIn('focus_reveal = true', function(source, 'static void focus_node('))
        self.assertIn('static void android_reveal_focus(', source)
        reveal = function(source, 'static void android_reveal_focus(')
        self.assertIn('focus_reveal = false', reveal)
        self.assertIn('android_focus_scroll(', reveal)


if __name__ == '__main__':
    unittest.main()
