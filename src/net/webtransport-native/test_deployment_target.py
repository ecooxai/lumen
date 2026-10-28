"""Regression fixtures for SDK-independent Apple deployment selection."""
import os
import unittest
from unittest.mock import patch

from build_component import macos_deployment_target


class DeploymentTargetTest(unittest.TestCase):
    def test_default_is_project_minimum_even_with_newer_ambient_sdk_target(self):
        with patch.dict(os.environ, {'MACOSX_DEPLOYMENT_TARGET': '26.5'}):
            self.assertEqual(macos_deployment_target('aarch64-apple-darwin'), '26.0')

    def test_explicit_version_and_patch_are_retained(self):
        self.assertEqual(macos_deployment_target('aarch64-apple-darwin', '26.0'), '26.0')
        self.assertEqual(macos_deployment_target('aarch64-apple-darwin', '26.1.2'), '26.1.2')

    def test_separately_audited_older_arm64_target_requires_explicit_option(self):
        self.assertEqual(macos_deployment_target('aarch64-apple-darwin', '13.0'), '13.0')

    def test_device_and_simulator_do_not_receive_a_macos_default(self):
        for target in ('aarch64-apple-ios', 'aarch64-apple-ios-sim'):
            with self.subTest(target=target):
                self.assertIsNone(macos_deployment_target(target))
                with self.assertRaises(ValueError):
                    macos_deployment_target(target, '26.0')

    def test_invalid_or_out_of_bounds_targets_reject_before_any_build(self):
        for value in ('', '26', '26.00', '26.0-beta', ' 26.0', '26.0\n', '26.0;touch x',
                      '10.15', '26.256', '26.0.256', '65536.0', None, True):
            if value is None:
                continue  # None deliberately selects the reproducible default.
            with self.subTest(value=value), self.assertRaises(ValueError):
                macos_deployment_target('aarch64-apple-darwin', value)


if __name__ == '__main__':
    unittest.main()
