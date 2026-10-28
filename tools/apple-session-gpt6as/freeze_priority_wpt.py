#!/usr/bin/env python3
"""Freeze the predeclared priority selection before examining browser outcomes."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
from datetime import datetime, timezone

PIN = '7f8d49b5a82606efc771f43d10aad5bce5ccbfeb'
CATEGORIES = ('dom', 'events', 'url', 'encoding', 'headers', 'microtasks', 'storage')
POLICY = {
    'schema': 'lume-priority-wpt-policy-10290244-v1',
    'upstream_revision': PIN,
    'declared_before_execution': True,
    'types': ['testharness'],
    'generated_variants': 'All official manifest variants of selected source families, including worker, HTTPS and query variants; none removed after outcomes.',
    'categories': {
        'dom': {'prefix': 'dom/nodes/', 'basename_pattern': r'^(Document-createElement|Document-createAttribute|Node-cloneNode|Document-importNode|Document-adoptNode)', 'case_insensitive': True},
        'events': {'prefix': 'dom/events/', 'basename_pattern': r'^(EventTarget|Event-constructors?|CustomEvent)', 'case_insensitive': True},
        'url': {'prefix': 'url/', 'all_sources': True},
        'encoding': {'prefix': 'encoding/', 'all_sources': True},
        'headers': {'prefix': 'fetch/api/headers/', 'all_sources': True},
        'microtasks': {'prefix': 'html/webappapis/microtask-queuing/', 'all_sources': True},
        'storage': {'prefix': 'webstorage/', 'basename_pattern': r'^storage_(clear|getitem|key|length|removeitem|setitem)', 'case_insensitive': True},
    },
    'limitations': [
        'This is a declared high-priority subset of a sparse pinned upstream checkout, not a full WPT run.',
        'The upstream manifest/server and unchanged assertions are used with the Lumen custom testharness execution adapter.',
        'No testdriver/WebDriver actions are implemented; automation-dependent failures must be identified and retained separately.',
        'An incomplete invocation retains unrun files in the frozen inventory; missing assertions cannot count as passing.',
        'Aggregate assertion counts do not replace per-category file completion and matched Chrome-pass retention.',
    ],
}


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def file_sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def category(row):
    if row['type'] != 'testharness':
        return None
    for name in CATEGORIES:
        rule = POLICY['categories'][name]
        if not row['source'].startswith(rule['prefix']):
            continue
        if rule.get('all_sources') or re.match(rule['basename_pattern'], Path(row['source']).name, re.I):
            return name
    return None


def context(url):
    if '.sharedworker.' in url:
        return 'sharedworker'
    if '.serviceworker.' in url:
        return 'serviceworker'
    if '.worker.' in url:
        return 'dedicatedworker'
    if 'shadowrealm' in url:
        return 'shadowrealm'
    return 'window-or-other'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    inventory = json.loads(args.inventory.read_text())
    if inventory['wpt_revision'] != PIN:
        raise RuntimeError('The priority suite must use the same pinned upstream revision as the original baseline')
    root = Path(inventory['wpt_root']).resolve()
    if file_sha(root / 'MANIFEST.json') != inventory['manifest_sha256']:
        raise RuntimeError('Upstream manifest differs from inventory')
    if subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip() != PIN:
        raise RuntimeError('Upstream checkout differs from the policy')
    if subprocess.check_output(['git', '-C', str(root), 'diff', '--binary', 'HEAD', '--']):
        raise RuntimeError('Do not run altered upstream assertions')
    selected = sorted((row for row in inventory['tests'] if category(row)), key=lambda row: (row['type'], row['test']))
    if not selected:
        raise RuntimeError('The declared priority selection is empty')
    urls = [row['test'] for row in selected]
    if len(urls) != len(set(urls)):
        raise RuntimeError('Ambiguous duplicate manifest URL')
    # Existing full_runner uses URL prefixes. Verify that these exact URL
    # prefixes reproduce the frozen set and cannot accidentally add other files.
    expanded = sorted((row for row in inventory['tests'] if row['type'] == 'testharness'
        and any(row['test'].startswith(prefix) for prefix in urls)), key=lambda row: (row['type'], row['test']))
    if expanded != selected:
        raise RuntimeError('Runner prefix selection would add cases beyond the declared policy')
    source_hashes = {}
    direct_testdriver_sources = []
    for source in sorted({row['source'] for row in selected}):
        path = (root / source).resolve(strict=True)
        path.relative_to(root)
        source_hashes[source] = file_sha(path)
        text = path.read_text(errors='replace')
        if re.search(r'testdriver(?:\.js|-vendor\.js)|test_driver\.', text):
            direct_testdriver_sources.append(source)
    counts = {}
    for name in CATEGORIES:
        rows = [row for row in selected if category(row) == name]
        if not rows:
            raise RuntimeError('Declared category selected no tests; inspect filenames before any execution: ' + name)
        counts[name] = {
            'variants': len(rows), 'source_files': len({row['source'] for row in rows}),
            'contexts': dict(Counter(context(row['test']) for row in rows)),
            'direct_testdriver_source_files': sum(source in direct_testdriver_sources for source in {row['source'] for row in rows}),
        }
    frozen = {
        'schema': 'lume-priority-wpt-selection-10290244-v1',
        'frozen_utc': datetime.now(timezone.utc).isoformat(),
        'policy': POLICY, 'policy_sha256': digest(POLICY),
        'wpt_revision': PIN, 'manifest_sha256': inventory['manifest_sha256'],
        'inventory_path': str(args.inventory.resolve()), 'inventory_sha256': file_sha(args.inventory),
        'schedule_sha256': digest(selected), 'selected_variants': len(selected),
        'selected_source_files': len(source_hashes), 'category_counts': counts,
        'selection_arguments': {'types': 'testharness', 'prefixes': urls, 'offset': 0, 'limit': None},
        'all_generated_variants_preserved': True,
        'direct_testdriver_sources': direct_testdriver_sources,
        'dependency_audit_note': 'The direct testdriver scan is a lower bound; indirect helper imports may also require automation.',
        'source_sha256': source_hashes, 'schedule': selected,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x') as output:
        json.dump(frozen, output, indent=2)
        output.write('\n')
    print(json.dumps({key: frozen[key] for key in ('schema', 'policy_sha256', 'manifest_sha256', 'inventory_sha256',
        'schedule_sha256', 'selected_variants', 'selected_source_files', 'category_counts', 'direct_testdriver_sources')}, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
