#!/usr/bin/env python3
"""Independent tokenizer EOF semantic and split-feed regression under sanitizers."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, default=Path('output/session-102900614/html-eof-semantics-1'))
args = parser.parse_args()
root = Path.cwd()
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
fixture = Path('tests/test_html_eof_semantics.c')
if sha(fixture) != 'b29d14055d187c6845493672e9c231daf265a4e31f1abef741ad483ebfc95136':
    raise SystemExit('Frozen native boundary fixture changed')
if sha('src/html/html.c') != '1863a73e2815a5da5c4e346366a08a3846458538d93e3709aa631d2376e9cf09':
    raise SystemExit('The reviewed HTML metadata source changed')
inputs = [str(fixture), 'src/html/html.c', 'src/html/entities.c', 'src/dom/dom.c', 'src/dom/dom.h',
          'src/dom/control.c', 'src/dom/control.h', 'src/base/url.c', 'src/base/url.h',
          'src/base/encoding.c', 'src/base/encoding.h', 'src/base/encoding_decode_data.inc',
          'src/base/encoding_encode_index.inc', 'src/base/util.c', 'src/base/util.h']
inputs.extend(str(p) for p in sorted(Path('src/html').iterdir()) if p.suffix in {'.inc', '.h'} and str(p) not in inputs)
manifest = {name: sha(name) for name in inputs}
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
(out / 'source-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
start = time.monotonic()
utc = lambda: datetime.now(timezone.utc).isoformat()
summary = {'startedUTC': utc(), 'sourceBaseCommit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
           'fixtureSHA256': sha(fixture), 'sourceManifestSHA256': sha(out / 'source-manifest.json'),
           'stages': [], 'passed': False, 'scope': 'Native byte parser bounds and semantic controls, not full HTML conformance.'}

def measured(name, command, seconds):
    prefix = out / name
    began = time.monotonic()
    started = utc()
    run = subprocess.run([sys.executable, 'tools/monitor_process.py', '--duration', str(seconds), '--settle', '0',
                          '--output', str(prefix.relative_to(root)), '--', *command],
                         capture_output=True, text=True, timeout=seconds + 40)
    (out / (name + '-collector.log')).write_text(run.stdout + run.stderr)
    monitor = json.loads(prefix.with_suffix('.json').read_text())
    log = prefix.with_suffix('.log').read_text(errors='replace')
    passed = monitor.get('natural_exit_code') == 0 and not monitor.get('ended_by_monitor') and not monitor.get('collector_error')
    row = {'stage': name, 'command': command, 'startedUTC': started, 'finishedUTC': utc(),
           'durationSeconds': round(time.monotonic() - began, 3), 'passed': passed,
           'monitor': str(prefix.with_suffix('.json').relative_to(root)),
           'naturalExitCode': monitor.get('natural_exit_code'), 'endedByMonitor': monitor.get('ended_by_monitor'),
           'intervalCpuSamples': monitor.get('interval_cpu_samples'), 'intervalCpuPeakPercent': monitor.get('interval_cpu_peak_percent'),
           'rssPeakMiB': monitor.get('rss_peak_mb')}
    summary['stages'].append(row)
    print(json.dumps(row), flush=True)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    return passed, log

binary = out / 'test_html_eof_semantics_asan'
command = ['clang', '-std=c11', '-D_DARWIN_C_SOURCE', '-g', '-O1', '-UNDEBUG',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
           '-I/opt/homebrew/opt/icu4c/include', '-L/opt/homebrew/opt/icu4c/lib',
           str(fixture), 'src/html/html.c', 'src/html/entities.c', 'src/dom/dom.c', 'src/dom/control.c',
           'src/base/url.c', 'src/base/encoding.c', 'src/base/util.c', '-licuuc', '-lm', '-o', str(binary)]
built, _ = measured('build', command, 120)
if built:
    summary['artifact'] = {'path': str(binary.relative_to(root)), 'sha256': sha(binary), 'bytes': binary.stat().st_size}
    tested, log = measured('bounds', ['env', 'ASAN_OPTIONS=abort_on_error=1:detect_leaks=0',
                                    'UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1', str(binary)], 60)
    matches = re.findall(r'^RESULTS html-eof-semantics PASS cases=(\d+) splitChecks=(\d+)$', log, re.M)
    summary['results'] = dict(zip(['cases', 'splitChecks'], map(int, matches[0]))) if len(matches) == 1 else None
    summary['passed'] = tested and len(matches) == 1
for name, digest in manifest.items():
    if sha(name) != digest:
        raise SystemExit('A compiled input changed during the sanitizer supplement: ' + name)
summary.update(finishedUTC=utc(), durationSeconds=round(time.monotonic() - start, 3))
(out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary), flush=True)
raise SystemExit(0 if summary['passed'] else 1)
