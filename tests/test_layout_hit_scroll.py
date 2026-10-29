import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class LayoutHitScrollTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        work = Path(cls.temporary.name)
        source = (ROOT / 'src/layout/block.c').read_text()
        start = source.index('static int hit_layer(')
        end = source.index('\nvoid layout_dump(', start)
        implementation = source[start:end]
        explicit = 'float scroll_x, float scroll_y' in implementation
        arguments = 'layout, point_x, point_y, scroll_x, scroll_y' if explicit else 'layout, point_x, point_y'
        wrapper = ('\nstatic Box *actual_hit(Layout *layout, float point_x, float point_y, '
                   'float scroll_x, float scroll_y) {\n'
                   '    (void)scroll_x; (void)scroll_y;\n'
                   f'    return layout_hit({arguments});\n}}\n')
        (work / 'layout_hit_functions.h').write_text(implementation + wrapper)
        cls.executable = work / 'codex-agent-layout-hit-scroll'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-missing-field-initializers', '-fsanitize=undefined,float-cast-overflow',
                        '-fno-sanitize-recover=all', '-I', str(work),
                        str(ROOT / 'tests/android/layout_hit_scroll_model.c'),
                        '-lm', '-o', str(cls.executable)], check=True, capture_output=True,
                       text=True, timeout=60)

    def scenario(self, name):
        result = subprocess.run([str(self.executable), name], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fixed_button_at_top(self):
        self.scenario('top')

    def test_fixed_button_after_scroll(self):
        self.scenario('scrolled')

    def test_fixed_footer_after_scroll(self):
        self.scenario('footer')

    def test_fixed_button_with_horizontal_scroll(self):
        self.scenario('horizontal')

    def test_forward_reverse_changes_before_any_paint(self):
        self.scenario('reverse')

    def test_document_content_keeps_document_coordinates(self):
        self.scenario('content')

    def test_scroller_inside_fixed_header(self):
        self.scenario('nested')

    def test_fixed_button_pointer_events_none(self):
        self.scenario('pointer-none')

    def test_empty_layout(self):
        self.scenario('empty')


if __name__ == '__main__':
    unittest.main()
