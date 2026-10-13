import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AndroidPageTransferTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        work = Path(cls.temporary.name)
        source = (ROOT / 'src/app/main.c').read_text()
        start = source.index('static void compose_page(')
        end = source.index('\n}\n', start) + 3
        (work / 'compose_page_function.h').write_text(source[start:end])
        start = source.index('    bool direct_page = false;', source.index('static void render('))
        end = source.index('    paint_profile_stage(&profile_frame, PAINT_PRESENT);', start)
        (work / 'page_transfer_function.h').write_text(re.sub(r'\ba\b', 'app', source[start:end]))
        cls.executables = {}
        for android in (False, True):
            executable = work / ('codex-agent-page-transfer-android' if android else 'codex-agent-page-transfer-other')
            command = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-fsanitize=undefined,float-cast-overflow', '-fno-sanitize-recover=all']
            if android:
                command.append('-D__ANDROID__')
            subprocess.run(command + ['-I', str(work), str(ROOT / 'tests/android/page_transfer_model.c'),
                                     '-lm', '-o', str(executable)], check=True, capture_output=True,
                           text=True, timeout=60)
            cls.executables[android] = executable

    def run_scenario(self, scenario, android=True):
        result = subprocess.run([str(self.executables[android]), scenario], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_direct_scroll_does_not_copy_cpu_frame(self):
        self.run_scenario('direct')

    def test_repeated_direct_scroll_keeps_uploading_authoritative_page(self):
        self.run_scenario('repeat')

    def test_partial_damage_after_direct_scroll_restores_entire_cpu_page(self):
        self.run_scenario('damage')

    def test_skipped_paint_restores_stale_frame_before_texture_recreation(self):
        self.run_scenario('skipped')

    def test_full_paint_restores_stale_frame(self):
        self.run_scenario('full')

    def test_popup_excludes_direct_upload_and_composes_after_restore(self):
        self.run_scenario('popup')

    def test_video_excludes_direct_upload(self):
        self.run_scenario('video')

    def test_screenshot_excludes_direct_upload(self):
        self.run_scenario('screenshot')

    def test_declined_direct_upload_restores_frame_before_regular_upload(self):
        self.run_scenario('declined')

    def test_present_failure_restores_frame_before_software_fallback(self):
        self.run_scenario('present-failure')

    def test_absent_software_surface_still_leaves_frame_coherent(self):
        self.run_scenario('no-surface')

    def test_backend_switch_to_software_restores_frame(self):
        self.run_scenario('software')

    def test_resized_page_restores_only_current_dimensions(self):
        self.run_scenario('resize')

    def test_sidebar_uses_normal_composition(self):
        self.run_scenario('sidebar')

    def test_non_android_retains_normal_copy_path(self):
        self.run_scenario('other', android=False)


if __name__ == '__main__':
    unittest.main()
