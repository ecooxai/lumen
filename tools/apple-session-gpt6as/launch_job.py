#!/usr/bin/env python3
"""Detach a bounded, recorded build/test job from an ephemeral remote shell."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def stop_owned_group(process):
    """Stop only the process group created for this worker's command."""
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker', action='store_true')
    parser.add_argument('--output', required=True)
    parser.add_argument('--timeout', type=int, default=900)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command or not 1 <= args.timeout <= 3600:
        parser.error('a command and a timeout between 1 and 3600 seconds are required')
    output = Path(args.output).resolve()
    if not args.worker:
        output.mkdir(parents=True, exist_ok=False)
        with (output / 'job.log').open('x') as log:
            process = subprocess.Popen(
                [sys.executable, str(Path(__file__).resolve()), '--worker', '--output', str(output),
                 '--timeout', str(args.timeout), '--', *command],
                stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                start_new_session=True, close_fds=True,
            )
        print(json.dumps({'worker_pid': process.pid, 'output': str(output), 'detached': True}))
        return 0
    started = time.monotonic()
    receipt = {'command': command, 'cwd': str(Path.cwd()), 'worker_pid': os.getpid(),
               'started_utc': datetime.now(timezone.utc).isoformat(), 'timeout_seconds': args.timeout}
    (output / 'started.json').write_text(json.dumps(receipt, indent=2) + '\n')
    process = None
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    try:
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, start_new_session=True)
        receipt['child_pid'] = process.pid
        try:
            receipt['exit_code'] = process.wait(timeout=args.timeout)
            receipt['timed_out'] = False
        except subprocess.TimeoutExpired:
            stop_owned_group(process)
            receipt['exit_code'] = 124
            receipt['timed_out'] = True
    except KeyboardInterrupt:
        receipt['exit_code'] = 130
        receipt['interrupted'] = True
    except OSError as error:
        receipt['exit_code'] = 127
        receipt['error'] = str(error)
    finally:
        # Cleanup precedes receipt I/O, which can itself fail on a full disk.
        if process is not None and process.poll() is None:
            stop_owned_group(process)
    receipt['finished_utc'] = datetime.now(timezone.utc).isoformat()
    receipt['duration_seconds'] = round(time.monotonic() - started, 3)
    temporary = output / 'result.tmp'
    temporary.write_text(json.dumps(receipt, indent=2) + '\n')
    temporary.replace(output / 'result.json')
    print(json.dumps({'exit_code': receipt['exit_code'], 'duration_seconds': receipt['duration_seconds']}))
    return receipt['exit_code']

if __name__ == '__main__':
    sys.exit(main())
