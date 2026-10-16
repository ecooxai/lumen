import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class GpuScrollUploadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.executable = Path(cls.temporary.name) / 'codex-agent-gpu-scroll-upload'
        source = (ROOT / 'src/gpu/gpu.c').read_text()
        start = source.index('bool gpu_present_frame(')
        end = source.index('    bool yv = ', start)
        prefix = source[start:end].replace('bool gpu_present_frame(', 'static bool prepare_present_test(', 1)
        (Path(cls.temporary.name) / 'prepare_present_test.h').write_text(prefix + '    (void)v; return true;\n}\n')
        headers = Path(os.environ.get('LUMEN_TEST_WGPU_HEADERS',
                                     ROOT / 'build/codex-android-wgpu/prefix/include/webgpu'))
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-D__ANDROID__', '-ffunction-sections', '-fdata-sections',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        '-I', cls.temporary.name, '-I', str(ROOT / 'tests/android/gpu_stubs'), '-I', str(headers),
                        str(ROOT / 'tests/android/gpu_scroll_upload_model.c'),
                        '-Wl,--gc-sections', '-o', str(cls.executable)],
                       check=True, capture_output=True, text=True, timeout=60)

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.executable), scenario], capture_output=True,
                                text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_circular_cpu_source_and_gpu_destination_wrap_independently(self):
        self.run_scenario('cpu-ring')
        self.run_scenario('cpu-ring-full')

    def test_video_present_preserves_page_ring_and_draw_order(self):
        self.run_scenario('video')

    def test_video_pixels_upload_once_per_frame_but_geometry_updates(self):
        self.run_scenario('video-cache')

    def test_up_scroll_only_uploads_exposed_rows(self):
        self.run_scenario('up')

    def test_down_scroll_only_uploads_exposed_rows(self):
        self.run_scenario('down')

    def test_reversals_wrap_and_fixed_rows_match_cpu_page(self):
        self.run_scenario('matrix')

    def test_padded_source_never_uploads_padding(self):
        self.run_scenario('padded')

    def test_fixed_damage_merges_with_exposed_strip(self):
        self.run_scenario('fixed')

    def test_full_page_upload_preserves_ring_mapping(self):
        self.run_scenario('full-page')

    def test_reset_restores_linear_sampling_and_clears_metadata(self):
        self.run_scenario('reset')

    def test_regular_partial_upload_restores_full_linear_texture(self):
        self.run_scenario('present-reset')

    def test_no_upload_presentation_preserves_ring(self):
        self.run_scenario('present-skip')

    def test_invalid_inputs_do_not_mutate_gpu_state(self):
        self.run_scenario('invalid')

    def test_missing_or_resized_texture_requires_fallback(self):
        self.run_scenario('unavailable')

    def test_shader_uses_ring_row_mapping_without_implicit_derivatives(self):
        self.run_scenario('shader')


if __name__ == '__main__':
    unittest.main()
