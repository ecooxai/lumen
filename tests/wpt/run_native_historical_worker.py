#!/usr/bin/env python3
"""Unmodified historical worker WPT on the Mac-verified worker-fix snapshot."""
import hashlib
import json
import os
from pathlib import Path
import platform
import sys

assert platform.system() == 'Darwin', 'Execute only on the authorized Mac'
repo = Path('/Users/devin/repos/lumen')
os.chdir(repo)


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


binary = Path('build/lumen-wpt-d48c30587b3f3883').resolve(strict=True)
assert digest(binary) == 'd48c30587b3f3883129d4978cc7e9aa048ddf263734ecf30d904d1ab07fec3e2'
assert not binary.stat().st_mode & 0o222, 'Snapshot must remain read-only'
assert digest('tests/wpt/full_runner.py') == 'be47d18fe8a00001b5b57fbd4dbfe24621485d0c32dcd2d177510db2f026504f'
reference_path = Path('output/wpt/gpt-6-astra-pro-devinmac3d-url-full-v31/lumen/run.json')
reference = json.loads(reference_path.read_text())
fixture = reference['signature']['native_fixture']
assert fixture['host_map_mode'] == 'exact' and fixture['host_map']
ca = repo / 'output/wpt/upstream/tools/certs/cacert.pem'
assert digest(ca) == fixture['ssl_cert_sha256']
assert digest('output/wpt/server.config.json') == '2056ab1f55ad0a37432460ac8f2fab9880c26b74f7036282bd3272d302e91adf'
readiness = Path('output/wpt/fixture-readiness-v3-clean.json')
assert json.loads(readiness.read_text())['fixture_ready'] is True
output = Path('output/wpt/gpt-6-astra-pro-devinmac3d-worker-historical-v33/lumen')
assert not output.exists(), 'Use a fresh output; do not combine binary generations'
for key in ('LUMEN_PRE_FILE', 'LUMEN_SHOT', 'LUMEN_SHOT_MS', 'LUMEN_NO_JS'):
    os.environ.pop(key, None)
os.environ.update(LUMEN_HOST_MAP=fixture['host_map'], LUMEN_HOST_MAP_MODE='exact', SSL_CERT_FILE=str(ca))
command = [sys.executable, 'tests/wpt/full_runner.py', 'run',
           '--inventory', 'output/wpt/inventory.json', '--engine', 'lumen',
           '--binary', str(binary), '--output', str(output),
           '--types', 'testharness', '--prefix', '/url/historical.any.worker.html', '--limit', '1',
           '--origin', 'http://web-platform.test:8000',
           '--secure-origin', 'https://web-platform.test:18443',
           '--h2-origin', 'https://web-platform.test:9000',
           '--timeout', '15', '--long-timeout', '65',
           '--server-config', 'output/wpt/server.config.json',
           '--fixture-manifest', str(readiness), '--strict-infrastructure']
print(json.dumps({'binary_sha256': digest(binary), 'fresh_output': str(output),
                  'scope': 'One unmodified official worker variant; excluded from the separate matched URL cohort'}), flush=True)
os.execv(sys.executable, command)
