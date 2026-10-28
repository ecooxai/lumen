#!/usr/bin/env python3
"""Read the closed language-200 pilot; preserve failures and inspect its wrapper.

This diagnostic never launches a browser, reruns a test, changes an outcome, or
equates a shared failure with a harness defect. Sources are read only from the
unchanged pinned upstream checkout. The original raw rows remain authoritative.
"""
import argparse
import ast
from collections import Counter
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import platform
import subprocess
import sys

REPO = Path('/Users/devin/repos/lumen')
COHORT = REPO / 'output/wpt/gpt-6-astra-pro-devinmac3d-test262-d7fea94c-cft156-functional-v38-language200'
COHORT_SHA = '2a8041ce72abc2c6d91f2b8d788c8b5f593fe58ef645eca7e8e8d62113cdcf11'
CONTROLLER_SHA = '614afc9447dbc287fca630265041c0a1495c69ddbfad10cf1c3987870d742a4a'
COMPARISON_SHA = '43594177c12a869a8b70604f2c7d889be234502279587c63fc7d9f335e390d01'
AGGREGATE_SHA = '24ebf980989e2815787babcf8caaf1e67309d89d8dfcfb9e51ef88d4ce6997d5'
CORE_SHA = 'a7bc96c3697fb099ad63c145011bc434a66d865a713a62e5198731bed9600ac7'
INVENTORY_SHA = '9ffd9444520fd3158b7566f37600b0f10b56bbded1ccbd9b1b1ed8166a836053'
NATIVE_SHA = 'd7fea94c14f7323446bcd6ca47f53bfba11b9bb86a4dcdf6acf41a309a997e5b'
CHROME_SHA = '34fbfdb52aacee492bbada9972bf323eba663f8d8a069f86e2652e5158d6c580'
UPSTREAM = REPO / 'output/wpt/upstream'
REVISION = 'a81c1ab584c1d71a78fe5b452df18330510c208e'
MANIFEST_SHA = 'bfa9753b1349816ceeb29a8db1e1698212e573f5e051708f9be220a223b40bfa'
PREFIX = 'third_party/test262/test/'
UNIT = 'test262-000000'


def require(value, message):
    if not value:
        raise RuntimeError(message)


