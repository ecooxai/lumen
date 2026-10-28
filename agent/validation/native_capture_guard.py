"""Required hardware-capture refusal for native WPT, including frozen binaries.

The child starts at a private loopback redirect gate. No author URL is released
until the sealed preload proves its constructor/interposition and receives ACK.
"""
import ctypes
import errno
import fcntl
import hashlib
import http.server
import json
import os
from pathlib import Path
import secrets
import select
import shutil
import stat
import struct
import subprocess as _subprocess
import threading
import time

_PROTOCOL = 'LUMEN_CAPTURE_GUARD_V1'
_HASH_CACHE = {}


def _sha(path):
    st = path.stat()
    key = (str(path), st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns)
    if key not in _HASH_CACHE:
        h = hashlib.sha256()
        with path.open('rb') as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                h.update(chunk)
        _HASH_CACHE[key] = h.hexdigest()
    return _HASH_CACHE[key]


def _audit(path, event):
    event = dict(event, utc=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()))
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
    try:
        os.write(fd, (json.dumps(event, sort_keys=True) + '\n').encode())
    finally:
        os.close(fd)


def _binary_check(binary):
    st = binary.stat()
    if not stat.S_ISREG(st.st_mode) or st.st_mode & (stat.S_ISUID | stat.S_ISGID):
        raise RuntimeError('Capture guard requires a regular non-setid native binary')
    try:
        caps = os.getxattr(binary, 'security.capability')
    except OSError as exc:
        if exc.errno not in (errno.ENODATA, errno.ENOTSUP, errno.EOPNOTSUPP):
            raise
        caps = b''
    if caps:
        raise RuntimeError('File capabilities can disable preload; native launch refused')
    with binary.open('rb') as stream:
        header = stream.read(64)
        if header[:6] != b'\x7fELF\x02\x01':
            raise RuntimeError('Capture guard currently supports Linux ELF64 little-endian only')
        phoff = struct.unpack_from('<Q', header, 32)[0]
        phentsize, phnum = struct.unpack_from('<HH', header, 54)
        if phentsize < 56 or phnum > 4096:
            raise RuntimeError('Invalid native ELF program headers')
        dynamic_loader = False
        for index in range(phnum):
            stream.seek(phoff + index * phentsize)
            entry = stream.read(56)
            if len(entry) != 56:
                raise RuntimeError('Truncated native ELF program header')
            dynamic_loader |= struct.unpack_from('<I', entry)[0] == 3
        if not dynamic_loader:
            raise RuntimeError('Native binary has no ELF interpreter; preload cannot be required')


def _sealed_library(library, expected_sha):
    data = library.read_bytes()
    if hashlib.sha256(data).hexdigest() != expected_sha:
        raise RuntimeError('Capture refusal library hash mismatch')
    if data[:6] != b'\x7fELF\x02\x01':
        raise RuntimeError('Capture refusal library is not a supported ELF shared object')
    if hasattr(os, 'memfd_create'):
        fd = os.memfd_create('lumen-capture-refusal', 3)
    else:
        libc = ctypes.CDLL(None, use_errno=True)
        create = libc.memfd_create
        create.argtypes = [ctypes.c_char_p, ctypes.c_uint]
        create.restype = ctypes.c_int
        fd = create(b'lumen-capture-refusal', 3)
        if fd < 0:
            raise OSError(ctypes.get_errno(), 'memfd_create')
    try:
        view = memoryview(data)
        while view:
            written = os.write(fd, view)
            view = view[written:]
        os.lseek(fd, 0, os.SEEK_SET)
        fcntl.fcntl(fd, getattr(fcntl, 'F_ADD_SEALS', 1033), 15)
        if fcntl.fcntl(fd, getattr(fcntl, 'F_GET_SEALS', 1034)) & 15 != 15:
            raise RuntimeError('Capture library memfd sealing failed')
        return fd
    except BaseException:
        os.close(fd)
        raise


