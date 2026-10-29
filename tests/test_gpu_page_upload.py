import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class GpuPageUploadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.executable = Path(cls.temporary.name) / 'codex-agent-gpu-page-upload'
        headers = Path(os.environ.get('LUMEN_TEST_WGPU_HEADERS',
                                     ROOT / 'build/codex-android-wgpu/prefix/include/webgpu'))
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-D__ANDROID__', '-ffunction-sections', '-fdata-sections',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        '-I', str(ROOT / 'tests/android/gpu_stubs'), '-I', str(headers),
                        str(ROOT / 'tests/android/gpu_page_upload_model.c'),
                        '-Wl,--gc-sections', '-o', str(cls.executable)],
                       check=True, capture_output=True, text=True, timeout=60)

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.executable), scenario], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_partial_page_upload_uses_page_origin_and_toolbar_offset(self):
        self.run_scenario('partial')

    def test_padded_page_upload_excludes_trailing_padding(self):
        self.run_scenario('padded')

    def test_bottom_row_upload_stays_within_page(self):
        self.run_scenario('bottom')

    def test_uninitialized_texture_requires_coherent_frame_fallback(self):
        self.run_scenario('missing')

    def test_texture_resize_requires_frame_fallback(self):
        self.run_scenario('resize')

    def test_surface_resize_requires_frame_fallback(self):
        self.run_scenario('surface-resize')

    def test_suspended_renderer_does_not_upload(self):
        self.run_scenario('suspended')

    def test_invalid_geometry_and_null_inputs_do_not_upload(self):
        self.run_scenario('invalid')


if __name__ == '__main__':
    unittest.main()
