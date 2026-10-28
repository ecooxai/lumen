#!/usr/bin/env python3
"""Build and verify the Linux SDL capture-refusal test library without devices.

Run from the repository through tools/codex_compile.py. Generated outputs belong
in an ignored project-local directory. The resulting JSON reports the exact
library hash to pin in LUMEN_CAPTURE_GUARD_SHA256; no browser is started here.
"""
import argparse
import ctypes
import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import secrets
import select
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]

def require(condition, message):
    if not condition:
        raise RuntimeError(message)

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'agent/runtime/capture-guard')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    os.chdir(ROOT)
    require(not os.environ.get('LD_PRELOAD') and not os.environ.get('LD_AUDIT'),
            'Run this verification without inherited loader injection')
    sources = [Path('agent/validation/capture_guard.c'), Path('agent/validation/capture_guard_oracle.c')]
    source_hashes = {str(p): digest(p) for p in sources}
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl3'], text=True))
    commands = []
    for source, name, extra in [(sources[0], 'capture_guard.so', ['-shared', '-fPIC']),
                                (sources[1], 'capture_guard_oracle', ['-UNDEBUG'])]:
        command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', *extra,
                   str(source), '-o', str(out / name), *flags, '-ldl', '-Wl,-z,relro,-z,now']
        subprocess.run(command, check=True)
        commands.append(command)
    competitor = out / 'competing.c'
    competitor.write_text('#include <SDL3/SDL.h>\nSDL_Camera *SDL_OpenCamera(SDL_CameraID id,const SDL_CameraSpec *spec){(void)id;(void)spec;return NULL;}\n')
    command = ['cc', '-shared', '-fPIC', str(competitor), '-o', str(out / 'competing.so'), *flags]
    subprocess.run(command, check=True)
    commands.append(command)
    require(source_hashes == {str(p): digest(p) for p in sources}, 'Sources changed during compilation')
    data = (out / 'capture_guard.so').read_bytes()
    library_hash = hashlib.sha256(data).hexdigest()
    manifest = {'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                'commands': commands, 'source_hashes': source_hashes, 'library_sha256': library_hash}
    (out / 'compile.json').write_text(json.dumps(manifest, indent=2) + '\n')
    libc = ctypes.CDLL(None, use_errno=True)
    libc.memfd_create.argtypes = [ctypes.c_char_p, ctypes.c_uint]
    libc.memfd_create.restype = ctypes.c_int
    fd = libc.memfd_create(b'lumen-capture-refusal-oracle', 2)
    require(fd >= 0, 'memfd_create failed: %s' % ctypes.get_errno())
    rows = []
    try:
        view = memoryview(data)
        while view:
            n = os.write(fd, view)
            require(n > 0, 'Short guard-library write')
            view = view[n:]
        fcntl.fcntl(fd, 1033, 15)
        require(fcntl.fcntl(fd, 1034) & 15 == 15, 'Guard library was not sealed')
        for mode in ['ack', 'reject', 'missing-env', 'competing']:
            rd, wr = os.pipe()
            ack_read, ack_write = os.pipe()
            nonce = secrets.token_hex(16)
            env = dict(os.environ, LD_PRELOAD='/proc/self/fd/%d' % fd,
                       SDL_AUDIO_DRIVER='dummy', LUMEN_CAPTURE_GUARD_FD=str(wr),
                       LUMEN_CAPTURE_GUARD_ACK_FD=str(ack_read), LUMEN_CAPTURE_GUARD_NONCE=nonce)
            if mode == 'missing-env':
                env.pop('LUMEN_CAPTURE_GUARD_NONCE')
            if mode == 'competing':
                env['LD_PRELOAD'] = str(out / 'competing.so') + ':' + env['LD_PRELOAD']
            proc = None
            handshake = b''
            try:
                proc = subprocess.Popen([str(out / 'capture_guard_oracle')], env=env,
                                        pass_fds=(fd, wr, ack_read), stdin=subprocess.DEVNULL,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                os.close(wr); wr = None
                os.close(ack_read); ack_read = None
                if mode not in ('missing-env', 'competing'):
                    require(bool(select.select([rd], [], [], 10)[0]), 'Missing constructor handshake')
                    handshake = os.read(rd, 4096)
                    expected = ('LUMEN_CAPTURE_GUARD_V1 %d %s\n' % (proc.pid, nonce)).encode()
                    require(handshake == expected, 'Incorrect PID/nonce handshake')
                    require(proc.poll() is None, 'Oracle main ran before runner ACK')
                    os.write(ack_write, b'A' if mode == 'ack' else b'X')
                os.close(ack_write); ack_write = None
                stdout, stderr = proc.communicate(timeout=15)
                require(proc.returncode == (0 if mode == 'ack' else 126),
                        'Unexpected oracle exit for %s: %s' % (mode, proc.returncode))
                if mode == 'ack':
                    require('capture guard oracle:' in stdout and 'checks passed; dummy playback only' in stdout,
                            'Dummy-only oracle did not report its checks')
                    for category in ['camera', 'recording-device-or-unknown', 'recording-stream-or-unknown']:
                        require(stderr.count('LUMEN_CAPTURE_GUARD_BLOCK ' + category + '\n') == 1,
                                'Missing or repeated activity marker: ' + category)
                else:
                    require(not stdout, 'Rejected startup reached oracle main')
                (out / (mode + '.stdout')).write_text(stdout)
                (out / (mode + '.stderr')).write_text(stderr)
                rows.append({'mode': mode, 'exit': proc.returncode,
                             'handshake': handshake.decode().strip(), 'stdout': stdout.strip()})
            finally:
                if proc is not None and proc.poll() is None:
                    proc.kill(); proc.wait()
                for child_fd in (rd, wr, ack_read, ack_write):
                    if child_fd is not None:
                        os.close(child_fd)
    finally:
        os.close(fd)
    require(digest(out / 'capture_guard.so') == library_hash, 'Library changed during verification')
    require(source_hashes == {str(p): digest(p) for p in sources}, 'Sources changed during verification')
    report = dict(manifest, results=rows, sealed_memfd=True,
                  device_policy='All camera and recording opens refused; playback oracle uses SDL dummy only',
                  library=str(out / 'capture_guard.so'))
    (out / 'preflight.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'library': report['library'], 'library_sha256': library_hash,
                      'report': str(out / 'preflight.json'), 'startup_modes': len(rows)}, indent=2))

if __name__ == '__main__':
    main()
