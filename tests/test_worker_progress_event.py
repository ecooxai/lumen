#!/usr/bin/env python3
"""Run public ProgressEvent conversion checks in a real native Lumen worker.

This custom regression is separate from upstream WPT. Run this driver under the
one-second process monitor when collecting performance evidence.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'tests/test_worker_progress_event.html'
MARKER = 'WORKER_PROGRESS_EVENT_RESULTS '


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--runtime-environment', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Preserve previous evidence; select a new output path')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    binary = args.binary.resolve(strict=True)
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('LUMEN_', 'JSRUN_'))}
    if args.runtime_environment:
        runtime = json.loads(args.runtime_environment.read_text())
        if set(runtime) - {'LUMEN_ICU_DATA', 'LD_LIBRARY_PATH'}:
            raise ValueError('Unexpected runtime environment key')
        environment.update(runtime)
    environment['LUMEN_CONSOLE'] = '1'
    begun = time.monotonic()
    result = subprocess.run([str(binary), str(FIXTURE), '2000'], cwd=ROOT,
                            env=environment, capture_output=True, text=True, timeout=12)
    log = result.stdout + result.stderr
    args.output.with_suffix('.log').write_text(log)
    messages = [json.loads(line.split(MARKER, 1)[1]) for line in log.splitlines() if MARKER in line]
    error = None
    rows = []
    if result.returncode or len(messages) != 1 or messages[0].get('error'):
        error = 'Native execution did not produce exactly one successful completion'
    else:
        rows = messages[0].get('rows', [])
        if len(rows) != 16 or len({row['name'] for row in rows}) != 16:
            error = 'Missing or duplicated regression cases'
    receipt = {'kind': 'native dedicated-worker ProgressEvent conversion regression',
               'officialWPT': False, 'binary': str(binary), 'binarySHA256': sha(binary),
               'fixtureSHA256': sha(FIXTURE), 'driverSHA256': sha(__file__),
               'finishedUTC': datetime.now(timezone.utc).isoformat(),
               'durationSeconds': time.monotonic() - begun, 'returnCode': result.returncode,
               'scheduled': 16, 'recorded': len(rows),
               'passed': sum(row.get('passed') is True for row in rows),
               'rows': rows, 'infrastructureError': error}
    args.output.write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({key: receipt[key] for key in ('scheduled', 'recorded', 'passed', 'infrastructureError')}))
    return 1 if error or receipt['passed'] != 16 else 0


if __name__ == '__main__':
    raise SystemExit(main())