def guarded_popen(command, **kwargs):
    """Popen-compatible native launch; exactly [binary, initial_url] is accepted."""
    if not isinstance(command, (list, tuple)) or len(command) != 2:
        raise RuntimeError('Guarded native launch requires exactly one initial URL')
    if kwargs.get('shell') or kwargs.get('executable'):
        raise RuntimeError('Shell/executable overrides are not accepted by the capture guard')
    binary = Path(command[0]).resolve(strict=True)
    original_url = str(command[1])
    if not original_url.startswith(('http://', 'https://', 'about:blank', 'file://')) or any(c in original_url for c in '\r\n'):
        raise RuntimeError('Unsupported guarded native startup URL')
    env = dict(kwargs.pop('env', os.environ))
    if env.get('LD_PRELOAD') or env.get('LD_AUDIT'):
        raise RuntimeError('Inherited preload/audit chains are not accepted for guarded native WPT')
    library_name = env.get('LUMEN_CAPTURE_GUARD_LIBRARY')
    expected_sha = env.get('LUMEN_CAPTURE_GUARD_SHA256', '')
    audit_name = env.get('LUMEN_CAPTURE_GUARD_AUDIT_DIR')
    if not library_name or len(expected_sha) != 64 or not audit_name:
        raise RuntimeError('Native WPT requires an explicitly pinned capture refusal library and audit directory')
    library = Path(library_name).resolve(strict=True)
    audit_root = Path(audit_name).resolve()
    audit_root.mkdir(parents=True, exist_ok=True)
    _binary_check(binary)
    library_fd = _sealed_library(library, expected_sha)
    profile = None
    profile_owned = False
    server = None
    server_started = False
    setup_fds = []
    try:
        nonce = secrets.token_hex(16)
        profile = audit_root / ('profile-' + nonce)
        profile.mkdir()
        profile_owned = True
        (profile / 'data').mkdir()
        (profile / 'config').mkdir()
        log_path = audit_root / 'launches.jsonl'
        ready = threading.Event()
        gate = {'approved': False, 'redirects': 0}
        report = {'binary': str(binary), 'binary_sha256': _sha(binary),
                  'guard_library_sha256': expected_sha, 'target_url': original_url,
                  'nonce_sha256': hashlib.sha256(nonce.encode()).hexdigest(),
                  'profile': str(profile), 'session_restore_disabled': True,
                  'guard_library_seals': 15, 'author_navigation_gated': True}

        class GateHandler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                if self.path != '/' + nonce:
                    self.send_error(403)
                    return
                ready.wait(15)
                if not gate['approved']:
                    self.send_error(503, 'Capture guard not approved')
                    return
                gate['redirects'] += 1
                self.send_response(302)
                self.send_header('Location', original_url)
                self.send_header('Cache-Control', 'no-store')
                self.send_header('Content-Length', '0')
                self.send_header('Connection', 'close')
                self.end_headers()
                _audit(log_path, dict(report, event='author_url_released'))

            def log_message(self, *unused):
                pass

        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), GateHandler)
        server.daemon_threads = True
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()
        server_started = True
        bootstrap = 'http://127.0.0.1:%d/%s' % (server.server_port, nonce)
        read_fd, write_fd = os.pipe()
        setup_fds.extend((read_fd, write_fd))
        ack_read, ack_write = os.pipe()
        setup_fds.extend((ack_read, ack_write))
        env.update(LD_PRELOAD='/proc/self/fd/%d' % library_fd,
                   LUMEN_CAPTURE_GUARD_FD=str(write_fd),
                   LUMEN_CAPTURE_GUARD_ACK_FD=str(ack_read),
                   LUMEN_CAPTURE_GUARD_NONCE=nonce,
                   LUMEN_NO_SESSION='1', XDG_DATA_HOME=str(profile / 'data'),
                   XDG_CONFIG_HOME=str(profile / 'config'))
        pass_fds = tuple(set(kwargs.pop('pass_fds', ())) | {library_fd, write_fd, ack_read})
    except BaseException:
        for fd in setup_fds:
            try:
                os.close(fd)
            except OSError:
                pass
        if server is not None:
            if server_started:
                server.shutdown()
            server.server_close()
        if profile_owned:
            shutil.rmtree(profile, ignore_errors=True)
        os.close(library_fd)
        raise
    proc = None
    try:
        proc = _subprocess.Popen([str(binary), bootstrap], env=env, pass_fds=pass_fds, **kwargs)
        report['pid'] = proc.pid
        _audit(log_path, dict(report, event='started_at_private_gate'))
        os.close(write_fd)
        write_fd = None
        os.close(ack_read)
        ack_read = None
        deadline = time.monotonic() + 10
        received = b''
        while b'\n' not in received and len(received) < 256 and time.monotonic() < deadline:
            readable, _, _ = select.select([read_fd], [], [], min(0.2, max(0, deadline - time.monotonic())))
            if readable:
                chunk = os.read(read_fd, 256 - len(received))
                if not chunk:
                    break
                received += chunk
            elif proc.poll() is not None:
                break
        expected = ('%s %d %s\n' % (_PROTOCOL, proc.pid, nonce)).encode()
        if received != expected:
            raise RuntimeError('Capture refusal constructor handshake failed; author URL withheld')
        os.write(ack_write, b'A')
        gate['approved'] = True
        ready.set()
        _audit(log_path, dict(report, event='constructor_verified_and_acknowledged'))
        proc.lumen_capture_guard = dict(report, audit_log=str(log_path))
    except BaseException as exc:
        ready.set()
        if proc is not None:
            proc.kill()
            proc.wait(timeout=5)
        _audit(log_path, dict(report, event='refused_before_author_navigation', reason=str(exc), redirects=gate['redirects']))
        server.shutdown()
        server.server_close()
        shutil.rmtree(profile, ignore_errors=True)
        raise
    finally:
        for fd in (read_fd, write_fd, ack_read, ack_write, library_fd):
            if fd is not None:
                try:
                    os.close(fd)
                except OSError:
                    pass

    def cleanup():
        status = proc.wait()
        server.shutdown()
        server.server_close()
        shutil.rmtree(profile, ignore_errors=True)
        _audit(log_path, dict(report, event='exited', exit_code=status, redirects=gate['redirects'], profile_removed=not profile.exists()))

    threading.Thread(target=cleanup, daemon=True).start()
    return proc
