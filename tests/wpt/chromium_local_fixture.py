"""Route only the validated local WPT fixture around an inherited browser proxy.

External HTTP/HTTPS requests keep exactly the inherited proxy endpoints. This
helper never modifies os.environ, system proxy settings, DNS or trust stores.
A process-only bypass is restricted to exact reserved WPT names already mapped
to127.0.0.1 and the ports configured on the owned loopback fixture server.
"""
import hashlib
import json
from urllib.parse import urlsplit


def fixture_proxy_arguments(environment, fixture, server):
    if server.get('server_host') != '127.0.0.1' or not server.get('bind_address'):
        raise ValueError('Local fixture routing requires the bound127.0.0.1 server')
    native = fixture['nativeEnvironment']
    if native.get('LUMEN_HOST_MAP_MODE') != 'exact':
        raise ValueError('Local fixture routing requires exact hostname mapping')
    hosts = []
    for entry in native['LUMEN_HOST_MAP'].split(','):
        host, address = entry.split('=', 1)
        allowed = any(host == suffix or host.endswith('.' + suffix)
                      for suffix in ('web-platform.test', 'not-web-platform.test'))
        if not allowed or any(c in host for c in '*;/:?@') or address != '127.0.0.1':
            raise ValueError('Only exact reserved WPT names mapped to127.0.0.1 are allowed')
        hosts.append(host)
    if not hosts or len(set(hosts)) != len(hosts):
        raise ValueError('Empty or duplicate fixture hostname mapping')
    ports = sorted({port for values in server['ports'].values() for port in values if port is not None})
    if not ports or any(type(port) is not int or not 1 <= port <= 65535 for port in ports):
        raise ValueError('Fixture ports must be explicit valid local TCP/UDP numbers')
    proxy = {}
    for scheme in ('http', 'https'):
        value = environment.get(scheme + '_proxy', environment.get(scheme.upper() + '_PROXY'))
        if value:
            parsed = urlsplit(value)
            if (parsed.scheme not in ('http', 'https') or not parsed.hostname or
                    parsed.username is not None or parsed.password is not None or
                    parsed.path not in ('', '/') or parsed.query or parsed.fragment):
                raise ValueError('Unsupported inherited proxy selector; do not expose or replace credentials')
            proxy[scheme] = parsed.scheme + '://' + parsed.netloc
    if not proxy:
        return [], {'enabled': False, 'reason': 'No inherited HTTP/HTTPS proxy; original direct configuration retained'}
    if set(proxy) != {'http', 'https'} or environment.get('all_proxy') or environment.get('ALL_PROXY'):
        raise ValueError('Local override cannot safely represent this inherited proxy configuration')
    original = environment.get('no_proxy', environment.get('NO_PROXY', ''))
    original_items = [part.strip() for part in original.split(',') if part.strip()]
    exceptions = [host + ':' + str(port) for host in sorted(hosts) for port in ports]
    args = ['--proxy-server=' + ';'.join(scheme + '=' + proxy[scheme] for scheme in ('http', 'https')),
            '--proxy-bypass-list=' + ';'.join(original_items + exceptions)]
    record = {'enabled': True, 'scope': 'Exact loopback WPT fixture only; inherited external HTTP/HTTPS proxy endpoints retained',
              'proxyConfigurationSHA256': hashlib.sha256(json.dumps(proxy, sort_keys=True).encode()).hexdigest(),
              'inheritedBypassSHA256': hashlib.sha256(original.encode()).hexdigest(),
              'localHosts': sorted(hosts), 'localPorts': ports, 'localExceptionCount': len(exceptions),
              'argumentsSHA256': hashlib.sha256(json.dumps(args).encode()).hexdigest()}
    return args, record
