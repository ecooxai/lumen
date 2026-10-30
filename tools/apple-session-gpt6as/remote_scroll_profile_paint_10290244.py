"""Profile the immutable native candidate during the unchanged SDL scroll fixture.

This is diagnostic: the sampler is allowed to perturb the measured process.
The preceding unprofiled failure remains the performance acceptance baseline.
Only this task-owned derived fixture enables paint/GPU diagnostic logs;
the original acceptance runner and its scheduled workload remain unchanged.
"""
import argparse
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

ROOT = Path('/Users/devin/repos/lumen').resolve()
ACCEPTANCE_RUNNER = ROOT / 'tools/apple-session-gpt6as/remote_native_lifecycle.py'
ACCEPTANCE_RUNNER_SHA = '61f919ee0be6e2a972237d7ed7cf9e71f827ff3d9f6d2a8336cebca9fbd9cfb7'
DIAGNOSTIC_RUNNER_SHA = 'b6d5a3104f0fa8dc6e0797b27f2b66c23063d54ec204fde479a4f737bfa69081'
DIAGNOSTIC_ENVIRONMENT = {'LUMEN_DEBUG_PAINT': '1', 'LUMEN_GPU_STATS': '1'}
ENVIRONMENT_NEEDLE = "        environment = {key: value for key, value in os.environ.items() if not key.startswith('LUMEN_')}\n"
ENVIRONMENT_REPLACEMENT = "        environment = {key: value for key, value in os.environ.items() if not key.startswith('LUMEN_')}\n        environment.update({'LUMEN_DEBUG_PAINT': '1', 'LUMEN_GPU_STATS': '1'})\n        receipt['diagnostic_logging'] = {'LUMEN_DEBUG_PAINT': '1', 'LUMEN_GPU_STATS': '1'}\n        receipt['acceptance_eligible'] = False\n"


def utc():
    return datetime.now(timezone.utc).isoformat()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def executable(pid):
    library = ctypes.CDLL('/usr/lib/libproc.dylib')
    library.proc_pidpath.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_uint32]
    library.proc_pidpath.restype = ctypes.c_int
    buffer = ctypes.create_string_buffer(4096)
    if library.proc_pidpath(pid, buffer, len(buffer)) <= 0:
        raise RuntimeError('Cannot identify selected task executable')
    return Path(os.fsdecode(buffer.value)).resolve()


def birth(pid):
    value = subprocess.check_output(['/bin/ps', '-p', str(pid), '-o', 'lstart='], text=True, timeout=3).strip()
    if not value:
        raise RuntimeError('Selected task PID is absent')
    return value


