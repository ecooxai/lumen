#!/usr/bin/env python3
"""Prepare, begin, or resume the fixed b5b2/CfT156 functional-only WPT cohort.

Preparation must precede rebuilding build/lumen. Later invocations use only the
coordinator's immutable snapshot and revalidate the complete official artifact.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import stat
import subprocess
import sys

REPO = Path('/Users/devin/repos/lumen')
NATIVE_SHA = 'b5b2ee8c80685a644a998b8b2cdbe527a372e6c2d77b408a5d0d657c7e65151a'
CORE_SHA = '0da9eda073345d9aeb053f8a8eda21de632eb5e5078a00d0b01b029fcee4ffda'
CONTROLLER_SHA = 'fee36b7ceb143585d6953f23d18a27e80cb6f99bbefafea85966535399fb9626'
POLICY_SHA = '9e4954758177647d82cda5a26d39307cf07bcbfde21fc2e5349cd2065b29f8af'
ADOPTION_SHA = 'bb030858a2082b49dac1cd661ff2707de2168046dd2b435c2fd8dac06289fb3d'
INVENTORY_SHA = '9ffd9444520fd3158b7566f37600b0f10b56bbded1ccbd9b1b1ed8166a836053'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'first', 'resume'))
    parser.add_argument('--output', type=Path,
                        default=Path('output/wpt/gpt-6-astra-pro-devinmac3d-full-b5b2-cft156-functional-v35'))
    parser.add_argument('--controller', type=Path, default=Path('tests/wpt/sequential_compare_functional.py'))
    parser.add_argument('--reference', type=Path, default=Path(
        'output/wpt/reference-browsers/cft-156.0.8078.47-mac-arm64-001/artifact-integrity-v1.json'))
    parser.add_argument('--smoke', type=Path, default=Path(
        'output/wpt/gpt-6-astra-pro-devinmac3d-cft156-url-smoke-v34/chromium'))
    args = parser.parse_args()
    require(platform.system() == 'Darwin', 'Run only on the authorized Mac through devinmac3d')
    os.chdir(REPO)
    controller = args.controller.resolve(strict=True)
    policy_path = controller.with_name('run_cft156_url77.py')

    def digest(path):
        with Path(path).open('rb') as source:
            return hashlib.file_digest(source, 'sha256').hexdigest()

    def load_module(path, expected, name):
        require(digest(path) == expected, 'Reviewed broad-run input changed: ' + str(path))
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    require(controller.is_relative_to(REPO), 'Controller must be project-local')
    require(digest(controller) == CONTROLLER_SHA, 'Reviewed functional-only coordinator changed')
    core_path = REPO / 'tests/wpt/full_runner.py'
    core = load_module(core_path, CORE_SHA, 'broad_cft_core')
    policy = load_module(policy_path, POLICY_SHA, 'broad_cft_reference_policy')
    adoption_path = Path(__file__).with_name('adopt_cft156.py').resolve(strict=True)
    adoption = load_module(adoption_path, ADOPTION_SHA, 'broad_cft_artifact_verifier')
    output = args.output.resolve()
    require(output.is_relative_to(REPO / 'output/wpt'), 'Evidence must stay in project output/wpt')
    launch = output.with_name(output.name + '.launch.json')
    reference_path = args.reference.resolve(strict=True)
    reference = adoption.verify_reference(reference_path, REPO)
    inventory_path = REPO / 'output/wpt/inventory.json'
    require(digest(inventory_path) == INVENTORY_SHA, 'WPT inventory changed')
    inventory = json.loads(inventory_path.read_text())
    require(inventory['test_variants_by_type']['testharness'] == 39402
            and inventory['test_variants_by_type']['test262'] == 53679, 'Full supported-class inventory changed')
    del inventory
    native, _, native_issues = policy.inspect_completed(core, policy.NATIVE, 77, 'lumen', NATIVE_SHA)
    require(not native_issues, 'Native generation has unsafe URL77 evidence')
    smoke_path = args.smoke.resolve(strict=True)
    smoke, _, smoke_issues = policy.inspect_completed(core, smoke_path, 6, 'chromium',
                                                      reference['binary_sha256'], allow_known_teardown=True)
    require(not smoke_issues and smoke['signature']['chromium_headless'] is True,
            'Reference smoke is ineligible for the explicit functional-only policy')
    flags = smoke['signature']['chromium_args']

    def option(prefix):
        values = [flag[len(prefix):] for flag in flags if flag.startswith(prefix)]
        require(len(values) == 1, 'Reference fixture option is missing or ambiguous: ' + prefix)
        return values[0]

    require(len(flags) == 2, 'Reference fixture contains extra browser flags')
    rules = option('--host-resolver-rules=')
    spki = option('--ignore-certificate-errors-spki-list=')
    signature = native['signature']
    fixture = signature['native_fixture']
    require(fixture['host_map_mode'] == 'exact' and fixture['host_map'], 'Native exact host map is absent')
    config = REPO / 'output/wpt/server.config.json'
    readiness = REPO / 'output/wpt/fixture-readiness-v3-clean.json'
    ca = REPO / 'output/wpt/upstream/tools/certs/cacert.pem'
    require(digest(config) == signature['server_config_sha256']
            and digest(readiness) == signature['fixture_manifest_sha256']
            and digest(ca) == fixture['ssl_cert_sha256'], 'Native reference fixture inputs changed')
    current = {'reference_manifest': str(reference_path), 'reference_manifest_sha256': digest(reference_path),
               'reference_tree_sha256': reference['integrity']['tree_sha256'], 'reference_binary_sha256': reference['binary_sha256'],
               'controller': str(controller), 'controller_sha256': CONTROLLER_SHA,
               'core_sha256': CORE_SHA, 'policy_sha256': POLICY_SHA, 'launcher_sha256': digest(__file__),
               'native_sha256': NATIVE_SHA, 'smoke': str(smoke_path),
               'smoke_run_sha256': digest(smoke_path / 'run.json'), 'smoke_results_sha256': digest(smoke_path / 'results.jsonl')}
    if args.action == 'prepare':
        require(not output.exists() and not launch.exists(), 'Use a fresh broad generation; preserve prior evidence')
        original = REPO / 'build/lumen'
        snapshot = original.with_name('lumen-wpt-' + NATIVE_SHA[:16])
        require(digest(original) == NATIVE_SHA and digest(snapshot) == NATIVE_SHA
                and not snapshot.stat().st_mode & 0o222, 'Prepare before rebuilding the known native generation')
        dependencies = {reference_path, adoption_path, adoption_path.with_name('prepare_cft156.py'),
                        policy_path, Path(__file__).resolve(), launch}
        extracted = Path(reference['extracted']).resolve(strict=True)
        for row in reference['integrity']['entries']:
            if row['kind'] == stat.S_IFREG:
                path = (extracted / row['path']).resolve(strict=True)
                require(path.is_relative_to(extracted), 'Verified reference dependency escaped extraction')
                dependencies.add(path)
        linked = subprocess.run(['otool', '-L', str(snapshot)], capture_output=True, text=True,
                                timeout=15, check=True).stdout
        for line in linked.splitlines()[1:]:
            path = line.strip().split(' (compatibility version', 1)[0]
            if path.startswith(('/usr/lib/', '/System/Library/')):
                continue
            require(path.startswith('/'), 'Resolve this nonabsolute native runtime dependency before preparation: ' + path)
            dependencies.add(Path(path).resolve(strict=True))
        command = [sys.executable, str(controller), '--repo', str(REPO), '--runner', str(core_path),
                   '--native-binary', str(original), '--chromium-binary', reference['binary'],
                   '--inventory', str(inventory_path), '--server-config', str(config), '--fixture-manifest', str(readiness),
                   '--ca-file', str(ca), '--host-map', fixture['host_map'], '--host-map-mode', 'exact',
                   '--chromium-host-rules', rules, '--chromium-spki', spki,
                   '--types', 'testharness,test262', '--shard-size', '50', '--smoke-count', '6',
                   '--timeout', '15', '--long-timeout', '65', '--min-free-gib', '3',
                   '--origin', signature['origin'], '--secure-origin', signature['secure_origin'],
                   '--h2-origin', signature['h2_origin'], '--allow-known-reference-teardown', '--output', str(output)]
        for path in sorted(dependencies):
            command.extend(['--dependency', str(path)])
        launch.parent.mkdir(parents=True, exist_ok=True)
        with launch.open('x') as destination:
            json.dump({'inputs': current, 'argv': command, 'cwd': str(REPO),
                       'reference_smoke_incident': smoke.get('functional_reference_incident'),
                       'reference_performance_release_gate_passed': False,
                       'classification': 'functional_only_reference_teardown_failed',
                       'plan': {'testharness': 39402, 'test262': 53679, 'other_classes': 'NOTRUN', 'shard_size': 50},
                       'bound_dependency_files': len(dependencies)}, destination, indent=2)
            destination.write('\n')
        command.append('--prepare-only')
    else:
        saved = json.loads(launch.read_text())
        require(saved['inputs'] == current and saved['cwd'] == str(REPO),
                'Broad launch inputs changed; do not resume a different generation')
        command = list(saved['argv'])
        require(command[:2] == [sys.executable, str(controller)]
                and command[command.index('--output') + 1] == str(output)
                and '--prepare-only' not in command and '--resume' not in command
                and '--allow-known-reference-teardown' in command,
                'Saved controller invocation differs')
        cohort = json.loads((output / 'cohort.json').read_text())
        state = json.loads((output / 'controller.json').read_text())
        require(state['cohort_sha256'] == core.digest(cohort), 'Prepared cohort manifest changed')
        launch_records = [record for record in cohort['files'].values() if record['path'] == str(launch)]
        require(len(launch_records) == 1 and launch_records[0]['sha256'] == digest(launch),
                'Saved launch invocation differs from the prepared cohort dependency')
        command.append('--resume')
        if args.action == 'first':
            command.extend(['--max-shards', '1'])
    for key in ('LUMEN_PRE_FILE', 'LUMEN_SHOT', 'LUMEN_SHOT_MS', 'LUMEN_NO_JS'):
        os.environ.pop(key, None)
    print(json.dumps({'action': args.action, 'controller_pid': os.getpid(), 'cohort': str(output),
                      'planned_testharness': 39402, 'planned_test262': 53679,
                      'reference_performance_release_gate_passed': False,
                      'classification': 'functional_only_reference_teardown_failed'}), flush=True)
    os.execv(sys.executable, command)


if __name__ == '__main__':
    main()
