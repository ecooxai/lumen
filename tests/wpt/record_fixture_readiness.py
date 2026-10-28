#!/usr/bin/env python3
"""Record real local WPT endpoint checks on the authorized Mac host.

This is fixture evidence, not a browser conformance result. QUIC/H3 checks only
the certificate-verified transport handshake and ALPN, not a WebTransport
session. The aioquic public asyncio/configuration/event APIs are documented at
https://aioquic.readthedocs.io/en/latest/asyncio.html and /quic.html.
"""
import argparse
import asyncio
import base64
from datetime import datetime, timezone
import hashlib
import http.client
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import socket
import ssl
import struct
import subprocess
import sys
import time


MAX_REPORTER_BYTES = 1024 * 1024
TIMEOUT = 6


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def tls_context(ca, protocols):
    context = ssl.create_default_context(cafile=str(ca))
    context.check_hostname = True
    context.verify_mode = ssl.CERT_REQUIRED
    context.set_alpn_protocols(protocols)
    return context


def tls_details(connection):
    return {'tls_version': connection.version(), 'cipher': connection.cipher()[0],
            'alpn': connection.selected_alpn_protocol(),
            'peer_certificate_sha256': hashlib.sha256(connection.getpeercert(binary_form=True)).hexdigest(),
            'certificate_verification': 'CERT_REQUIRED with pinned WPT CA and canonical hostname'}


def reporter_probe(host, port, secure, ca, expected_hash):
    connection = http.client.HTTPConnection('127.0.0.1', port, timeout=TIMEOUT)
    details = {}
    try:
        if secure:
            raw = socket.create_connection(('127.0.0.1', port), timeout=TIMEOUT)
            try:
                connection.sock = tls_context(ca, ['http/1.1']).wrap_socket(raw, server_hostname=host)
            except Exception:
                raw.close()
                raise
            details.update(tls_details(connection.sock))
        connection.request('GET', '/resources/testharnessreport.js',
                           headers={'Host': host + ':' + str(port), 'Connection': 'close'})
        response = connection.getresponse()
        payload = response.read(MAX_REPORTER_BYTES + 1)
        if len(payload) > MAX_REPORTER_BYTES:
            raise ValueError('Reporter response exceeded the one-MiB verification limit')
        actual_hash = hashlib.sha256(payload).hexdigest()
        if response.status != 200:
            raise ValueError('Reporter endpoint returned HTTP ' + str(response.status))
        if actual_hash != expected_hash:
            raise ValueError('Reporter SHA256 mismatch: ' + actual_hash)
        details.update(http_status=response.status, reporter_sha256=actual_hash,
                       response_bytes=len(payload), host_header=host + ':' + str(port))
        return details
    finally:
        connection.close()


def h2_probe(host, port, ca):
    with socket.create_connection(('127.0.0.1', port), timeout=TIMEOUT) as raw:
        with tls_context(ca, ['h2']).wrap_socket(raw, server_hostname=host) as connection:
            details = tls_details(connection)
            if details['alpn'] != 'h2':
                raise ValueError('TLS peer did not negotiate h2: ' + repr(details['alpn']))
            return details


