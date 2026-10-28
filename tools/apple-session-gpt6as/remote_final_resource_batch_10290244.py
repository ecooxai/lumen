#!/usr/bin/env python3
"""Explicitly launched, serialized final resource evidence; never starts WPT."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

BINARY_SHA = '1b9d43ed19cd616b0029e34fde97050b86c9acbdbda16a0dca5892922df704e9'
RUNNER_SHA = '61f919ee0be6e2a972237d7ed7cf9e71f827ff3d9f6d2a8336cebca9fbd9cfb7'
REVIEW_SHA = '54c8b89a238e3bd411572b4450a1f43070e252b5af7f8e033d619f4d05306bf5'

def utc():
    return datetime.now(timezone.utc).isoformat()

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def save(path, value):
    pending = path.with_suffix('.tmp')
    pending.write_text(json.dumps(value, indent=2) + '\n')
    pending.replace(path)

def busy(root):
    rows = []
    parents = {}
    for line in subprocess.check_output(['ps', '-A', '-o', 'pid=,ppid=,command='], text=True, timeout=5).splitlines():
        parts = line.strip().split(None, 2)
        if len(parts) == 3:
            pid, parent = int(parts[0]), int(parts[1])
            parents[pid] = parent
            rows.append((pid, parts[2]))
    ancestors = {os.getpid()}
    ancestor = os.getpid()
    while parents.get(ancestor, 0) and parents[ancestor] not in ancestors:
        ancestor = parents[ancestor]
        ancestors.add(ancestor)
    blocked = []
    for pid, command in rows:
        if pid in ancestors:
            continue
        first = command.split(None, 1)[0]
        python = bool(re.search(r'(?:^|/)python(?:[0-9.]*)?$', first))
        scripts = bool(re.search(r'(?:run_priority_wpt_10290244|remote_priority_batch_10290244|remote_build_[^ /]+|full_runner|remote_native_lifecycle)\.py(?:\s|$)', command))
        native = bool(re.search(r'(?:^|/)lumen(?:-[^ /]+)?(?:\s|$)', first))
        compiler = bool(re.search(r'(?:^|/)(?:make|gmake|ninja|clang(?:\+\+)?|cc1|ld|xcodebuild)$', first))
        chrome = 'Google Chrome.app/Contents/' in command and ('10290244' in command or str(root) in command)
        if (python and scripts) or native or compiler or chrome:
            blocked.append({'pid': pid, 'command': command[:700]})
    return blocked

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--runner', type=Path, default=Path('tools/apple-session-gpt6as/remote_native_lifecycle.py'))
    parser.add_argument('--adjudicator', required=True, type=Path)
    parser.add_argument('--after-result', required=True, action='append', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--budget', type=float, default=350)
    args = parser.parse_args()
    root = Path.cwd().resolve()
    if str(root) != '/Users/devin/repos/lumen':
        raise RuntimeError('Unexpected repository')
    if not 300 <= args.budget <= 350:
        raise RuntimeError('The resource-only budget must be 300–350 seconds')
    for path in (args.binary, args.runner, args.adjudicator, args.output, *args.after_result):
        (root / path).resolve().relative_to(root)
    for path, expected in ((args.binary, BINARY_SHA), (args.runner, RUNNER_SHA), (args.adjudicator, REVIEW_SHA)):
        if sha(path) != expected:
            raise RuntimeError('Pinned artifact changed: ' + str(path))
    prerequisites = []
    for path in args.after_result:
        result = json.loads(path.read_text())
        if result.get('exit_code') != 0 or result.get('timed_out'):
            raise RuntimeError('Predecessor did not finish normally: ' + str(path))
        prerequisites.append({'path': str(path), 'sha256': sha(path), 'result': result})
    overlap = busy(root)
    if overlap:
        raise RuntimeError('Owned or build workloads are still active: ' + json.dumps(overlap))
    args.output.mkdir(parents=True, exist_ok=False)
    record = {'schema': 1, 'startedUTC': utc(), 'binary': str(args.binary), 'binarySHA256': BINARY_SHA,
              'runnerSHA256': RUNNER_SHA, 'adjudicatorSHA256': REVIEW_SHA, 'budgetSeconds': args.budget,
              'predecessors': prerequisites, 'stages': [], 'issues': [],
              'note': 'Measurement failures remain failures; a high scrolling CPU verdict does not skip the independent navigation scenario.'}
    output_receipt = args.output / 'receipt.json'
    save(output_receipt, record)
    started = time.monotonic()
    deadline = started + args.budget
    child = None

    def stop_child():
        nonlocal child
        if child is None or child.poll() is not None:
            return
        record['issues'].append('Task-owned runner required interruption')
        child.send_signal(signal.SIGTERM)
        try:
            child.wait(timeout=12)
        except subprocess.TimeoutExpired:
            record['issues'].append('Task-owned runner exceeded graceful cleanup deadline')
            child.kill()
            child.wait(timeout=5)

    def interrupted(signum, _frame):
        signal.signal(signum, signal.SIG_IGN)
        raise KeyboardInterrupt('Termination requested via signal ' + str(signum))
    signal.signal(signal.SIGTERM, interrupted)

    try:
        for scenario, allowance in (('scroll', 142), ('navigation', 192)):
            overlap = busy(root)
            if overlap:
                raise RuntimeError('Refusing overlapping workloads: ' + json.dumps(overlap))
            if sha(args.binary) != BINARY_SHA or sha(args.runner) != RUNNER_SHA or sha(args.adjudicator) != REVIEW_SHA:
                raise RuntimeError('Pinned artifact changed before scenario')
            remaining = deadline - time.monotonic()
            if remaining < (120 if scenario == 'scroll' else 170):
                raise RuntimeError('Insufficient remaining budget for unchanged ' + scenario)
            run = args.output / scenario
            command = [sys.executable, str(args.runner), '--scenario', scenario, '--binary', str(args.binary),
                       '--sha256', BINARY_SHA, '--output', str(run)]
            stage = {'scenario': scenario, 'startedUTC': utc(), 'command': command, 'run': str(run)}
            stage_started = time.monotonic()
            record['stages'].append(stage)
            save(output_receipt, record)
            with (args.output / (scenario + '-runner.log')).open('x') as stream:
                child = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=stream, stderr=subprocess.STDOUT)
                try:
                    stage['runnerExitCode'] = child.wait(timeout=min(allowance, deadline - time.monotonic() - 17))
                except subprocess.TimeoutExpired:
                    stop_child()
                    stage['runnerExitCode'] = child.returncode
                    raise RuntimeError('Unchanged ' + scenario + ' runner exceeded bounded time')
            child = None
            review_path = run / 'independent-review.json'
            review_command = [sys.executable, str(args.adjudicator), '--run', str(run), '--sha256', BINARY_SHA,
                              '--runner', str(args.runner), '--output', str(review_path)]
            with (args.output / (scenario + '-review.log')).open('x') as stream:
                review = subprocess.run(review_command, stdout=stream, stderr=subprocess.STDOUT, timeout=min(10, max(1, deadline - time.monotonic())))
            stage['reviewExitCode'] = review.returncode
            stage['review'] = json.loads(review_path.read_text()) if review_path.is_file() else None
            stage['finishedUTC'] = utc()
            stage['wallSeconds'] = round(time.monotonic() - stage_started, 3)
            stage['snapshots'] = {}
            for name in ('receipt.json', 'process-summary.json', 'process-samples.jsonl', 'independent-review.json', 'lumen.log', 'sampler.log'):
                path = run / name
                if path.is_file():
                    stage['snapshots'][name] = {'path': str(path), 'sha256': sha(path), 'bytes': path.stat().st_size}
            save(output_receipt, record)
            print(json.dumps({'scenario': scenario, 'runnerExitCode': stage['runnerExitCode'], 'reviewExitCode': stage['reviewExitCode'],
                              'review': str(review_path), 'wallSeconds': stage['wallSeconds']}), flush=True)
            # Exit 1 is a failed evidence gate, not an instruction to skip the next scenario.
            if stage['runnerExitCode'] not in (0, 1) or review.returncode not in (0, 1) or stage['review'] is None:
                raise RuntimeError('Scenario harness did not produce a valid review')
        record['completedBothScenarios'] = True
    except BaseException as error:
        record['issues'].append(type(error).__name__ + ': ' + str(error))
    finally:
        stop_child()
        record['finishedUTC'] = utc()
        record['wallSeconds'] = round(time.monotonic() - started, 3)
        record['binarySHA256After'] = sha(args.binary)
        if record['binarySHA256After'] != BINARY_SHA:
            record['issues'].append('Binary changed during resource batch')
        record['remainingWorkloads'] = busy(root)
        if record['remainingWorkloads']:
            record['issues'].append('A browser or build workload remains active after the batch')
        record['harnessCompleted'] = bool(record.get('completedBothScenarios')) and not record['issues']
        record['allResourceGatesPassed'] = record['harnessCompleted'] and all((s.get('review') or {}).get('accepted') for s in record['stages'])
        save(output_receipt, record)
    print(json.dumps({'receipt': str(output_receipt), 'harnessCompleted': record['harnessCompleted'],
                      'allResourceGatesPassed': record['allResourceGatesPassed'], 'issues': record['issues']}), flush=True)
    return 0 if record['harnessCompleted'] else 2

if __name__ == '__main__':
    raise SystemExit(main())