def canonical(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def identity(path):
    value = path.stat()
    return [value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_ctime_ns]


class Evidence:
    def __init__(self):
        self.files = {}

    def path(self, supplied, root, maximum=64 * 1024 * 1024, expected=None):
        path = supplied.resolve(strict=True)
        require(path.is_relative_to(root) and path.is_file() and not supplied.is_symlink(),
                'Only regular confined input files are permitted: ' + str(supplied))
        before = identity(path)
        require(before[2] <= maximum, 'Input exceeds its explicit size bound')
        with path.open('rb') as source:
            digest = hashlib.file_digest(source, 'sha256').hexdigest()
        require(identity(path) == before, 'Input changed while fingerprinting')
        require(expected is None or digest == expected, 'Pinned input changed: ' + str(path))
        previous = self.files.get(str(path))
        record = {'sha256': digest, 'identity': before}
        require(previous is None or previous == record, 'Input changed between reads')
        self.files[str(path)] = record
        return path

    def load(self, path, root=COHORT, expected=None):
        return json.loads(self.path(path, root, expected=expected).read_text())

    def rows(self, path):
        with self.path(path, COHORT, maximum=32 * 1024 * 1024).open('rb') as source:
            for index in range(201):
                line = source.readline(1024 * 1024 + 1)
                if not line:
                    return
                require(index < 200 and len(line) <= 1024 * 1024 and line.endswith(b'\n') and line.strip(),
                        'Result stream is oversized, interrupted, or longer than200')
                yield json.loads(line)

    def unchanged(self):
        for name, record in self.files.items():
            require(identity(Path(name)) == record['identity'], 'Evidence changed during extraction: ' + name)


def git(args):
    result = subprocess.run(['git', '-C', str(UPSTREAM), *args], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=30)
    require(len(result.stdout) <= 1024 * 1024 and len(result.stderr) <= 65536, 'Git evidence exceeds bounds')
    require(result.returncode == 0, 'Read-only upstream verification failed: ' + ' '.join(args[:3]))
    return result.stdout.decode('utf-8', errors='strict').strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path,
                        default=Path('output/wpt/gpt-6-astra-pro-devinmac3d-test262-language200-failures-v38'))
    args = parser.parse_args()
    require(platform.system() == 'Darwin' and Path.cwd().resolve() == REPO,
            'Run only on the authorized Mac in the actual project')
    output = (REPO / args.output).resolve()
    require(output.is_relative_to(REPO / 'output/wpt') and not output.exists()
            and not output.is_relative_to(COHORT) and not output.is_relative_to(UPSTREAM),
            'Use a fresh derived diagnostic directory outside the source cohort/checkout')
    evidence = Evidence()
    core_path = evidence.path(REPO / 'tests/wpt/full_runner.py', REPO, expected=CORE_SHA)
    spec = importlib.util.spec_from_file_location('language200_pinned_core', core_path)
    core = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = core
    spec.loader.exec_module(core)
    cohort = evidence.load(COHORT / 'cohort.json')
    controller = evidence.load(COHORT / 'controller.json', expected=CONTROLLER_SHA)
    aggregate = evidence.load(COHORT / 'aggregate.json', expected=AGGREGATE_SHA)
    require(canonical(cohort) == COHORT_SHA == controller['cohort_sha256']
            and controller.get('status') == 'complete' and controller.get('child_pid') is None
            and controller.get('current_unit') is None, 'The exact paired cohort is not closed')
    unit = next(value for value in cohort['units'] if value['name'] == UNIT)
    state = controller['units'][UNIT]
    require(unit['phase'] == 'broad' and unit['type'] == 'test262' and unit['selected_files'] == 200
            and unit['offset'] == 0 and state.get('paired_complete') is True, 'Pilot unit changed or incomplete')
    directory = COHORT / UNIT
    comparison = evidence.load(directory / 'comparison.json', expected=COMPARISON_SHA)
    require(state['comparison_sha256'] == COMPARISON_SHA, 'Controller does not bind this comparison')
    inventory = evidence.load(REPO / 'output/wpt/inventory.json', REPO / 'output/wpt', INVENTORY_SHA)
    require(Path(inventory['wpt_root']).resolve() == UPSTREAM and inventory['wpt_revision'] == REVISION
            and inventory['manifest_sha256'] == MANIFEST_SHA, 'Pinned upstream identity differs')
    evidence.path(UPSTREAM / 'MANIFEST.json', UPSTREAM, maximum=128 * 1024 * 1024, expected=MANIFEST_SHA)
    require(git(['rev-parse', 'HEAD']) == REVISION, 'Upstream revision changed')
    git(['diff', '--quiet', 'HEAD', '--'])
    selected, selection = core.select_schedule(inventory, 'test262', ['/' + PREFIX], 0, 200)
    categories = Counter('upstream_test' if row['source'].startswith(PREFIX) else
                         'upstream_harness' if row['source'].startswith('third_party/test262/harness/') else
                         'infrastructure' if row['source'].startswith('infrastructure/test262/') else 'other'
                         for row in inventory['tests'] if row['type'] == 'test262')
    require(dict(categories) == {'upstream_test': 53616, 'upstream_harness': 43, 'infrastructure': 20},
            'Recorded upfront source categories changed')
    del inventory
    require(len(selected) == 200 and all(row['source'].startswith(PREFIX) for row in selected)
            and canonical(selected) == unit['schedule_sha256'], 'Exact canonical language selection differs')
    metas = {}
    for engine, binary in (('lumen', NATIVE_SHA), ('chromium', CHROME_SHA)):
        meta = metas[engine] = evidence.load(directory / engine / 'run.json')
        schedule = evidence.load(directory / engine / 'schedule.json')
        signature = meta['signature']
        require(schedule == selected and canonical(schedule) == signature['schedule_sha256']
                and signature['engine'] == engine and signature['binary_sha256'] == binary
                and signature['runner_sha256'] == CORE_SHA and signature['inventory_sha256'] == INVENTORY_SHA
                and signature['wpt_revision'] == REVISION and signature['manifest_sha256'] == MANIFEST_SHA
                and meta.get('finished_schedule') is True, 'Engine source, generation, or schedule differs')
    require(metas['chromium']['browser_version']['product'] == 'Chrome/156.0.8078.47', 'Reference version changed')
    output.mkdir(parents=True, exist_ok=False)
    report = {'status': 'reading', 'cohort': str(COHORT), 'cohort_sha256': COHORT_SHA,
              'comparison_sha256': COMPARISON_SHA, 'scope': 'Exactly200 upfront-selected genuine upstream Test262 wrappers',
              'source_category_counts': dict(categories), 'selection': selection,
              'schedule_first': selected[0], 'schedule_last': selected[-1],
              'reference_qualification': comparison.get('reference_qualification'),
              'aggregate_reference_qualification': aggregate.get('reference_qualification'),
              'interpretation': 'Shared FAIL is an observed outcome, not proof of a wrapper defect or a language defect. No expectation or denominator is changed.'}
    totals = {engine: Counter() for engine in metas}
    groups, failed_sources, failures, same_message = {}, set(), 0, 0
    parity = core.Parity()
    try:
        streams = {engine: iter(evidence.rows(directory / engine / 'results.jsonl')) for engine in metas}
        with (output / 'paired-failures.jsonl').open('x') as failed, (output / 'paired-cases.jsonl').open('x') as cases:
            for index, scheduled in enumerate(selected):
                rows = {engine: next(stream, None) for engine, stream in streams.items()}
                for engine, row in rows.items():
                    require(row is not None and (row['type'], row['test']) == ('test262', scheduled['test'])
                            and row['status'] == 'OK' and len(row.get('subtests', [])) == 1,
                            'Complete contiguous one-assertion Test262 row differs')
                    totals[engine].update(sub['status'] for sub in row['subtests'])
                parity.add(('test262', scheduled['test']), rows['lumen'], rows['chromium'])
                statuses = {engine: row['subtests'][0]['status'] for engine, row in rows.items()}
                json.dump({'index': index, 'schedule': scheduled, 'statuses': statuses}, cases, ensure_ascii=True)
                cases.write('\n')
                if all(status == 'PASS' for status in statuses.values()):
                    continue
                failures += 1
                failed_sources.add(scheduled['source'])
                same_message += (rows['lumen']['subtests'][0].get('message') == rows['chromium']['subtests'][0].get('message'))
                json.dump({'index': index, 'schedule': scheduled, 'rows': rows}, failed, ensure_ascii=True)
                failed.write('\n')
                for engine, row in rows.items():
                    sub = row['subtests'][0]
                    message = sub.get('message')
                    first = str(message).splitlines()[0] if message is not None and str(message).splitlines() else ''
                    key = (engine, sub['status'], first)
                    group = groups.setdefault(key, {'engine': engine, 'status': sub['status'], 'first_message_line': first,
                                                   'indices': [], 'sources': [], 'representative_test': scheduled['test']})
                    group['indices'].append(index)
                    if scheduled['source'] not in group['sources']:
                        group['sources'].append(scheduled['source'])
            require(all(next(stream, None) is None for stream in streams.values()), 'Raw rows exceed the exact200 schedule')
        require(all(dict(counts) == {'PASS': 167, 'FAIL': 33} for counts in totals.values()) and failures == 33
                and parity.value() == comparison['parity'], 'Observed outcomes differ from the completed comparison')
        source_records = []

        def excerpt(relative, role):
            relative_path = PurePosixPath(relative)
            require(not relative_path.is_absolute() and '..' not in relative_path.parts and '\\' not in relative,
                    'Source path is not a confined upstream path')
            git(['ls-files', '--error-unmatch', '--', relative])
            path = evidence.path(UPSTREAM / relative, UPSTREAM, maximum=2 * 1024 * 1024)
            text = path.read_text(encoding='utf-8', errors='strict')
            lines = text.splitlines(keepends=True)
            blocks = []
            if role == 'virtual_wrapper_implementation':
                nodes = [node for node in ast.parse(text).body if isinstance(node, ast.ClassDef)
                         and node.name.startswith('Test262')]
                require(nodes, 'Official Test262 wrapper classes were not found')
                for node in nodes:
                    blocks.append({'name': node.name, 'line_start': node.lineno, 'line_end': node.end_lineno,
                                   'text': ''.join(lines[node.lineno - 1:node.end_lineno])})
            else:
                blocks.append({'line_start': 1, 'line_end': min(len(lines), 300), 'text': ''.join(lines[:300])})
            require(sum(len(block['text'].encode()) for block in blocks) <= 192 * 1024, 'Source excerpt exceeds bounds')
            source_records.append({'source': relative, 'role': role, 'total_lines': len(lines),
                                   'sha256': evidence.files[str(path)]['sha256'], 'blocks': blocks,
                                   'excerpt_only': role == 'virtual_wrapper_implementation' or len(lines) > 300})

        for source in sorted(failed_sources):
            require(source.startswith(PREFIX), 'A failed source is outside the selected test subtree')
            excerpt(source, 'original_failed_test_source')
        for source, role in (
                ('tools/serve/serve.py', 'virtual_wrapper_implementation'),
                ('resources/test262/test262-reporter.js', 'test262_reporter'),
                ('resources/test262/test262-provider.js', 'test262_host_provider'),
                ('third_party/test262/harness/assert.js', 'test262_assert_harness'),
                ('third_party/test262/harness/sta.js', 'test262_error_harness')):
            excerpt(source, role)
        git(['diff', '--quiet', 'HEAD', '--'])
        require(git(['rev-parse', 'HEAD']) == REVISION, 'Upstream revision changed during extraction')
        evidence.unchanged()
        with (output / 'source-excerpts.json').open('x') as out:
            json.dump(source_records, out, indent=2, ensure_ascii=True)
            out.write('\n')
        report.update(status='complete', raw_outcomes={engine: dict(counts) for engine, counts in totals.items()},
                      failed_paired_files=failures, failed_pairs_with_equal_complete_message=same_message,
                      parity=parity.value(), message_groups=sorted(groups.values(), key=lambda item: (item['engine'], -len(item['indices']), item['first_message_line'])),
                      source_files=evidence.files, source_excerpt_count=len(source_records),
                      wrapper_note='The scheduled .test262.html URLs are virtual server wrappers; excerpts show the actual pinned implementation, not fabricated fetched HTML.')
    except Exception as error:
        report.update(status='failed_incomplete_diagnostic', error=type(error).__name__ + ': ' + str(error))
    with (output / 'summary.json').open('x') as out:
        json.dump(report, out, indent=2, ensure_ascii=True)
        out.write('\n')
    print(json.dumps({'status': report['status'], 'summary': str(output / 'summary.json'),
                      'raw_outcomes': report.get('raw_outcomes'), 'failed_paired_files': report.get('failed_paired_files'),
                      'equal_complete_message_pairs': report.get('failed_pairs_with_equal_complete_message'),
                      'message_group_count': len(groups), 'first_12_groups': [
                          {'engine': group['engine'], 'count': len(group['indices']),
                           'first_message_line': group['first_message_line'][:320],
                           'representative_test': group['representative_test'][:240]}
                          for group in report.get('message_groups', [])[:12]], 'error': report.get('error')}, ensure_ascii=True))
    return 0 if report['status'] == 'complete' else 1


if __name__ == '__main__':
    raise SystemExit(main())
