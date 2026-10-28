import importlib.util
from pathlib import Path
import unittest


spec = importlib.util.spec_from_file_location('chromium_mode_runner', Path(__file__).with_name('full_runner.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class ChromiumModeTests(unittest.TestCase):
    def test_default_retains_existing_headless_profile_and_fixture_flags(self):
        extra = ['--host-resolver-rules=MAP web-platform.test 127.0.0.1',
                 '--ignore-certificate-errors-spki-list=fixture-key']
        before = list(extra)
        args = runner.chromium_launch_arguments('/test/chrome', '/test/owned-profile', extra)
        self.assertEqual(args, ['/test/chrome', '--headless=new', '--remote-debugging-port=0',
                                '--user-data-dir=/test/owned-profile', '--no-first-run',
                                '--no-default-browser-check', '--disable-background-networking',
                                *extra, 'about:blank'])
        self.assertEqual(extra, before)

    def test_headful_changes_only_the_headless_switch(self):
        extra = ['--host-resolver-rules=MAP web-platform.test 127.0.0.1']
        old = runner.chromium_launch_arguments('/test/chrome', '/test/owned-profile', extra)
        visible = runner.chromium_launch_arguments('/test/chrome', '/test/owned-profile', extra, headless=False)
        self.assertEqual(visible, [arg for arg in old if arg != '--headless=new'])
        self.assertIn('--user-data-dir=/test/owned-profile', visible)
        self.assertFalse(any(arg.startswith(('--disable-gpu', '--test-type', '--disable-features'))
                             for arg in visible))

    def test_extra_arguments_cannot_silently_override_the_reported_browser_mode(self):
        for option in ('--headless', '--headless=new', '--headless=false',
                       '-headless', '-headless=new', '-headless=false'):
            for headless in (False, True):
                with self.subTest(option=option, headless=headless):
                    with self.assertRaises(ValueError):
                        runner.chromium_launch_arguments('/test/chrome', '/test/profile',
                                                         [option], headless=headless)

    def test_non_boolean_mode_is_rejected_instead_of_mislabeled(self):
        for value in ('false', 0, 1, None):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    runner.chromium_launch_arguments('/test/chrome', '/test/profile', [], headless=value)


if __name__ == '__main__':
    unittest.main()
