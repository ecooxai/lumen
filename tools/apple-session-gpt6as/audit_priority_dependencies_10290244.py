#!/usr/bin/env python3
"""Read-only, conservative audit of direct WPT scripts/styles/import helpers.

This is not a JavaScript interpreter or proof of complete network dependency
coverage. Dynamic fetch/navigation targets are not classified as missing
helpers: many upstream assertions deliberately request nonexistent resources.
"""
from collections import deque
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
from urllib.parse import unquote, urlsplit

ROOT = Path.cwd()
BASE = ROOT / 'output/wpt-10290244'
frozen = json.loads((BASE / 'priority-selection-v1.json').read_text())
inventory = json.loads(Path(frozen['inventory_path']).read_text())
UPSTREAM = Path(inventory['wpt_root'])
if frozen['schedule_sha256'] != 'adcbf9719f722368b1e53ac1bd61cf38e566d3bd7495d9d6127cad38dd6aeabe':
    raise RuntimeError('Unexpected frozen selection')
tracked = set(subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', 'HEAD'], cwd=UPSTREAM, text=True).splitlines())
PATTERNS = [
    ('script-src', re.compile(r'<script\b[^>]*\bsrc\s*=\s*[\"\']([^\"\']+)[\"\']', re.I)),
    ('stylesheet', re.compile(r'<link\b[^>]*\bhref\s*=\s*[\"\']([^\"\']+)[\"\']', re.I)),
    ('module-import', re.compile(r'\b(?:import|export)\s+(?:[^;\n]*?\s+from\s+)?[\"\']([^\"\']+)[\"\']')),
    ('meta-script', re.compile(r'^\s*//\s*META:\s*script\s*=\s*(\S+)', re.M)),
]
FUNCTIONS = re.compile(r'\b(importScripts|loadScript|loadScripts|load_js|loadJS)\s*\(([^)]*)\)')
STRINGS = re.compile(r'[\"\']([^\"\']+)[\"\']')
MAX_SOURCE_BYTES = 2 * 1024 * 1024
queue = deque((name, None) for name in frozen['source_sha256'])
seen, references, unresolved, missing, generated, outside, too_large = set(), [], [], [], [], [], []


def resolve(source, url):
    if '{{' in url or '${' in url:
        return None, 'template-expression'
    parts = urlsplit(url)
    if parts.scheme or parts.netloc:
        return None, 'absolute-network-url'
    path = unquote(parts.path)
    if not path:
        return None, 'empty-path'
    # URL path resolution, not filesystem symlink resolution.
    candidate = path.lstrip('/') if path.startswith('/') else str(Path(source).parent / path)
    stack = []
    for component in candidate.split('/'):
        if component in ('', '.'):
            continue
        if component == '..':
            if not stack:
                return None, 'path-outside-upstream'
            stack.pop()
        else:
            stack.append(component)
    return '/'.join(stack), None


while queue:
    source, imported_by = queue.popleft()
    if source in seen:
        continue
    seen.add(source)
    path = UPSTREAM / source
    if not path.is_file():
        missing.append({'path': source, 'referenced_by': imported_by, 'tracked_upstream': source in tracked})
        continue
    if path.stat().st_size > MAX_SOURCE_BYTES:
        too_large.append(source)
        continue
    if path.suffix.lower() not in ('.js', '.html', '.htm', '.xhtml', '.css', '.svg', '.mjs'):
        continue
    text = path.read_text(errors='replace')
    found = [(label, match.group(1)) for label, pattern in PATTERNS for match in pattern.finditer(text)]
    for function in FUNCTIONS.finditer(text):
        found.extend((function.group(1), value) for value in STRINGS.findall(function.group(2)))
    for kind, url in found:
        candidate, reason = resolve(source, url)
        ref = {'source': source, 'kind': kind, 'url': url, 'resolved': candidate}
        if reason:
            ref['reason'] = reason
            (outside if reason == 'absolute-network-url' else unresolved).append(ref)
            continue
        references.append(ref)
        # Reporter is intentionally supplied through an exact file alias.
        if candidate == 'resources/testharnessreport.js':
            continue
        if candidate in tracked or (UPSTREAM / candidate).exists():
            queue.append((candidate, source))
            continue
        # .sub variants and WPT generated wrapper URLs can have a different
        # source filename. Retain these as unresolved, not missing assertions.
        if '.any.' in candidate or '.worker.' in candidate or '.sub.' in candidate:
            generated.append(ref)
        else:
            unresolved.append(dict(ref, reason='not-a-tracked-literal-helper'))

receipt = {
    'schema': 1, 'created_utc': datetime.now(timezone.utc).isoformat(),
    'wpt_revision': frozen['wpt_revision'], 'selection_schedule_sha256': frozen['schedule_sha256'],
    'selection_file_sha256': hashlib.sha256((BASE / 'priority-selection-v1.json').read_bytes()).hexdigest(),
    'selected_sources': len(frozen['source_sha256']), 'visited_sources_and_helpers': len(seen),
    'literal_helper_references': len(references), 'missing_tracked_helpers': missing,
    'unresolved_literals_or_templates': unresolved, 'generated_wrapper_references': generated,
    'absolute_network_references': outside, 'oversize_unscanned_helpers': too_large,
    'limits': [
        'Static literal helper scan only; does not establish complete runtime fixture readiness.',
        'Dynamic fetch, navigation, worker-constructor and negative-test request targets are not automatically classified.',
        'Tracked missing helper files are sparse-checkout fixture gaps, not browser failures.',
        'No upstream file, manifest, selection or browser assertion was changed by this audit.'
    ]
}
target = BASE / 'priority-helper-audit.json'
with target.open('x') as output:
    json.dump(receipt, output, indent=2)
    output.write('\n')
print(json.dumps({key: receipt[key] for key in ('selected_sources', 'visited_sources_and_helpers',
    'literal_helper_references', 'missing_tracked_helpers', 'oversize_unscanned_helpers')}, indent=2))
print(json.dumps({'audit_file': str(target), 'unresolved_reference_count': len(unresolved),
                  'generated_reference_count': len(generated), 'absolute_network_reference_count': len(outside)}))
