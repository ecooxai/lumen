import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_android_density import function


ROOT = Path(__file__).resolve().parents[1]


class AndroidFocusAppTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        work = Path(cls.temporary.name)
        source = (ROOT / 'src/app/main.c').read_text()
        signature = 'static void android_reveal_focus('
        implementation = function(source, signature) if signature in source else (
            'static void android_reveal_focus(App *app) { (void)app; }\n')
        header = ROOT / 'src/app/android_focus.h'
        if header.exists():
            implementation = f'#include "{header}"\n' + implementation
        (work / 'focus_app_functions.h').write_text(implementation)
        cls.executable = work / 'codex-agent-android-focus-app'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                        '-Wno-unused-function', '-Wno-missing-field-initializers', '-Werror',
                        '-fsanitize=undefined,float-cast-overflow',
                        '-fno-sanitize-recover=all', '-I', str(work),
                        str(ROOT / 'tests/android/focus_app_model.c'), '-lm',
                        '-o', str(cls.executable)], check=True, capture_output=True,
                       text=True, timeout=60)

    def scenario(self, name):
        result = subprocess.run([str(self.executable), name], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_pending_focus_reveals_and_invalidates_cached_page(self):
        self.scenario('pending')

    def test_unrequested_reveal_does_not_override_manual_scroll(self):
        self.scenario('manual')

    def test_address_bar_does_not_scroll_document(self):
        self.scenario('address')

    def test_missing_or_removed_focus_consumes_request_safely(self):
        self.scenario('removed')

    def test_contenteditable_selection_box_is_revealed(self):
        self.scenario('editable')

    def test_selection_outside_editable_is_ignored(self):
        self.scenario('foreign-selection')


if __name__ == '__main__':
    unittest.main()
