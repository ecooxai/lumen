#!/usr/bin/env python3
"""Run and restart one explicit binary, sampling only its owned process tree.

Does not open profiles, alter account sessions, send keystrokes, grant permissions,
or close preexisting apps. A launch receipt is not a functional-test pass.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def utc():
    return datetime.now(timezone.utc).isoformat()


def atomic_json(path, value):
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(value, indent=2) + '\n')
    temp.replace(path)


def wait_alive(process, duration):
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        if process.poll() is not None:
            return False
        time.sleep(min(.25, max(0, deadline-time.monotonic())))
    return process.poll() is None


def stop_owned(process):
    if process.poll() is not None:
        return {'already_exited': True, 'returncode': process.returncode, 'forced_kill': False}
    process.terminate()
    try:
        return {'returncode': process.wait(timeout=6), 'forced_kill': False}
    except subprocess.TimeoutExpired:
        process.kill()
        return {'returncode': process.wait(timeout=3), 'forced_kill': True}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True)
    parser.add_argument('--url', required=True)
    parser.add_argument('--iteration', type=int, required=True, choices=range(1,7))
    parser.add_argument('--output', required=True)
    parser.add_argument('--startup-seconds', type=int, default=10)
    parser.add_argument('--idle-seconds', type=int, default=20)
    parser.add_argument('--build-log')
    args=parser.parse_args()
    binary=Path(args.binary).resolve(strict=True)
    output=Path(args.output)
    output.mkdir(parents=True, exist_ok=False)
    if not os.access(binary, os.X_OK):
        parser.error('binary is not executable')
    if not 1 <= args.startup_seconds <= 180 or not 12 <= args.idle_seconds <= 300:
        parser.error('startup seconds must be1..180; idle seconds12..300')
    telemetry=Path(__file__).with_name('telemetry.py')
    digest=hashlib.sha256(binary.read_bytes()).hexdigest()
    receipts={'iteration':args.iteration, 'binary':str(binary), 'sha256':digest,
              'url':args.url,'started_utc':utc(), 'build_log':args.build_log,
              'release_provenance':'review build log separately; filename is not proof',
              'functional_verification':'not performed by this lifecycle runner',
              'launches':[]}
    targets=output/'targets.json'
    samples=output/'process-samples.jsonl'
    atomic_json(targets,{'iteration':args.iteration,'phase':'baseline','groups':[]})
    sampler=subprocess.Popen([sys.executable,str(telemetry),'collect','--targets',str(targets),
                              '--output',str(samples),'--seconds',str(30+2*(args.startup_seconds+args.idle_seconds))],
                             stdout=(output/'sampler.log').open('w'),stderr=subprocess.STDOUT)
    process=None
    interrupted=False
    def terminate(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM,terminate)
    try:
        time.sleep(1.1)
        for launch_index in (1,2):
            log_path=output/f'launch-{launch_index}.log'
            environment=dict(os.environ)
            environment['LUMEN_CONSOLE']='1'
            environment['LUMEN_CPU_DEBUG']='1'
            environment.setdefault('LUMEN_WINDOW','1200x900')
            with log_path.open('w') as log:
                process=subprocess.Popen([str(binary),args.url],env=environment,stdout=log,stderr=subprocess.STDOUT)
                receipt={'launch':launch_index,'kind':'initial' if launch_index==1 else 'restart',
                         'pid':process.pid,'launched_utc':utc(),'log':str(log_path)}
                receipts['launches'].append(receipt)
                atomic_json(targets,{'iteration':args.iteration,'phase':'startup','launch':launch_index,
                                     'groups':[{'name':'lumen','pids':[process.pid]}]})
                receipt['startup_survived']=wait_alive(process,args.startup_seconds)
                if receipt['startup_survived']:
                    atomic_json(targets,{'iteration':args.iteration,'phase':'idle','launch':launch_index,
                                         'groups':[{'name':'lumen','pids':[process.pid]}]})
                    receipt['idle_survived']=wait_alive(process,args.idle_seconds)
                else:
                    receipt['idle_survived']=False
                receipt['stopped_utc']=utc()
                receipt['termination']=stop_owned(process)
                process=None
                atomic_json(targets,{'iteration':args.iteration,'phase':'cleanup','groups':[]})
                atomic_json(output/'release-restart-receipt.json',receipts)
                time.sleep(2)
    except KeyboardInterrupt:
        interrupted=True
    finally:
        if process is not None:
            stop_owned(process)
        if sampler.poll() is None:
            sampler.terminate()
            try:
                sampler.wait(timeout=5)
            except subprocess.TimeoutExpired:
                sampler.kill()
                sampler.wait(timeout=3)
        receipts['finished_utc']=utc()
        receipts['interrupted']=interrupted
        receipts['lifecycle_completed']=not interrupted and len(receipts['launches'])==2 and all(
            item.get('startup_survived') and item.get('idle_survived') and not item['termination']['forced_kill']
            for item in receipts['launches'])
        atomic_json(output/'release-restart-receipt.json',receipts)
        if samples.exists():
            subprocess.run([sys.executable,str(telemetry),'summarize',str(samples),'--output',str(output/'process-summary.json')],check=True)
    print(json.dumps({'receipt':str(output/'release-restart-receipt.json'),
                      'lifecycle_completed':receipts['lifecycle_completed']}))
    return 0 if receipts['lifecycle_completed'] else 1


if __name__=='__main__':
    sys.exit(main())
