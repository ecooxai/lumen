#!/usr/bin/env python3
"""Local HTTP/3-only WebTransport echo endpoint. No HTTP/2/TCP fallback.

Run inside a project virtual environment with aioquic installed. A short-lived
ECDSA certificate is generated locally; the browser pins its public SHA-256
certificate fingerprint through serverCertificateHashes. Private keys are never
served by the HTTP lab and must not be committed.
"""
import argparse
import asyncio
import datetime
import ipaddress
import json
import os
import pathlib
import signal
import time

from aioquic.asyncio import QuicConnectionProtocol, serve
from aioquic.h3.connection import H3_ALPN, H3Connection
from aioquic.h3.events import DatagramReceived, HeadersReceived, WebTransportStreamDataReceived
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import ProtocolNegotiated
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

OUTPUT = pathlib.Path(__file__).resolve().parent / 'output'
ORIGINS = set(os.environ.get('LUME_LAB_ORIGINS', 'http://127.0.0.1:8765,http://localhost:8765,https://127.0.0.1:8765,https://localhost:8765').split(','))


def certificate(output=OUTPUT):
    output.mkdir(parents=True, exist_ok=True)
    key_path, cert_path = output / 'wt-key.pem', output / 'wt-cert.pem'
    now = datetime.datetime.now(datetime.timezone.utc)
    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'Lume local WebTransport test')])
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now - datetime.timedelta(minutes=1))
            .not_valid_after(now + datetime.timedelta(days=7))
            .add_extension(x509.SubjectAlternativeName([x509.DNSName('localhost'), x509.IPAddress(ipaddress.ip_address('127.0.0.1'))]), critical=False)
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
            .sign(key, hashes.SHA256()))
    fd = os.open(key_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, 'wb') as handle:
        handle.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    os.chmod(key_path, 0o600)
    cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    manifest = {'certificateHash': list(cert.fingerprint(hashes.SHA256())), 'expiresAt': int((now + datetime.timedelta(days=7)).timestamp() * 1000), 'transport': 'HTTP/3 only'}
    (output / 'wt-certificate.json').write_text(json.dumps(manifest, indent=2))
    return cert_path, key_path


class EchoProtocol(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.http = None
        self.sessions = set()

    def quic_event_received(self, event):
        if isinstance(event, ProtocolNegotiated):
            self.http = H3Connection(self._quic, enable_webtransport=True)
        if self.http is None:
            return
        for message in self.http.handle_event(event):
            if isinstance(message, HeadersReceived):
                headers = dict(message.headers)
                valid = (headers.get(b':method') == b'CONNECT' and headers.get(b':protocol') == b'webtransport'
                         and headers.get(b':path') == b'/echo' and headers.get(b'origin', b'').decode() in ORIGINS)
                response = [(b':status', b'200' if valid else b'403')]
                if valid:
                    self.sessions.add(message.stream_id)
                    response.append((b'sec-webtransport-http3-draft', b'draft02'))
                self.http.send_headers(message.stream_id, response, end_stream=not valid)
            elif isinstance(message, DatagramReceived) and message.stream_id in self.sessions:
                if len(message.data) <= 4096:
                    self.http.send_datagram(message.stream_id, message.data)
                    self.audit('datagram', len(message.data))
            elif isinstance(message, WebTransportStreamDataReceived) and message.session_id in self.sessions:
                if message.stream_id & 2:
                    reply = self.http.create_webtransport_stream(message.session_id, is_unidirectional=True)
                else:
                    reply = message.stream_id
                self._quic.send_stream_data(reply, message.data, end_stream=message.stream_ended)
                self.audit('stream', len(message.data))
        self.transmit()

    @staticmethod
    def audit(kind, length):
        # Evidence logs contain transport and byte counts, never payloads or cookies.
        with (OUTPUT / 'webtransport-events.jsonl').open('a') as handle:
            handle.write(json.dumps({'time': time.time(), 'transport': 'h3-quic', 'event': kind, 'echoBytes': length}) + '\n')


async def main(host, port):
    cert, key = certificate()
    configuration = QuicConfiguration(is_client=False, alpn_protocols=H3_ALPN, max_datagram_frame_size=65536, idle_timeout=30)
    configuration.load_cert_chain(str(cert), str(key))
    server = await serve(host, port, configuration=configuration, create_protocol=EchoProtocol)
    print(f'Lume HTTP/3-only WebTransport echo: https://{host}:{port}/echo', flush=True)
    finished = asyncio.Event()
    loop = asyncio.get_running_loop()
    for signum in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(signum, finished.set)
    try:
        await finished.wait()
    finally:
        server.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=4433)
    args = parser.parse_args()
    asyncio.run(main(args.host, args.port))
