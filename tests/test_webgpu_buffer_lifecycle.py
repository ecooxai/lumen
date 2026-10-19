#!/usr/bin/env python3
"""Run buffer lifecycle assertions against Lumen/V8 and a real native GPU."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jsrun', default='build/jsrun')
    parser.add_argument('--output', type=Path, default=Path('out/webtech/webgpu-buffer-regression'))
    args = parser.parse_args()
    environment = dict(os.environ)
    flags = environment.get('LUMEN_V8_FLAGS', '')
    environment['LUMEN_V8_FLAGS'] = (flags + ' --expose-gc').strip()
    environment.update(LUMEN_CONSOLE='1', JSRUN_UNTIL_ATTR='data-done')
    command = [sys.executable, 'tools/monitor_process.py', '--duration', '25', '--settle', '0',
               '--output', str(args.output), '--', args.jsrun, 'tests/js/webgpu-buffer-lifecycle.html', '18000']
    run = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=35)
    log_path = args.output.with_suffix('.log')
    text = log_path.read_text(errors='replace') if log_path.exists() else ''
    reports = []
    for line in text.splitlines():
        if 'WEBGPU_BUFFER_RESULT ' in line:
            reports.append(json.loads(line.split('WEBGPU_BUFFER_RESULT ', 1)[1]))
        if '[js log] FAIL ' in line:
            print(line)
    if run.returncode or not reports:
        print('Native GPU regression did not complete: ' + (run.stderr or run.stdout).strip())
        print(text[-1600:])
        return 1
    report = reports[-1]
    print(json.dumps(report))
    args.output.with_suffix('.assertions.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0 if report.get('total', 0) >= 33 and report.get('passed') == report.get('total') and report.get('failed') == 0 and report.get('gcStress') else 1


if __name__ == '__main__':
    raise SystemExit(main())
