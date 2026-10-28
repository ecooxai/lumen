#!/usr/bin/env python3
"""Prepare, begin, or resume the b5b2/CfT156 cohort with the v36 reporter.

The already prepared immutable native snapshot is reused with its recorded
resources and dylibs. Mutable build/lumen is never read or changed. Every
invocation revalidates the complete official Chromium artifact.
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
from types import SimpleNamespace

REPO = Path('/Users/devin/repos/lumen')
NATIVE_SHA = 'b5b2ee8c80685a644a998b8b2cdbe527a372e6c2d77b408a5d0d657c7e65151a'
CORE_SHA = 'a7bc96c3697fb099ad63c145011bc434a66d865a713a62e5198731bed9600ac7'
REPORTER_SHA = 'dd0c222e2680c8621ac42dc98746b15891fa4e14d9f99f4c909dbc2ca2558328'
CONTROLLER_SHA = '03d08456037ec0509e36c7cc98771ac6e908e445c367986057b863ed5ab73cb6'
PRIOR_COHORT_DIGEST = '3d85d853860220b20e25482c5cbc7c8d0f76f11b2946abecca48c7926ab84c43'
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
                        default=Path('output/wpt/gpt-6-astra-pro-devinmac3d-full-b5b2-cft156-functional-v36'))
    parser.add_argument('--controller', type=Path, default=Path('tests/wpt/sequential_compare_v36.py'))
    parser.add_argument('--snapshot-cohort', type=Path, default=Path(
        'output/wpt/gpt-6-astra-pro-devinmac3d-full-b5b2-cft156-functional-v35/cohort.json'))
    parser.add_argument('--fixture-manifest', type=Path,
                        default=Path('output/wpt/fixture-readiness-v36-clean.json'))
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
    controller_module = load_module(controller, CONTROLLER_SHA, 'broad_v36_controller')
    core_path = REPO / 'tests/wpt/full_runner.py'
    core = load_module(core_path, CORE_SHA, 'broad_cft_core')
    require(digest(core_path.with_name('testharnessreport.js')) == REPORTER_SHA,
            'Reviewed repeat-aware reporter changed')
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
    inventory_metadata = {key: inventory[key] for key in ('wpt_revision', 'manifest_sha256')}
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
    historical_readiness = REPO / 'output/wpt/fixture-readiness-v3-clean.json'
    readiness = args.fixture_manifest.resolve(strict=True)
    require(readiness.is_relative_to(REPO / 'output/wpt') and readiness != historical_readiness,
            'Use a fresh fixture record; preserve historical readiness evidence')
    ca = REPO / 'output/wpt/upstream/tools/certs/cacert.pem'
    require(digest(config) == signature['server_config_sha256']
            and digest(historical_readiness) == signature['fixture_manifest_sha256']
            and digest(ca) == fixture['ssl_cert_sha256'], 'Native reference fixture inputs changed')
    controller_module.validate_fixture(json.loads(readiness.read_text()), inventory_metadata,
        SimpleNamespace(server_config=config, ca_file=ca, runner=core_path,
                        origin=signature['origin'], secure_origin=signature['secure_origin'],
                        h2_origin=signature['h2_origin']))
    prior_path = args.snapshot_cohort.resolve(strict=True)
    require(prior_path.is_relative_to(REPO / 'output/wpt'), 'Snapshot proof must stay in project evidence')
    prior = json.loads(prior_path.read_text())
    require(core.digest(prior) == PRIOR_COHORT_DIGEST, 'The actual prepared v35 snapshot proof changed')
    snapshot = REPO / 'build' / ('lumen-wpt-' + NATIVE_SHA[:16])
    adopted = controller_module.reuse_native_snapshot(snapshot, prior_path, REPO)
    require(adopted['snapshot']['sha256'] == NATIVE_SHA, 'Prior snapshot differs from the measured native generation')
    native_dependencies = set()
    previous_files = {record['path']: record for record in prior['files'].values()}
    linked = subprocess.run(['otool', '-L', str(snapshot)], capture_output=True, text=True,
                            timeout=15, check=True).stdout
    for line in linked.splitlines()[1:]:
        path = line.strip().split(' (compatibility version', 1)[0]
        if path.startswith(('/usr/lib/', '/System/Library/')):
            continue
        require(path.startswith('/'), 'Unresolved native runtime dependency: ' + path)
        dependency = Path(path).resolve(strict=True)
        previous = previous_files.get(str(dependency))
        require(previous is not None, 'Native dependency lacks prior generation evidence: ' + str(dependency))
        controller_module.assert_files_unchanged({'native_dependency': previous}, content=True)
        native_dependencies.add(dependency)
    current = {'reference_manifest': str(reference_path), 'reference_manifest_sha256': digest(reference_path),
               'reference_tree_sha256': reference['integrity']['tree_sha256'], 'reference_binary_sha256': reference['binary_sha256'],
               'controller': str(controller), 'controller_sha256': CONTROLLER_SHA,
               'core_sha256': CORE_SHA, 'reporter_sha256': REPORTER_SHA,
               'policy_sha256': POLICY_SHA, 'launcher_sha256': digest(__file__),
               'native_sha256': NATIVE_SHA, 'smoke': str(smoke_path),
               'smoke_run_sha256': digest(smoke_path / 'run.json'), 'smoke_results_sha256': digest(smoke_path / 'results.jsonl'),
               'native_run_sha256': digest(policy.NATIVE / 'run.json'),
               'native_results_sha256': digest(policy.NATIVE / 'results.jsonl'),
               'prior_cohort': str(prior_path), 'prior_cohort_sha256': digest(prior_path),
               'prior_cohort_canonical_digest': PRIOR_COHORT_DIGEST,
               'fixture_manifest': str(readiness), 'fixture_manifest_sha256': digest(readiness)}
    if args.action == 'prepare':
        require(not output.exists() and not launch.exists(), 'Use a fresh broad generation; preserve prior evidence')
        dependencies = {reference_path, adoption_path, adoption_path.with_name('prepare_cft156.py'),
                        policy_path, Path(__file__).resolve(), launch, prior_path, historical_readiness,
                        *native_dependencies}
        for evidence in (policy.NATIVE, smoke_path):
            dependencies.update(evidence / name for name in ('run.json', 'schedule.json', 'results.jsonl'))
        extracted = Path(reference['extracted']).resolve(strict=True)
        for row in reference['integrity']['entries']:
            if row['kind'] == stat.S_IFREG:
                path = (extracted / row['path']).resolve(strict=True)
                require(path.is_relative_to(extracted), 'Verified reference dependency escaped extraction')
                dependencies.add(path)
        command = [sys.executable, str(controller), '--repo', str(REPO), '--runner', str(core_path),
                   '--native-binary', str(snapshot), '--native-snapshot-manifest', str(prior_path),
                   '--chromium-binary', reference['binary'],
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
                and '--allow-known-reference-teardown' in command
                and command[command.index('--native-binary') + 1] == str(snapshot)
                and command[command.index('--native-snapshot-manifest') + 1] == str(prior_path),
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