def interrupted(signum, _frame):
    signal.signal(signum, signal.SIG_IGN)
    raise KeyboardInterrupt(f'Termination requested via signal {signum}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--name', required=True)
    parser.add_argument('--phase-delay', type=float, default=20.0)
    options = parser.parse_args()
    BINARY = Path(options.binary).resolve()
    BINARY_SHA = options.sha256
    if not BINARY.is_relative_to(ROOT / 'build-apple-load-20261029'):
        raise RuntimeError('Expected the task-owned immutable build directory')
    if len(BINARY_SHA) != 64 or any(c not in '0123456789abcdef' for c in BINARY_SHA):
        raise RuntimeError('Expected a full lowercase SHA256')
    if not options.name or len(options.name) > 80 or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in options.name):
        raise RuntimeError('Expected a simple task-output name')
    if not 0 <= options.phase_delay <= 24:
        raise RuntimeError('Profiling must start within the active scrolling interval')
    OUTPUT = ROOT / 'output/session-10290244' / options.name
    CONTROL = OUTPUT.with_name(OUTPUT.name + '-control')
    os.chdir(ROOT)
    if sha(BINARY) != BINARY_SHA or sha(ACCEPTANCE_RUNNER) != ACCEPTANCE_RUNNER_SHA:
        raise RuntimeError('Immutable candidate or fixture runner changed')
    if OUTPUT.exists():
        raise RuntimeError('Diagnostic measurement output already exists')
    CONTROL.mkdir(parents=True, exist_ok=False)
    source = ACCEPTANCE_RUNNER.read_text()
    if source.count(ENVIRONMENT_NEEDLE) != 1:
        raise RuntimeError('Pinned fixture environment hook is ambiguous')
    RUNNER = CONTROL / 'diagnostic_native_lifecycle.py'
    RUNNER.write_text(source.replace(ENVIRONMENT_NEEDLE, ENVIRONMENT_REPLACEMENT))
    if sha(RUNNER) != DIAGNOSTIC_RUNNER_SHA:
        raise RuntimeError('Diagnostic fixture derivation changed unexpectedly')
    started = time.monotonic()
    receipt = {'started_utc': utc(), 'scope': 'Diagnostic owned-PID stack sampling during the identical SDL scroll workload. Profiling can perturb CPU; this run does not replace unprofiled acceptance evidence.',
               'binary_sha256': BINARY_SHA, 'runner_sha256': DIAGNOSTIC_RUNNER_SHA,
               'acceptance_runner_sha256': ACCEPTANCE_RUNNER_SHA, 'diagnostic_environment': DIAGNOSTIC_ENVIRONMENT,
               'acceptance_eligible': False,
               'sample_duration_seconds': 5, 'sample_interval_milliseconds': 1, 'phase_delay_seconds': options.phase_delay, 'issues': []}
    runner = None
    try:
        with (CONTROL / 'runner.log').open('x') as log:
            runner = subprocess.Popen([sys.executable, str(RUNNER), '--scenario', 'scroll', '--binary', str(BINARY), '--sha256', BINARY_SHA, '--output', str(OUTPUT)],
                                      stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, start_new_session=True)
            (CONTROL / 'started.json').write_text(json.dumps({'runner_pid': runner.pid, 'started_utc': receipt['started_utc']}) + '\n')
            selected = None
            scrolling_seen = None
            while time.monotonic() - started < 30 + options.phase_delay:
                if runner.poll() is not None:
                    raise RuntimeError('Scroll fixture ended before its profiling window')
                targets = OUTPUT / 'targets.json'
                if targets.exists():
                    data = json.loads(targets.read_text())
                    if data.get('phase') == 'scrolling':
                        groups = data.get('groups', [])
                        if len(groups) != 1 or groups[0].get('name') != 'lumen' or len(groups[0].get('pids', [])) != 1:
                            raise RuntimeError('Ambiguous owned process target')
                        pid = groups[0]['pids'][0]
                        if type(pid) is not int or pid <= 1 or executable(pid) != BINARY:
                            raise RuntimeError('Refusing to sample a process outside this exact candidate')
                        if scrolling_seen is None:
                            scrolling_seen = time.monotonic()
                            selected = (pid, birth(pid))
                        if time.monotonic() - scrolling_seen >= options.phase_delay:
                            break
                time.sleep(.2)
            if selected is None or scrolling_seen is None or time.monotonic() - scrolling_seen < options.phase_delay:
                raise RuntimeError('No verified steady scrolling window was available')
            pid, process_birth = selected
            if birth(pid) != process_birth or executable(pid) != BINARY:
                raise RuntimeError('Selected PID identity changed before profiling')
            receipt['app_pid'] = pid
            receipt['app_process_start'] = process_birth
            receipt['app_executable'] = str(BINARY)
            profile = CONTROL / 'scroll-stack.txt'
            receipt['profile_started_utc'] = utc()
            sampled = subprocess.run(['/usr/bin/sample', str(pid), '5', '1', '-file', str(profile)],
                                     text=True, capture_output=True, timeout=15)
            receipt['profile_finished_utc'] = utc()
            receipt['sample_exit_code'] = sampled.returncode
            (CONTROL / 'sample-command.log').write_text(sampled.stdout + sampled.stderr)
            if sampled.returncode != 0 or not profile.is_file():
                raise RuntimeError('Owned process sampling was unavailable; inspect sample-command.log')
            receipt['profile_bytes'] = profile.stat().st_size
            receipt['profile_sha256'] = sha(profile)
            if birth(pid) != process_birth or executable(pid) != BINARY:
                raise RuntimeError('Selected PID identity changed during profiling')
            # The active workload up to this point is unchanged. Once the
            # profile is complete, deliberately end the diagnostic fixture;
            # its SIGTERM handler writes an incomplete/failed acceptance
            # receipt and cleans up both application and telemetry sessions.
            receipt['diagnostic_end_requested_utc'] = utc()
            receipt['deliberately_truncated_after_profile'] = True
            os.killpg(runner.pid, signal.SIGTERM)
            runner.wait(timeout=15)
            receipt['runner_exit_code'] = runner.returncode
            receipt['measurement_receipt'] = str((OUTPUT / 'receipt.json').relative_to(ROOT))
            if not (OUTPUT / 'receipt.json').is_file():
                raise RuntimeError('Diagnostic fixture did not write its measurement receipt')
    except BaseException as error:
        receipt['issues'].append(type(error).__name__ + ': ' + str(error))
    finally:
        if runner and runner.poll() is None:
            try:
                os.killpg(runner.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                runner.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(runner.pid, signal.SIGKILL)
                runner.wait(timeout=3)
                receipt['issues'].append('Diagnostic runner needed forced termination; inspect its owned-process cleanup')
        receipt['runner_final_exit_code'] = runner.returncode if runner else None
        receipt['finished_utc'] = utc()
        receipt['duration_seconds'] = round(time.monotonic() - started, 3)
        receipt['binary_sha256_after'] = sha(BINARY)
        receipt['acceptance_runner_sha256_after'] = sha(ACCEPTANCE_RUNNER)
        receipt['diagnostic_runner_sha256_after'] = sha(RUNNER)
        if receipt['acceptance_runner_sha256_after'] != ACCEPTANCE_RUNNER_SHA or receipt['diagnostic_runner_sha256_after'] != DIAGNOSTIC_RUNNER_SHA:
            receipt['issues'].append('A fixture runner changed during the diagnostic run')
        if receipt['binary_sha256_after'] != BINARY_SHA:
            receipt['issues'].append('Candidate changed during diagnostic run')
        (CONTROL / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'receipt': str((CONTROL / 'receipt.json').relative_to(ROOT)), 'issues': receipt['issues']}))
    return 1 if receipt['issues'] else 0


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, interrupted)
    sys.exit(main())
