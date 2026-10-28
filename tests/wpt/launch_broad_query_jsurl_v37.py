#!/usr/bin/env python3
"""Prepare/run a fresh 200-file pilot after the actual matched v37 URL77 gate.

The measured v36 first50 remains immutable. The unchanged reviewed coordinator
owns paired smokes, file watchdogs, telemetry, cleanup, disk guard and resume.
Only preparation reads build/lumen; all browser work uses its frozen adjacent
snapshot. Every invocation verifies the complete official CfT artifact.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import stat
import sys
from types import SimpleNamespace

REPO = Path('/Users/devin/repos/lumen')
NATIVE_SHA = 'd7fea94c14f7323446bcd6ca47f53bfba11b9bb86a4dcdf6acf41a309a997e5b'
CORE_SHA = 'a7bc96c3697fb099ad63c145011bc434a66d865a713a62e5198731bed9600ac7'
REPORTER_SHA = 'dd0c222e2680c8621ac42dc98746b15891fa4e14d9f99f4c909dbc2ca2558328'
CONTROLLER_SHA = '03d08456037ec0509e36c7cc98771ac6e908e445c367986057b863ed5ab73cb6'
POLICY_SHA = '9e4954758177647d82cda5a26d39307cf07bcbfde21fc2e5349cd2065b29f8af'
ADOPTION_SHA = 'bb030858a2082b49dac1cd661ff2707de2168046dd2b435c2fd8dac06289fb3d'
INVENTORY_SHA = '9ffd9444520fd3158b7566f37600b0f10b56bbded1ccbd9b1b1ed8166a836053'
READINESS_SHA = '8ac07a9ac8d163cfd868d4172cf82d837020573c9999365faf84be6a789682e7'
URL77_LAUNCHER_SHA = 'fc6b80437dfc62ffa66be16d16f7a3e2ac0d6c77ddef8e50aadf59f3ed74c222'
URL77 = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-query-jsurl-url77-cft156-v37'
DEFAULT_OUTPUT = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-full-query-jsurl-d7fea94c-cft156-functional-v37-200'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def module(path, expected, name):
    require(digest(path) == expected, 'Reviewed launcher dependency changed: ' + str(path))
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'first', 'resume'))
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    require(platform.system() == 'Darwin', 'Run only on the authorized Mac through devinmac3d')
    require(len(URL77_LAUNCHER_SHA) == 64 and all(char in '0123456789abcdef' for char in URL77_LAUNCHER_SHA),
            'This candidate has not yet been pinned to the final reviewed URL77 launcher')
    os.chdir(REPO)
    here = REPO / 'tests/wpt'
    controller_path = here / 'sequential_compare_v36.py'
    controller = module(controller_path, CONTROLLER_SHA, 'query_jsurl_broad_controller')
    core_path = here / 'full_runner.py'
    core = module(core_path, CORE_SHA, 'query_jsurl_broad_core')
    policy_path = here / 'run_cft156_url77.py'
    module(policy_path, POLICY_SHA, 'query_jsurl_broad_reference_policy')
    adoption_path = here / 'adopt_cft156.py'
    adoption = module(adoption_path, ADOPTION_SHA, 'query_jsurl_broad_artifact_verifier')
    url_launcher = here / 'run_query_jsurl_url77_v37.py'
    require(digest(url_launcher) == URL77_LAUNCHER_SHA, 'Reviewed URL77 gate launcher changed')
    require(digest(here / 'testharnessreport.js') == REPORTER_SHA, 'Repeat-aware reporter changed')
    output = args.output.resolve()
    require(output.is_relative_to(REPO / 'output/wpt') and output != URL77
            and 'v37-200' in output.name, 'Use a separate fresh v37-200 broad cohort')
    launch_path = output.with_name(output.name + '.launch.json')
    reference_path = REPO / 'output/wpt/reference-browsers/cft-156.0.8078.47-mac-arm64-001/artifact-integrity-v1.json'
    reference = adoption.verify_reference(reference_path, REPO)
    inventory_path = REPO / 'output/wpt/inventory.json'
    require(digest(inventory_path) == INVENTORY_SHA, 'Pinned upstream inventory changed')
    inventory = json.loads(inventory_path.read_text())
    require(inventory['test_variants_by_type']['testharness'] == 39402
            and inventory['test_variants_by_type']['test262'] == 53679, 'Supported-class inventory differs')
    url_schedule, url_selection = core.select_schedule(inventory, 'testharness', ['/url/'], 0, None)
    require(len(url_schedule) == 77, 'Actual complete URL selection changed')
    metadata = {key: inventory[key] for key in ('wpt_revision', 'manifest_sha256')}
    del inventory
    comparison_path = URL77 / 'comparison.json'
    comparison = json.loads(comparison_path.read_text())
    gate = comparison['matched_query_jsurl_url77']
    inputs = gate['inputs']
    require(gate.get('classification') == 'functional_only_reference_teardown_failed'
            and gate.get('reference_performance_release_gate_passed') is False,
            'URL77 gate lost the explicit reference qualification')
    for key, expected in {'core_sha256': CORE_SHA, 'reporter_sha256': REPORTER_SHA,
                          'inventory_sha256': INVENTORY_SHA, 'native_sha256': NATIVE_SHA,
                          'launcher_sha256': URL77_LAUNCHER_SHA,
                          'fixture_manifest_sha256': READINESS_SHA,
                          'reference_manifest_sha256': digest(reference_path),
                          'reference_tree_sha256': reference['integrity']['tree_sha256'],
                          'schedule_sha256': core.digest(url_schedule), 'selection': url_selection}.items():
        require(inputs.get(key) == expected, 'Actual completed URL77 input differs: ' + key)
    dependencies = {comparison_path, url_launcher, reference_path, adoption_path,
                    adoption_path.with_name('prepare_cft156.py'), policy_path, Path(__file__).resolve(), launch_path}
    actual_runs = {}
    for engine, expected_binary in (('lumen', NATIVE_SHA), ('chromium', reference['binary_sha256'])):
        directory = URL77 / engine
        run = json.loads((directory / 'run.json').read_text())
        launch = json.loads((URL77 / (engine + '.launch.json')).read_text())
        signature = run['signature']
        require(launch['inputs'] == inputs and signature['engine'] == engine
                and signature['binary_sha256'] == expected_binary
                and signature['runner_sha256'] == CORE_SHA and signature['reporter_sha256'] == REPORTER_SHA
                and signature['fixture_manifest_sha256'] == READINESS_SHA
                and signature['strict_infrastructure'] is True,
                'URL77 actual run no longer matches its paired gate: ' + engine)
        require(run.get('finished_schedule') is True
                and core.verified_schedule(directory, signature) == url_schedule,
                'URL77 gate is not the actual completed schedule: ' + engine)
        count, assertions = 0, Counter()
        for index, row in enumerate(core.iter_jsonl(directory / 'results.jsonl')):
            require(index < len(url_schedule) and (row['type'], row['test']) ==
                    (url_schedule[index]['type'], url_schedule[index]['test']), 'URL77 rows are not contiguous')
            require(not controller.row_issues(row, smoke=False) and not core.measurement_failure(row)
                    and (row.get('reporter_state') or {}).get('status') == 'ATTACHED',
                    'URL77 case has unsafe reporting/cleanup/telemetry evidence')
            count += 1
            assertions.update(item['status'] for item in row.get('subtests', []))
        observed = gate['observed_results'][engine]
        require(count == 77 == observed['files'] and dict(assertions) == observed['assertion_counts'],
                'URL77 gate summary differs from actual complete results')
        stops = [key for key, value in run.items() if key.startswith('stopped_for_') and value]
        if engine == 'lumen':
            require(not stops, 'Native URL77 has unsafe stop state')
        else:
            require((run.get('browser_version') or {}).get('product', '').split('/')[-1] == '156.0.8078.47'
                    and signature.get('chromium_headless') is True, 'Actual reference version or mode changed')
            if controller.natural_reference_cleanup(run):
                require(not stops, 'Reference has an unrelated stop condition')
            else:
                issues = stops + ['chromium final cleanup failed']
                require(controller.known_reference_incident(directory, run, True, issues) is not None,
                        'Reference teardown is outside the unchanged explicit policy')
            require(run.get('chromium_sessions'), 'Reference session telemetry is absent')
            for supplied in run['chromium_sessions']:
                session = Path(supplied).resolve(strict=True)
                require(session.parent == directory, 'Reference session escaped its exact URL77 stage')
                metrics = session / 'chromium.metrics-summary.json'
                require(not core.measurement_failure({'metrics': json.loads(metrics.read_text())}),
                        'Actual reference session telemetry failed')
                dependencies.update((metrics, session / 'chromium.cleanup.json', session / 'chromium-process.log'))
        actual_runs[engine] = run
        dependencies.update(directory / name for name in ('run.json', 'schedule.json', 'results.jsonl'))
        dependencies.add(URL77 / (engine + '.launch.json'))
    records = inputs['native_dependencies']
    require(records and len({record['path'] for record in records}) == len(records), 'Native dependency proof is ambiguous')
    controller.assert_files_unchanged({str(index): record for index, record in enumerate(records)}, content=True)
    snapshot = REPO / ('build/lumen-wpt-' + NATIVE_SHA[:16])
    matching = [record for record in records if record['path'] == str(snapshot)]
    require(len(matching) == 1 and matching[0]['sha256'] == NATIVE_SHA
            and not snapshot.is_symlink() and not snapshot.stat().st_mode & 0o222
            and os.access(snapshot, os.X_OK), 'Actual immutable native generation changed')
    dependencies.update(Path(record['path']) for record in records)
    extracted = Path(reference['extracted']).resolve(strict=True)
    for row in reference['integrity']['entries']:
        if row['kind'] == stat.S_IFREG:
            dependency = (extracted / row['path']).resolve(strict=True)
            require(dependency.is_relative_to(extracted), 'Reference artifact dependency escaped its extraction')
            dependencies.add(dependency)
    native_signature = actual_runs['lumen']['signature']
    chrome_signature = actual_runs['chromium']['signature']
    native_fixture = native_signature['native_fixture']
    require(native_fixture['host_map_mode'] == 'exact' and native_fixture['host_map'], 'Exact host mapping is absent')
    config = REPO / 'output/wpt/server.config.json'
    readiness = REPO / 'output/wpt/fixture-readiness-v36-clean.json'
    ca = REPO / 'output/wpt/upstream/tools/certs/cacert.pem'
    require(digest(config) == native_signature['server_config_sha256']
            and digest(readiness) == READINESS_SHA and digest(ca) == native_fixture['ssl_cert_sha256'],
            'URL77 fixture inputs changed')
    origins = {key: native_signature[key] for key in ('origin', 'secure_origin', 'h2_origin')}
    require(all(chrome_signature[key] == value for key, value in origins.items()), 'Paired origins changed')
    controller.validate_fixture(json.loads(readiness.read_text()), metadata,
        SimpleNamespace(server_config=config, ca_file=ca, runner=core_path, **origins))
    flags = chrome_signature['chromium_args']
    def option(prefix):
        found = [value[len(prefix):] for value in flags if value.startswith(prefix)]
        require(len(found) == 1, 'Reference fixture flag missing or ambiguous: ' + prefix)
        return found[0]
    require(len(flags) == 2, 'Unreviewed reference browser flags')
    rules = option('--host-resolver-rules=')
    spki = option('--ignore-certificate-errors-spki-list=')
    current = {'native_sha256': NATIVE_SHA, 'controller_sha256': CONTROLLER_SHA, 'core_sha256': CORE_SHA,
        'reporter_sha256': REPORTER_SHA, 'launcher_sha256': digest(__file__),
        'url77_launcher_sha256': URL77_LAUNCHER_SHA, 'url77_comparison_sha256': digest(comparison_path),
        'url77_inputs': inputs, 'reference_manifest_sha256': digest(reference_path),
        'reference_tree_sha256': reference['integrity']['tree_sha256'], 'reference_binary_sha256': reference['binary_sha256'],
        'fixture_manifest_sha256': READINESS_SHA, 'native_dependencies': records}
    if args.action == 'prepare':
        require(not output.exists() and not launch_path.exists(), 'Preserve every prior generation; prepare a fresh cohort')
        require(digest(REPO / 'build/lumen') == NATIVE_SHA,
                'Mutable native build advanced before preparation; use a separately reviewed snapshot route')
        command = [sys.executable, str(controller_path), '--repo', str(REPO), '--runner', str(core_path),
            '--native-binary', str(REPO / 'build/lumen'), '--chromium-binary', reference['binary'],
            '--inventory', str(inventory_path), '--server-config', str(config), '--fixture-manifest', str(readiness),
            '--ca-file', str(ca), '--host-map', native_fixture['host_map'], '--host-map-mode', 'exact',
            '--chromium-host-rules', rules, '--chromium-spki', spki,
            '--types', 'testharness,test262', '--shard-size', '200', '--smoke-count', '6',
            '--timeout', '15', '--long-timeout', '65', '--min-free-gib', '3',
            '--origin', origins['origin'], '--secure-origin', origins['secure_origin'], '--h2-origin', origins['h2_origin'],
            '--allow-known-reference-teardown', '--output', str(output)]
        for path in sorted(dependencies):
            command.extend(['--dependency', str(path)])
        with launch_path.open('x') as destination:
            json.dump({'inputs': current, 'argv': command, 'cwd': str(REPO),
                'classification': 'functional_only_reference_teardown_failed',
                'reference_performance_release_gate_passed': False,
                'plan': {'testharness': 39402, 'test262': 53679, 'shard_size': 200,
                         'first_invocation_broad_pairs': 1, 'other_classes': 'NOTRUN'},
                'scope': 'Fresh generation. v36 first50 remains a separate baseline; no outcome denominators are mixed.'}, destination, indent=2)
            destination.write('\n')
        command.append('--prepare-only')
    else:
        saved = json.loads(launch_path.read_text())
        require(saved['inputs'] == current and saved['cwd'] == str(REPO), 'Prepared broad launch identity changed')
        command = list(saved['argv'])
        require(command[:2] == [sys.executable, str(controller_path)]
                and command[command.index('--output') + 1] == str(output)
                and command[command.index('--shard-size') + 1] == '200'
                and command[command.index('--native-binary') + 1] == str(REPO / 'build/lumen')
                and '--prepare-only' not in command and '--resume' not in command and '--max-shards' not in command,
                'Saved prepared invocation changed')
        cohort = json.loads((output / 'cohort.json').read_text())
        state = json.loads((output / 'controller.json').read_text())
        require(state['cohort_sha256'] == core.digest(cohort), 'Prepared cohort manifest changed')
        launcher_records = [record for record in cohort['files'].values() if record['path'] == str(launch_path)]
        require(len(launcher_records) == 1 and launcher_records[0]['sha256'] == digest(launch_path),
                'Prepared launch record is no longer the bound dependency')
        command.append('--resume')
        if args.action == 'first':
            command.extend(['--max-shards', '1'])
    for key in ('LUMEN_PRE_FILE', 'LUMEN_SHOT', 'LUMEN_SHOT_MS', 'LUMEN_NO_JS'):
        os.environ.pop(key, None)
    print(json.dumps({'action': args.action, 'controller_pid': os.getpid(), 'cohort': str(output),
        'shard_size': 200, 'reference_performance_release_gate_passed': False,
        'classification': 'functional_only_reference_teardown_failed'}), flush=True)
    os.execv(sys.executable, command)


if __name__ == '__main__':
    main()
