#!/usr/bin/env python3
"""Run six unchanged official URL cases on the artifact-verified CfT app.

Its upstream ad-hoc/unsealed signing and failed strict bundle verification remain
visible. The complete extracted archive is checked immediately before launch.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import sys


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reference', required=True, type=Path, help='Verified CfT artifact-integrity-v1.json')
parser.add_argument('--output', required=True, type=Path, help='Fresh diagnostic run directory')
args = parser.parse_args()
if platform.system() != 'Darwin':
    raise RuntimeError('Run only on the authorized Mac')
repo = Path('/Users/devin/repos/lumen')
os.chdir(repo)
reference_path = args.reference.resolve(strict=True)
reference = json.loads(reference_path.read_text())
if reference.get('status') != 'official_artifact_verified' or reference.get('version') != '156.0.8078.47':
    raise RuntimeError('Reference preparation has not verified this exact CfT version')
preparation = reference_path.parent
if (reference_path.name != 'artifact-integrity-v1.json' or
        not preparation.is_relative_to(repo / 'output/wpt/reference-browsers') or
        preparation == repo / 'output/wpt/reference-browsers'):
    raise RuntimeError('Reference record must live inside its project-local preparation directory')
runner = repo / 'tests/wpt/full_runner.py'
if digest(runner) != '0da9eda073345d9aeb053f8a8eda21de632eb5e5078a00d0b01b029fcee4ffda':
    raise RuntimeError('The measured cleanup/mode runner changed')
inventory_path = repo / 'output/wpt/inventory.json'
if digest(inventory_path) != '9ffd9444520fd3158b7566f37600b0f10b56bbded1ccbd9b1b1ed8166a836053':
    raise RuntimeError('The pinned WPT inventory changed')
spec = importlib.util.spec_from_file_location('lumen_wpt_smoke_guard', runner)
core = importlib.util.module_from_spec(spec)
spec.loader.exec_module(core)
inventory = json.loads(inventory_path.read_text())
selection, descriptor = core.select_schedule(inventory, 'testharness', ['/url/'], limit=6)
prior_dir = repo / 'output/wpt/gpt-6-astra-pro-devinmac3d-urlfix-v31/chromium'
prior = json.loads((prior_dir / 'run.json').read_text())
if (len(selection) != 6 or selection != core.verified_schedule(prior_dir, prior['signature']) or
        prior['signature']['inventory_sha256'] != digest(inventory_path)):
    raise RuntimeError('Selected smoke cases differ from the prior hash-validated six-case schedule')
output = args.output.resolve()
if not output.is_relative_to(repo / 'output/wpt') or output.exists():
    raise RuntimeError('A fresh project WPT output is required')
previous = json.loads((repo / 'output/wpt/gpt-6-astra-pro-devinmac3d-url-full-v31/chromium/run.json').read_text())
allowed = ('--host-resolver-rules=', '--ignore-certificate-errors-spki-list=')
flags = previous['signature']['chromium_args']
if len(flags) != 2 or any(sum(value.startswith(prefix) for value in flags) != 1 for prefix in allowed):
    raise RuntimeError('Reference fixture arguments include unreviewed browser changes')
config = repo / 'output/wpt/server.config.json'
if digest(config) != '2056ab1f55ad0a37432460ac8f2fab9880c26b74f7036282bd3272d302e91adf':
    raise RuntimeError('Validated fixture configuration changed')
readiness = repo / 'output/wpt/fixture-readiness-v3-clean.json'
if json.loads(readiness.read_text()).get('fixture_ready') is not True:
    raise RuntimeError('Fixture has no verified readiness evidence')
for key in ('LUMEN_PRE_FILE', 'LUMEN_SHOT', 'LUMEN_SHOT_MS', 'LUMEN_NO_JS'):
    os.environ.pop(key, None)
verifier_path = Path(__file__).with_name('adopt_cft156.py')
if digest(verifier_path) != 'bb030858a2082b49dac1cd661ff2707de2168046dd2b435c2fd8dac06289fb3d':
    raise RuntimeError('The reviewed complete-artifact verifier changed')
spec = importlib.util.spec_from_file_location('cft_launch_integrity', verifier_path)
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)
reference = verifier.verify_reference(reference_path, repo)
binary = Path(reference['binary']).resolve(strict=True)
if not binary.is_relative_to(preparation / 'extracted'):
    raise RuntimeError('Reference executable is outside the same verified preparation directory')
command = [sys.executable, str(runner), 'run',
           '--inventory', str(inventory_path), '--engine', 'chromium',
           '--binary', str(binary), '--output', str(output),
           '--types', 'testharness', '--prefix', '/url/', '--limit', '6',
           '--origin', 'http://web-platform.test:8000', '--secure-origin', 'https://web-platform.test:18443',
           '--h2-origin', 'https://web-platform.test:9000', '--timeout', '15', '--long-timeout', '65',
           '--server-config', str(config), '--fixture-manifest', str(readiness), '--strict-infrastructure']
command.extend('--chromium-arg=' + value for value in flags)
launch = output.with_name(output.name + '.launch.json')
launch.parent.mkdir(parents=True, exist_ok=True)
with launch.open('x') as target:
    json.dump({'argv': command, 'cwd': str(repo), 'reference_manifest': str(reference_path),
               'reference_manifest_sha256': digest(reference_path), 'expected_version': reference['version'],
               'binary_sha256': reference['binary_sha256'], 'launcher_sha256': digest(__file__),
               'reference_verification_policy': reference['verification_policy'],
               'reference_tree_sha256': reference['integrity']['tree_sha256'],
               'strict_bundle_verification_passed': False, 'developer_id_verified': False,
               'strict_bundle_verification': reference['strict_bundle_verification'],
               'schedule_sha256': core.digest(selection), 'selection': descriptor,
               'prior_measured_smoke': str(prior_dir),
               'scope': 'Fresh headless full-browser reference smoke; six official URL files; not a new matched rate'},
              target, indent=2)
print(json.dumps({'fresh_output': str(output), 'reference_version': reference['version'],
                  'binary_sha256': reference['binary_sha256'], 'chromium_headless': True}), flush=True)
os.execv(sys.executable, command)
