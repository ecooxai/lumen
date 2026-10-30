import ast
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

# Execute only the production pure functions; these fixtures never import a
# browser launcher or require the Mac process-monitor dependencies.
SOURCE = Path(__file__).with_name('public_video_native_candidate_case.py')
TREE = ast.parse(SOURCE.read_text())
NAMES = {'need', 'within_bounds', 'sha', 'capture_proof'}
BODY = [node for node in TREE.body if isinstance(node, ast.FunctionDef) and node.name in NAMES]
GLOBALS = {'Path': Path, 'json': json, 'struct': struct, 'RSS_LIMIT_MIB': 4096,
           'DISK_FLOOR_BYTES': 1024 ** 3, 'hashlib': __import__('hashlib')}
exec(compile(ast.Module(body=BODY, type_ignores=[]), str(SOURCE), 'exec'), GLOBALS)
within_bounds = GLOBALS['within_bounds']
capture_proof = GLOBALS['capture_proof']


class NativePublicEvidence(unittest.TestCase):
    def row(self):
        return {'rss_mib': 100, 'free_bytes': 2 * 1024 ** 3, 'monitor_age_s': 0.2, 'active_compilers': 0}

    def test_actual_monitor_sample_accepts_normal_values(self):
        self.assertTrue(within_bounds(self.row()))

    def test_each_resource_boundary_fails_closed(self):
        for change in ({'rss_mib': 4096.01}, {'free_bytes': 1024 ** 3 - 1}, {'monitor_age_s': 6},
                       {'monitor_age_s': -0.001}, {'active_compilers': 1}, {'rss_mib': None}, {'free_bytes': True}):
            with self.subTest(change=change):
                self.assertFalse(within_bounds({**self.row(), **change}))

    def fixture(self, root):
        out = root / 'out/verification'
        out.mkdir(parents=True)
        image = out / 'public-video-lumen-candidate02-native-v3.later.png'
        image.write_bytes(b'\x89PNG\r\n\x1a\n' + struct.pack('>I', 13) + b'IHDR' + struct.pack('>II', 1024, 768))
        diagnostic = out / 'capture.json'
        data = {'path': str(image.relative_to(root)), 'testPID': 123, 'windowID': 55, 'bytes': image.stat().st_size,
                'alpha': 1, 'onScreen': True, 'targetActive': True, 'frontmostMatchesTarget': True}
        diagnostic.write_text(json.dumps(data))
        return image, diagnostic, data

    def test_exact_relative_and_absolute_capture_paths_agree(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            image, diagnostic, data = self.fixture(root)
            self.assertTrue(capture_proof(root, image, diagnostic, 123)['visibleOwnedWindow'])
            data['path'] = str(image)
            diagnostic.write_text(json.dumps(data))
            self.assertTrue(capture_proof(root, image, diagnostic, 123)['visibleOwnedWindow'])

    def test_wrong_window_and_background_evidence_cannot_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            image, diagnostic, data = self.fixture(root)
            for change in ({'testPID': 999}, {'frontmostMatchesTarget': False}, {'onScreen': False},
                           {'bytes': 0}, {'path': 'out/verification/unrelated.png'}):
                with self.subTest(change=change):
                    diagnostic.write_text(json.dumps({**data, **change}))
                    with self.assertRaises(RuntimeError):
                        capture_proof(root, image, diagnostic, 123)

    def test_invalid_and_escaped_images_cannot_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            image, diagnostic, data = self.fixture(root)
            image.write_bytes(b'not PNG')
            with self.assertRaises(RuntimeError):
                capture_proof(root, image, diagnostic, 123)
            image.unlink()
            other = root / 'elsewhere.png'
            other.write_bytes(b'not PNG')
            image.symlink_to(other)
            with self.assertRaises(RuntimeError):
                capture_proof(root, image, diagnostic, 123)

    def test_existing_playback_criteria_are_imported_unchanged(self):
        imports = [node for node in TREE.body if isinstance(node, ast.ImportFrom) and node.module == 'public_video_case']
        self.assertEqual(len(imports), 1)
        self.assertEqual({item.name for item in imports[0].names}, {'VideoPipe', 'assess_video', 'URL'})
        self.assertFalse(any(isinstance(node, ast.FunctionDef) and node.name in ('assess_video', 'sanitize_video') for node in TREE.body))


if __name__ == '__main__':
    unittest.main()
