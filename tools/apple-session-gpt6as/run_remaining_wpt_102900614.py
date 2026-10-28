#!/usr/bin/env python3
"""Run the six existing nonencoding groups without changing their cohort."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument('--plan', type=Path, required=True)
parser.add_argument('--max-seconds', type=float, default=600)
args = parser.parse_args()
tool = Path('tools/apple-session-gpt6as/run_task_wpt_102900614.py')
plan = json.loads(args.plan.read_text())
if hashlib.sha256(tool.read_bytes()).hexdigest() != plan['runnerToolSHA256']:
    raise SystemExit('The frozen cohort runner changed')
if plan['fullFrozenVariantCount'] != 1515:
    raise SystemExit('The complete frozen inventory is required')
if args.max_seconds <= 0:
    parser.error('--max-seconds must be positive')
utc = lambda: datetime.now(timezone.utc).isoformat()
start = time.monotonic()
receipt = {'startedUTC': utc(), 'cohortSHA256': hashlib.sha256(args.plan.read_bytes()).hexdigest(),
           'binarySHA256': plan['binary']['sha256'], 'scheduled': 1515,
           'budgetSeconds': args.max_seconds, 'stages': [], 'error': None}
destination = args.plan.parent / ('nonencoding-invocation-' + str(time.time_ns()) + '.json')
try:
    for group in ['dom', 'events', 'url', 'storage', 'microtasks', 'headers']:
        remaining = args.max_seconds - (time.monotonic() - start)
        if remaining <= 0:
            receipt['stoppedAfterBudget'] = True
            break
        budget = min(180, remaining)
        command = [sys.executable, str(tool), 'run', '--plan', str(args.plan),
                   '--group', group, '--max-seconds', str(round(budget, 3))]
        began = time.monotonic()
        row = {'group': group, 'startedUTC': utc(), 'budgetSeconds': budget, 'command': command}
        # The underlying runner owns each browser, profile and one-second
        # sampler. A case already in flight may finish its bounded watchdog.
        run = subprocess.run(command, timeout=budget + 90)
        row.update(finishedUTC=utc(), seconds=round(time.monotonic() - began, 3), exitCode=run.returncode)
        receipt['stages'].append(row)
        print(json.dumps({'group': group, 'seconds': row['seconds'], 'exitCode': run.returncode}), flush=True)
        if run.returncode:
            raise RuntimeError('Cohort execution failed for ' + group)
except BaseException as error:
    receipt['error'] = type(error).__name__ + ': ' + str(error)
    raise
finally:
    receipt.update(finishedUTC=utc(), seconds=round(time.monotonic() - start, 3))
    destination.write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'receipt': str(destination), 'seconds': receipt['seconds'], 'error': receipt['error']}), flush=True)
