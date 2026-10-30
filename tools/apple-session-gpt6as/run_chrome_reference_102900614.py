#!/usr/bin/env python3
"""Measure the immutable chrome-cache RED reference with unchanged lifecycle."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

root = Path.cwd().resolve()
directory = root / 'output/session-102900614/chrome-cache-reference-1'
summary = json.loads((directory / 'summary.json').read_text())
if summary.get('fixtureSHA256') != '32515b62aece48fd75fe0246866461c7438a195ba196921faaa64ab6c040feb0':
    raise SystemExit('Reviewed chrome fixture changed')
if not summary.get('stages') or not summary['stages'][0].get('passed'):
    raise SystemExit('Chrome reference did not build')
log = (directory / 'pixel-equivalence.log').read_text()
failures = [line for line in log.splitlines() if line.startswith('FAIL render-overdraw: ')]
if failures != ['FAIL render-overdraw: unchanged browser chrome avoids five redundant raster passes while page pixels change'] or 'RESULTS render-overdraw FAIL (99/100 checks)' not in log:
    raise SystemExit('Chrome reference RED differs from the single expected missing reuse assertion')
sources = json.loads((root / summary['compiledSourceManifest']).read_text())
for source, digest in {
    'src/app/main.c': '820f482562977eb2b3c4d462892250a21549a39ff687c9d6a5a1a19c61795a1a',
    'src/app/chrome_paint.h': '2c1d4f35ae7d73ddb9517b0701e7facb640a4f5484a828eb946dbfcfa28fcd00',
    'src/paint/raster.c': 'ab8490489c5c3f670d903ee0959ded347464b5b3bcc577007db164d0012cedf1',
    'src/gpu/gpu.c': '93f7e48a28806f6ac57e0e3e9c9b3b8f9d5cfa57c20b1328abe57d43e983888a',
}.items():
    if sources.get(source) != digest:
        raise SystemExit('Chrome reference source bytes changed: ' + source)
artifact = summary['artifacts']['lumen']
binary = (root / artifact['path']).resolve()
if not binary.is_relative_to(root):
    raise SystemExit('Reference binary is outside the project')
with binary.open('rb') as stream:
    if hashlib.file_digest(stream, 'sha256').hexdigest() != artifact['sha256']:
        raise SystemExit('Immutable reference binary changed')
runner = root / 'tools/apple-session-gpt6as/remote_native_lifecycle.py'
with runner.open('rb') as stream:
    if hashlib.file_digest(stream, 'sha256').hexdigest() != '61f919ee0be6e2a972237d7ed7cf9e71f827ff3d9f6d2a8336cebca9fbd9cfb7':
        raise SystemExit('Fixed lifecycle changed')
command = [sys.executable, str(runner), '--scenario', 'scroll', '--binary', artifact['path'],
           '--sha256', artifact['sha256'], '--output', 'output/session-102900614/scroll-chrome-reference']
print(json.dumps({'scope': 'Unchanged fixed 115-second reference workload with built-in1Hz telemetry.',
                  'binary': artifact, 'command': command}), flush=True)
raise SystemExit(subprocess.run(command, timeout=150).returncode)