def websocket_probe(host, port, secure, ca, path, origin):
    connection = socket.create_connection(('127.0.0.1', port), timeout=TIMEOUT)
    details = {}
    try:
        if secure:
            connection = tls_context(ca, ['http/1.1']).wrap_socket(connection, server_hostname=host)
            details.update(tls_details(connection))
        key = base64.b64encode(os.urandom(16)).decode('ascii')
        request = ('GET ' + path + ' HTTP/1.1\r\nHost: ' + host + ':' + str(port) +
                   '\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ' + key +
                   '\r\nSec-WebSocket-Version: 13\r\nOrigin: ' + origin + '\r\n\r\n')
        connection.sendall(request.encode('ascii'))
        header = bytearray()
        deadline = time.monotonic() + TIMEOUT
        while not header.endswith(b'\r\n\r\n'):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('WebSocket upgrade exceeded its total deadline')
            connection.settimeout(remaining)
            part = connection.recv(1)
            if not part:
                raise EOFError('WebSocket endpoint closed before upgrade response')
            header.extend(part)
            if len(header) > 65536:
                raise ValueError('Oversized WebSocket upgrade header')
        lines = bytes(header).decode('latin-1').split('\r\n')
        if len(lines[0].split()) < 2 or lines[0].split()[1] != '101':
            raise ValueError('WebSocket upgrade failed: ' + lines[0][:160])
        headers = {}
        for line in lines[1:]:
            if ':' in line:
                name, value = line.split(':', 1)
                headers[name.lower()] = value.strip()
        expected = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode('ascii')).digest()).decode('ascii')
        if headers.get('sec-websocket-accept') != expected or headers.get('upgrade', '').lower() != 'websocket':
            raise ValueError('WebSocket upgrade response did not authenticate the handshake key')
        # A masked normal-close frame avoids leaving the echo handler open.
        payload = struct.pack('!H', 1000)
        mask = os.urandom(4)
        connection.sendall(bytes((0x88, 0x80 | len(payload))) + mask +
                           bytes(value ^ mask[index % 4] for index, value in enumerate(payload)))
        details.update(http_status=101, path=path, handshake_key_verified=True,
                       application_echo_tested=False)
        return details
    finally:
        connection.close()


async def quic_probe(host, port, ca):
    from aioquic.asyncio.client import connect
    from aioquic.asyncio.protocol import QuicConnectionProtocol
    from aioquic.h3.connection import H3_ALPN, H3Connection
    from aioquic.quic.configuration import QuicConfiguration
    from aioquic.quic.events import ConnectionTerminated, DatagramFrameReceived, HandshakeCompleted, StreamDataReceived

    class ProbeProtocol(QuicConnectionProtocol):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            self.handshake, self.termination = None, None
            self.http = H3Connection(self._quic)
            self.settings_ready = asyncio.Event()

        def quic_event_received(self, event):
            if isinstance(event, HandshakeCompleted):
                self.handshake = {'alpn': event.alpn_protocol,
                                  'session_resumed': event.session_resumed,
                                  'early_data_accepted': event.early_data_accepted}
            if isinstance(event, ConnectionTerminated):
                self.termination = {'error_code': event.error_code, 'reason_phrase': event.reason_phrase}
            if isinstance(event, (StreamDataReceived, DatagramFrameReceived)):
                # HTTP/3 control/QPACK streams are unidirectional. Let H3 parse
                # them instead of the generic asyncio stream adapter, whose
                # StreamWriter destructor tries to send an illegal FIN in1.2.0.
                self.http.handle_event(event)
                if self.http.received_settings is not None:
                    self.settings_ready.set()
                return
            super().quic_event_received(event)

    configuration = QuicConfiguration(is_client=True, alpn_protocols=H3_ALPN,
                                      server_name=host, idle_timeout=TIMEOUT,
                                      verify_mode=ssl.CERT_REQUIRED, max_datagram_frame_size=65536)
    configuration.load_verify_locations(cafile=str(ca))
    loop, callback_errors = asyncio.get_running_loop(), []
    previous_handler = loop.get_exception_handler()

    def callback_error(event_loop, context):
        callback_errors.append(str(context.get('exception') or context.get('message')))
        # Retain diagnostic stderr while ensuring the manifest cannot pass.
        if previous_handler:
            previous_handler(event_loop, context)
        else:
            event_loop.default_exception_handler(context)

    loop.set_exception_handler(callback_error)
    try:
        async with connect('127.0.0.1', port, configuration=configuration,
                           create_protocol=ProbeProtocol, wait_connected=True) as protocol:
            if protocol.handshake is None or protocol.handshake['alpn'] not in H3_ALPN:
                raise ValueError('QUIC handshake did not confirm an HTTP/3 ALPN')
            await protocol.settings_ready.wait()
            details = dict(protocol.handshake, aioquic_version=importlib.metadata.version('aioquic'),
                           sni=host, certificate_verification='CERT_REQUIRED with pinned WPT CA and canonical hostname',
                           http3_settings_received={str(int(key)): value for key, value in protocol.http.received_settings.items()},
                           webtransport_session_tested=False, http3_request_tested=False)
        await asyncio.sleep(0)
        if callback_errors:
            raise RuntimeError('QUIC callback errors: ' + '; '.join(callback_errors))
        if protocol.termination and protocol.termination['error_code'] != 0:
            raise RuntimeError('QUIC cleanup ended with protocol error: ' + str(protocol.termination))
        details['connection_cleanup'] = protocol.termination
        return details
    finally:
        loop.set_exception_handler(previous_handler)


