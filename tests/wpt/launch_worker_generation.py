#!/usr/bin/env python3
"""Launch the verified worker-fix URL pair or complete supported WPT plan."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('scope', choices=('url77', 'full'))
parser.add_argument('--resume', action='store_true')
options = parser.parse_args()
assert platform.system() == 'Darwin', 'Execute only on the authorized Mac'
repo = Path('/Users/devin/repos/lumen')
os.chdir(repo)
name = ('gpt-6-astra-pro-devinmac3d-url-worker-v33' if options.scope == 'url77'
        else 'gpt-6-astra-pro-devinmac3d-full-worker-v33')
output = Path('output/wpt') / name
launch = output.with_name(output.name + '.launch.json')

if options.resume:
    record = json.loads(launch.read_text())
    assert record['scope'] == options.scope and record['cwd'] == str(repo)
    command = record['argv']
    index = command.index('--max-shards')
    del command[index:index + 2]
    command.append('--resume')
    os.execv(command[0], command)


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


expected_native = 'd48c30587b3f3883129d4978cc7e9aa048ddf263734ecf30d904d1ab07fec3e2'
binary = Path('build/lumen-wpt-d48c30587b3f3883').resolve(strict=True)
assert digest(binary) == expected_native and not binary.stat().st_mode & 0o222
original = Path('build/lumen').resolve(strict=True)
assert digest(original) == expected_native, 'Prepare this generation before rebuilding the original executable'
assert digest('tests/wpt/full_runner.py') == 'be47d18fe8a00001b5b57fbd4dbfe24621485d0c32dcd2d177510db2f026504f'
assert digest('tests/wpt/sequential_compare.py') == 'f98d721edc4def26bb8c300759fee5dcdb746b5c05177a69c945361be09fc6a4'
assert digest('output/wpt/inventory.json') == '9ffd9444520fd3158b7566f37600b0f10b56bbded1ccbd9b1b1ed8166a836053'
assert not output.exists() and not launch.exists(), 'Fresh generation already exists; inspect before resuming'
reference = Path('output/wpt/gpt-6-astra-pro-devinmac3d-url-full-v31')
native = json.loads((reference / 'lumen/run.json').read_text())
chrome = json.loads((reference / 'chromium/run.json').read_text())
fixture = native['signature']['native_fixture']
assert fixture['host_map_mode'] == 'exact' and fixture['host_map']
flags = chrome['signature']['chromium_args']


def exact_option(prefix):
    values = [value[len(prefix):] for value in flags if value.startswith(prefix)]
    assert len(values) == 1
    return values[0]


rules = exact_option('--host-resolver-rules=')
spki = exact_option('--ignore-certificate-errors-spki-list=')
known = ('--host-resolver-rules=', '--ignore-certificate-errors-spki-list=')
extras = [value for value in flags if not value.startswith(known)]
assert '--disable-default-apps' not in extras, 'Do not silently promote the failed profile probe'
dependencies = set()
linked = subprocess.run(['otool', '-L', str(binary)], check=True,
                        capture_output=True, text=True, timeout=15).stdout
for line in linked.splitlines()[1:]:
    path = line.strip().split(' (compatibility version', 1)[0]
    if path.startswith(('/usr/lib/', '/System/Library/')):
        continue
    if not path.startswith('/'):
        raise RuntimeError('Resolve this nonabsolute runtime dependency before launch: ' + path)
    dependencies.add(Path(path).resolve(strict=True))
version = chrome['browser_version']['product'].split('/', 1)[1]
framework = Path('/Applications/Google Chrome.app/Contents/Frameworks/Google Chrome Framework.framework/Versions') / version
assert (framework / 'Google Chrome Framework').is_file()
for path in framework.rglob('*'):
    if path.is_file() and (os.access(path, os.X_OK) or path.suffix in ('.dylib', '.pak', '.bin', '.dat')):
        dependencies.add(path.resolve(strict=True))
command = [sys.executable, 'tests/wpt/sequential_compare.py',
           '--repo', str(repo), '--native-binary', str(original),
           '--fixture-manifest', 'output/wpt/fixture-readiness-v3-clean.json',
           '--host-map', fixture['host_map'], '--host-map-mode', 'exact',
           '--chromium-host-rules', rules, '--chromium-spki', spki,
           '--output', str(output), '--max-shards', '1', '--min-free-gib', '3']
if options.scope == 'url77':
    command.extend(['--types', 'testharness', '--prefix', '/url/', '--shard-size', '77'])
else:
    command.extend(['--types', 'testharness,test262', '--shard-size', '50'])
for path in sorted(dependencies):
    command.extend(['--dependency', str(path)])
command.extend('--chromium-arg=' + value for value in extras)
with launch.open('x') as destination:
    json.dump({'scope': options.scope, 'argv': command, 'cwd': str(repo),
               'native_sha256': expected_native, 'launcher_sha256': digest(__file__)}, destination, indent=2)
print(json.dumps({'scope': options.scope, 'native_sha256': expected_native, 'output': str(output),
                  'planned_testharness': 77 if options.scope == 'url77' else 39402,
                  'planned_test262': 0 if options.scope == 'url77' else 53679,
                  'first_invocation_broad_pairs': 1, 'bound_runtime_files': len(dependencies)}), flush=True)
os.execv(sys.executable, command)
