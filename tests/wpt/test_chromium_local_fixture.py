import copy
import importlib.util
from pathlib import Path
import unittest
spec = importlib.util.spec_from_file_location('fixture_route', Path(__file__).with_name('chromium_local_fixture.py'))
module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)


class FixtureRoutingTests(unittest.TestCase):
    def values(self):
        return ({'http_proxy': 'http://proxy.local:8889', 'https_proxy': 'http://proxy.local:8889', 'no_proxy': '127.0.0.1,localhost'},
                {'nativeEnvironment': {'LUMEN_HOST_MAP_MODE': 'exact', 'LUMEN_HOST_MAP': 'web-platform.test=127.0.0.1,www.web-platform.test=127.0.0.1'}},
                {'server_host': '127.0.0.1', 'bind_address': True, 'ports': {'http': [18800, 18801], 'https': [18443], 'ws': [None]}})

    def test_preserves_external_proxy_and_scopes_exact_loopback_ports(self):
        values = self.values(); before = copy.deepcopy(values)
        args, record = module.fixture_proxy_arguments(*values)
        self.assertEqual(args[0], '--proxy-server=http=http://proxy.local:8889;https=http://proxy.local:8889')
        self.assertIn('web-platform.test:18801', args[1])
        self.assertNotIn('web-platform.test:443;', args[1])
        self.assertEqual(record['localExceptionCount'], 6)
        self.assertEqual(values, before)

    def test_rejects_nonloopback_mapping(self):
        env, fixture, server = self.values(); fixture['nativeEnvironment']['LUMEN_HOST_MAP'] = 'web-platform.test=192.0.2.1'
        with self.assertRaisesRegex(ValueError, 'mapped'): module.fixture_proxy_arguments(env, fixture, server)

    def test_rejects_external_destination_name(self):
        env, fixture, server = self.values(); fixture['nativeEnvironment']['LUMEN_HOST_MAP'] = 'example.org=127.0.0.1'
        with self.assertRaisesRegex(ValueError, 'reserved'): module.fixture_proxy_arguments(env, fixture, server)

    def test_rejects_wildcard_name(self):
        env, fixture, server = self.values(); fixture['nativeEnvironment']['LUMEN_HOST_MAP'] = '*.web-platform.test=127.0.0.1'
        with self.assertRaisesRegex(ValueError, 'exact'): module.fixture_proxy_arguments(env, fixture, server)

    def test_rejects_unbound_server(self):
        env, fixture, server = self.values(); server['server_host'] = '0.0.0.0'
        with self.assertRaisesRegex(ValueError, 'bound'): module.fixture_proxy_arguments(env, fixture, server)

    def test_rejects_credential_bearing_proxy(self):
        env, fixture, server = self.values(); env['http_proxy'] = 'http://secret:credential@proxy.local:8889'
        with self.assertRaisesRegex(ValueError, 'credentials'): module.fixture_proxy_arguments(env, fixture, server)

    def test_rejects_general_proxy_not_represented_by_http_selectors(self):
        env, fixture, server = self.values(); env['ALL_PROXY'] = 'socks5://proxy.local:1080'
        with self.assertRaisesRegex(ValueError, 'configuration'): module.fixture_proxy_arguments(env, fixture, server)

    def test_does_not_invent_proxy_when_none_inherited(self):
        _, fixture, server = self.values(); args, record = module.fixture_proxy_arguments({}, fixture, server)
        self.assertEqual(args, []); self.assertFalse(record['enabled'])


if __name__ == '__main__': unittest.main()