def checked(label, protocol, port, method, callback):
    began = time.monotonic()
    result = {'label': label, 'protocol': protocol, 'port': port,
              'connect_address': '127.0.0.1', 'method': method, 'checked_at': utc_now()}
    try:
        result.update(callback())
        result['status'] = 'PASS'
    except Exception as error:
        result.update(status='FAIL', error=type(error).__name__ + ': ' + str(error)[:800])
    result['duration_ms'] = round((time.monotonic() - began) * 1000)
    return result


def process_identity(pid):
    result = subprocess.run(['ps', '-p', str(pid), '-o', 'lstart='], capture_output=True,
                            text=True, timeout=5, check=False)
    value = result.stdout.strip()
    if result.returncode or not value:
        raise ValueError('Owned WPT server PID is not live: ' + str(pid))
    return value


def verify_server_start(record, pid, current_identity, config_path, config_hash, ports):
    if record.get('pid') != pid or record.get('server_config_sha256') != config_hash or record.get('ports') != ports:
        raise ValueError('Server-start PID, configuration digest or explicit ports do not match this live fixture')
    argv = record.get('argv') or []
    if '--webtransport-h3' not in argv or '--config' not in argv:
        raise ValueError('Server-start command does not establish the required H3/configuration options')
    index = argv.index('--config')
    if index + 1 >= len(argv) or Path(argv[index + 1]).resolve() != config_path.resolve():
        raise ValueError('Server-start command used another configuration file')
    created = datetime.fromisoformat(record['started_at'])
    if created.tzinfo is None:
        raise ValueError('Server creation timestamp must include its time zone')
    # ps lstart is local time with one-second resolution. This verifies the
    # original PID/timestamp record against a current observation; it does not
    # pretend that an original ps identity was captured at server launch.
    observed = datetime.strptime(current_identity, '%a %b %d %H:%M:%S %Y').astimezone(timezone.utc)
    delta = abs((created.astimezone(timezone.utc) - observed).total_seconds())
    if delta > 5:
        raise ValueError('Current PID start time differs from original server creation record by ' + str(delta) + ' seconds')
    return {'verified_at': utc_now(), 'original_pid_and_config_matched': True,
            'original_creation_to_observed_start_delta_s': round(delta, 3),
            'original_ps_start_identity_was_recorded': False,
            'current_ps_start_identity': current_identity}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server-config', type=Path, required=True)
    parser.add_argument('--server-start', type=Path, required=True)
    parser.add_argument('--server-pid', type=int, required=True)
    parser.add_argument('--wpt-root', type=Path, required=True)
    parser.add_argument('--reporter', type=Path, required=True)
    parser.add_argument('--ca-file', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ws-path', default='/echo')
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('Fixture probes must run on the authorized Mac host')
    config = json.loads(args.server_config.read_text())
    server_start = json.loads(args.server_start.read_text())
    host = config['browser_host']
    ports = config['ports']
    required = ('http', 'http-local', 'http-public', 'https', 'https-local', 'https-public',
                'h2', 'ws', 'wss', 'webtransport-h3')
    for scheme in required:
        values = ports.get(scheme)
        if not isinstance(values, list) or not values or any(type(port) is not int or not 0 < port < 65536 for port in values):
            parser.error('Required ports must be explicit nonempty integer lists: ' + scheme)
    before_identity = process_identity(args.server_pid)
    config_hash = sha256(args.server_config)
    start_record_hash = sha256(args.server_start)
    server_identity_check = verify_server_start(server_start, args.server_pid, before_identity,
                                                args.server_config, config_hash, ports)
    reporter_hash = sha256(args.reporter)
    origin = 'http://' + host + ':' + str(ports['http'][0])
    probes = []
    for scheme in ('http', 'http-local', 'http-public', 'https', 'https-local', 'https-public'):
        secure = scheme.startswith('https')
        for index, port in enumerate(ports[scheme]):
            probes.append(checked(scheme + '[' + str(index) + ']', scheme, port,
                                  'Canonical Host reporter GET and exact SHA256' + ('; CA-verified TLS with SNI' if secure else ''),
                                  lambda p=port, s=secure: reporter_probe(host, p, s, args.ca_file, reporter_hash)))
    for port in ports['h2']:
        probes.append(checked('h2', 'h2', port, 'CA-verified TLS with SNI and h2 ALPN; no HTTP/2 request',
                              lambda p=port: h2_probe(host, p, args.ca_file)))
    for scheme in ('ws', 'wss'):
        for port in ports[scheme]:
            probes.append(checked(scheme, scheme, port, 'RFC6455 HTTP101 and challenge response; no echo round trip',
                                  lambda p=port, s=scheme == 'wss': websocket_probe(host, p, s, args.ca_file, args.ws_path, origin)))
    for port in ports['webtransport-h3']:
        probes.append(checked('webtransport-h3 transport', 'webtransport-h3', port,
                              'CA-verified QUIC TLS handshake, SNI, HTTP/3 ALPN and SETTINGS; no WebTransport session',
                              lambda p=port: asyncio.run(asyncio.wait_for(quic_probe(host, p, args.ca_file), timeout=TIMEOUT + 4))))
    final_identity_error = None
    try:
        if process_identity(args.server_pid) != before_identity or sha256(args.server_config) != config_hash or sha256(args.server_start) != start_record_hash:
            final_identity_error = 'Owned server identity or configuration changed during probes'
    except Exception as error:
        final_identity_error = type(error).__name__ + ': ' + str(error)
    revision = subprocess.run(['git', '-C', str(args.wpt_root), 'rev-parse', 'HEAD'], capture_output=True,
                              text=True, timeout=10, check=True).stdout.strip()
    ready = all(probe['status'] == 'PASS' for probe in probes) and final_identity_error is None
    evidence = {'schema_version': 1, 'recorded_at': utc_now(), 'fixture_ready': ready,
                'wpt_revision': revision, 'manifest_sha256': sha256(args.wpt_root / 'MANIFEST.json'),
                'server_config_sha256': config_hash, 'server_start_record_sha256': start_record_hash,
                'server_start_record': server_start, 'server_pid': args.server_pid,
                'server_start_identity': before_identity, 'server_identity_error': final_identity_error,
                'server_identity_verification': server_identity_check,
                'browser_host': host, 'resolved_ports': ports, 'ca_sha256': sha256(args.ca_file),
                'reporter_sha256': reporter_hash, 'recorder_sha256': sha256(Path(__file__)),
                'python_version': platform.python_version(), 'readiness': probes,
                'known_limitations': [
                    'Fixture transport handshakes are not browser conformance or capability results.',
                    'No HTTP/2 request, HTTP/3 request, WebSocket echo, or WebTransport session is validated by this recorder.',
                    'Probes explicitly connect loopback with canonical Host/SNI; system DNS and per-browser host mappings are validated separately.',
                    'Local/public port probes verify listener transport, not private-network-access or address-space semantics.',
                    'This is a point-in-time readiness record; server restarts or configuration changes require a new cohort manifest.']}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + '.tmp')
    temporary.write_text(json.dumps(evidence, indent=2, ensure_ascii=True) + '\n')
    temporary.replace(args.output)
    print(json.dumps({'fixture_ready': ready, 'output': str(args.output),
                      'probes': [{'label': probe['label'], 'status': probe['status'], 'error': probe.get('error')} for probe in probes]}, ensure_ascii=True))
    return 0 if ready else 1


if __name__ == '__main__':
    sys.exit(main())
