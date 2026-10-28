#!/usr/bin/env python3
"""Run the unchanged 77 official URL variants on the verified shared-URL worker build."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import sys


def sha256(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def require(condition, reason):
    if not condition:
        raise RuntimeError(reason)


require(platform.system() == 'Darwin', 'Execute only on the authorized Mac')
repo = Path('/Users/devin/repos/lumen')
os.chdir(repo)
binary = repo / 'build/lumen-wpt-b5b2ee8c80685a64'
expected = 'b5b2ee8c80685a644a998b8b2cdbe527a372e6c2d77b408a5d0d657c7e65151a'
require(sha256(binary) == expected and not binary.stat().st_mode & 0o222,
        'The read-only verified native snapshot changed')
runner = repo / 'tests/wpt/full_runner.py'
require(sha256(runner) == '0da9eda073345d9aeb053f8a8eda21de632eb5e5078a00d0b01b029fcee4ffda',
        'The Mac-gated runner changed')
inventory_path = repo / 'output/wpt/inventory.json'
require(sha256(inventory_path) == '9ffd9444520fd3158b7566f37600b0f10b56bbded1ccbd9b1b1ed8166a836053',
        'The pinned WPT inventory changed')
spec = importlib.util.spec_from_file_location('lumen_wpt_guard', runner)
core = importlib.util.module_from_spec(spec)
spec.loader.exec_module(core)
inventory = json.loads(inventory_path.read_text())
selection, descriptor = core.select_schedule(inventory, 'testharness', ['/url/'])
previous_dir = repo / 'output/wpt/gpt-6-astra-pro-devinmac3d-url-full-v31/lumen'
previous = json.loads((previous_dir / 'run.json').read_text())
prior_schedule = core.verified_schedule(previous_dir, previous['signature'])
require(len(selection) == 77 and selection == prior_schedule,
        'The actual selected schedule differs from the earlier complete 77-case URL schedule')
require(previous['signature']['inventory_sha256'] == sha256(inventory_path),
        'The prior URL schedule used a different inventory')
fixture = previous['signature']['native_fixture']
require(fixture['host_map_mode'] == 'exact' and fixture['host_map'], 'Missing exact native DNS mapping')
ca = repo / 'output/wpt/upstream/tools/certs/cacert.pem'
require(sha256(ca) == fixture['ssl_cert_sha256'], 'Validated CA bytes changed')
config = repo / 'output/wpt/server.config.json'
require(sha256(config) == '2056ab1f55ad0a37432460ac8f2fab9880c26b74f7036282bd3272d302e91adf',
        'Validated fixture configuration changed')
readiness = repo / 'output/wpt/fixture-readiness-v3-clean.json'
require(json.loads(readiness.read_text()).get('fixture_ready') is True, 'Fixture readiness is absent')
output = repo / 'output/wpt/gpt-6-astra-pro-devinmac3d-url-native-shared-worker-v34/lumen'
launch = output.parent.with_name(output.parent.name + '.launch.json')
require(not output.parent.exists() and not launch.exists(), 'Use a fresh output for this binary generation')
for key in ('LUMEN_PRE_FILE', 'LUMEN_SHOT', 'LUMEN_SHOT_MS', 'LUMEN_NO_JS'):
    os.environ.pop(key, None)
os.environ.update(LUMEN_HOST_MAP=fixture['host_map'], LUMEN_HOST_MAP_MODE='exact', SSL_CERT_FILE=str(ca))
command = [sys.executable, str(runner), 'run', '--inventory', str(inventory_path),
           '--engine', 'lumen', '--binary', str(binary), '--output', str(output),
           '--types', 'testharness', '--prefix', '/url/',
           '--origin', 'http://web-platform.test:8000', '--secure-origin', 'https://web-platform.test:18443',
           '--h2-origin', 'https://web-platform.test:9000', '--timeout', '15', '--long-timeout', '65',
           '--server-config', str(config), '--fixture-manifest', str(readiness), '--strict-infrastructure']
with launch.open('x') as destination:
    json.dump({'argv': command, 'cwd': str(repo), 'binary_sha256': expected,
               'launcher_sha256': sha256(__file__), 'schedule_sha256': core.digest(selection),
               'selection': descriptor, 'previous_run': str(previous_dir),
               'scope': 'All 77 unchanged official URL variants; native-only, no new matched Chromium rate'},
              destination, indent=2)
print(json.dumps({'fresh_output': str(output), 'binary_sha256': expected, 'selected_files': len(selection),
                  'schedule_sha256': core.digest(selection)}), flush=True)
os.execv(sys.executable, command)
