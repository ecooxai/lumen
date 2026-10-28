#!/usr/bin/env python3
"""Complete native before/after identities, harness transitions, and reference matches."""
import argparse
import collections
import datetime
import hashlib
import importlib.util
import json
from pathlib import Path


def digest(path):
    p = Path(path)
    return {'path': str(p), 'bytes': p.stat().st_size,
            'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}


def compare(urls, baseline, candidate, chromium, helpers):
    lanes = {'baseline': baseline, 'candidate': candidate, 'chromium': chromium}
    summary = {}
    for name, data in lanes.items():
        selected = [data[u] for u in urls if u in data]
        summary[name] = {
            'observed_urls': len(selected),
            'missing_urls': len(urls) - len(selected),
            'unselected_reported_urls': len(set(data) - set(urls)),
            'observed_assertions': sum(len(x['tests']) for x in selected),
            'passing_assertions': sum(len(helpers.passed(x)) for x in selected),
            'fully_passing_urls': sum(helpers.fully(x) for x in selected),
            'harness_ok_zero_assertions': sum(x['status'] == 'OK' and not x['tests'] for x in selected),
            'harness_statuses': dict(collections.Counter(x['status'] for x in selected)),
        }
    transitions = collections.Counter()
    assertion_transitions = collections.Counter()
    changes = []
    url_details = []
    totals = collections.Counter()
    for url in urls:
        rows = {name: data.get(url) for name, data in lanes.items()}
        states = {name: helpers.states(row) if row is not None else {} for name, row in rows.items()}
        b, c, ref = states['baseline'], states['candidate'], states['chromium']
        bh = rows['baseline']['status'] if rows['baseline'] is not None else 'MISSING'
        ch = rows['candidate']['status'] if rows['candidate'] is not None else 'MISSING'
        transitions[(bh, ch)] += 1
        detail = {'url': url, 'baseline_harness': bh, 'candidate_harness': ch,
                  'chromium_harness': rows['chromium']['status'] if rows['chromium'] is not None else 'MISSING',
                  'baseline_assertions': len(b), 'candidate_assertions': len(c),
                  'observed_assertion_delta': len(c) - len(b)}
        counts = collections.Counter()
        for key in sorted(set(b) | set(c)):
            old, new = b.get(key, 'MISSING'), c.get(key, 'MISSING')
            assertion_transitions[(old, new)] += 1
            if key in b and key in c:
                counts['common_assertions'] += 1
            elif key in c:
                counts['newly_observed_assertions'] += 1
            else:
                counts['baseline_assertions_not_observed'] += 1
            if old != 'PASS' and new == 'PASS':
                counts['gained_passes'] += 1
                counts['newly_observed_passes' if old == 'MISSING' else 'existing_assertions_gained_pass'] += 1
            if old == 'PASS' and new != 'PASS':
                counts['lost_passes'] += 1
                counts['lost_passes_not_observed' if new == 'MISSING' else 'lost_passes_to_observed_nonpass'] += 1
            if old != new:
                changes.append({'identity': helpers.identity(url, key), 'baseline': old,
                                'candidate': new, 'chromium': ref.get(key, 'MISSING'),
                                'baseline_message': rows['baseline']['tests'][key].get('message') if key in b else None,
                                'candidate_message': rows['candidate']['tests'][key].get('message') if key in c else None})
        for key, state in ref.items():
            if key not in c:
                counts['candidate_missing_chromium_assertions'] += 1
            if state == 'PASS':
                counts['chromium_passing_assertions'] += 1
                counts['baseline_matches_chromium_pass'] += b.get(key) == 'PASS'
                counts['candidate_matches_chromium_pass'] += c.get(key) == 'PASS'
                counts['candidate_missing_chromium_pass'] += key not in c
        detail.update(counts)
        url_details.append(detail)
        totals.update(counts)
    for key in ('common_assertions', 'newly_observed_assertions', 'baseline_assertions_not_observed', 'gained_passes', 'newly_observed_passes', 'existing_assertions_gained_pass', 'lost_passes', 'lost_passes_not_observed', 'lost_passes_to_observed_nonpass', 'chromium_passing_assertions', 'baseline_matches_chromium_pass', 'candidate_matches_chromium_pass', 'candidate_missing_chromium_assertions', 'candidate_missing_chromium_pass'):
        totals.setdefault(key, 0)
    totals['observed_assertion_delta'] = summary['candidate']['observed_assertions'] - summary['baseline']['observed_assertions']
    totals['passing_assertion_delta'] = summary['candidate']['passing_assertions'] - summary['baseline']['passing_assertions']
    return {
        'selected_urls': len(urls), 'summary': summary, 'totals': dict(totals),
        'harness_transitions': [{'baseline': b, 'candidate': c, 'urls': n} for (b, c), n in sorted(transitions.items())],
        'assertion_transitions': [{'baseline': b, 'candidate': c, 'assertions': n} for (b, c), n in sorted(assertion_transitions.items())],
        'changed_assertion_identities': changes, 'per_url': url_details,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('urls', 'baseline', 'candidate', 'chromium', 'output'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--provenance', action='append', default=[])
    args = parser.parse_args()
    helper_path = Path('agent/validation/compare_wpt_runs.py')
    spec = importlib.util.spec_from_file_location('lumen_compare', helper_path)
    helpers = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helpers)
    selected = json.loads(Path(args.urls).read_text())
    urls = selected['urls'] if isinstance(selected, dict) else selected
    if not all(isinstance(u, str) for u in urls) or len(set(urls)) != len(urls):
        raise ValueError('Selection must contain unique URL strings')
    data, duplicates, input_hashes = {}, {}, {'urls': digest(args.urls)}
    for name in ('baseline', 'candidate', 'chromium'):
        input_hashes[name] = digest(getattr(args, name))
        data[name], duplicates[name] = helpers.read(Path(getattr(args, name)))
        if digest(getattr(args, name)) != input_hashes[name]:
            raise RuntimeError('Input report changed while being read: ' + name)
    report = compare(urls, data['baseline'], data['candidate'], data['chromium'], helpers)
    report.update(created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  identity_policy='Exact URL plus full assertion name and duplicate occurrence; no port or text normalization.',
                  duplicate_url_records=duplicates,
                  inputs=input_hashes,
                  tooling=[digest(helper_path), digest(__file__)],
                  provenance=[digest(p) for p in args.provenance],
                  caveats=[
                      'MISSING means no observed row/identity, not a synthetic assertion outcome.',
                      'Newly observed PASS identities are separated from changed outcomes for previously observed assertions.',
                      'Fully passing URLs require harnessOK, at least one observed assertion, and every observed assertionPASS.',
                      'NativeERROR/TIMEOUT and ChromiumCRASH are different runner classifications, not a crash-rate comparison.',
                      'The same frozen selection does not imply identical execution order: official Chromium groups by directory.',
                      'Timing and reuse-order differences must be interpreted using retained run metadata, not as browsing performance.',
                  ])
    Path(args.output).write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'output': args.output, 'summary': report['summary'], 'totals': report['totals']}))


if __name__ == '__main__':
    main()
