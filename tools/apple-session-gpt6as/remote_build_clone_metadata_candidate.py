#!/usr/bin/env python3
"""Build and measure a reviewed native clone-metadata candidate without V8 rebuilds."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone


def utc():
    return datetime.now(timezone.utc).isoformat()


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def save(path, value):
    pending = path.with_suffix('.tmp')
    pending.write_text(json.dumps(value, indent=2) + '\n')
    pending.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--label', default='clone-metadata-green-1')
    parser.add_argument('--worker', action='store_true')
    args = parser.parse_args()
    if not args.label or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in args.label):
        raise RuntimeError('Invalid immutable candidate label')
    root = Path.cwd().resolve()
    out = root / 'output/session-10290244' / args.label
    build = 'build-apple-load-20261029'
    if not args.worker:
        subprocess.run([sys.executable, 'tools/apple-session-gpt6as/launch_job.py',
            '--output', str(out.relative_to(root)) + '-job', '--timeout', '420', '--',
            sys.executable, str(Path(__file__).resolve()), '--label', args.label, '--worker'], check=True)
        return 0

    expected = json.loads((root / 'tools/apple-session-gpt6as/clone-metadata-source-manifest.json').read_text())
    for row in expected:
        if sha(root / row['path']) != row['after']:
            raise RuntimeError('Reviewed production source changed: ' + row['path'])
    tests = {
        'tests/test_clone_document_metadata.py': 'af3638e12ec8dd6e2987d46d46ac70df57f032fee74da2b5315ba91bbfa4a85c',
        'tests/test_xml_frame_documents.py': '482134caf871c3683f48ce2facd8e74ff17160dea291204cae84d8f39420c73b',
        'tests/test_html_interface_clone_brands.py': '7f8f6c0dd74ab9c751cee3292ffb3755fa2630c9ae6b84b940ad399242cc7583',
        'tests/js/html-interface-clone-brands.html': 'b2f8fcfc554cd5625d3a5999e981d9ba967571cfc0afd3bac9466e50ce715232',
    }
    for name, expected_hash in tests.items():
        if sha(root / name) != expected_hash:
            raise RuntimeError('Reviewed regression test changed: ' + name)
    out.mkdir(parents=True, exist_ok=False)
    names = subprocess.check_output(['git', 'ls-files', '-z', '--', 'src', 'Makefile', 'tests/jsrun.c'], text=True).split('\0')
    source_hashes = {name: sha(root / name) for name in names if name and (root / name).is_file()}
    source_hashes.update({name: sha(root / name) for name in tests})
    save(out / 'compiled-source-sha256.json', source_hashes)
    summary = {
        'startedUTC': utc(), 'sourceBaseCommit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
        'candidateFiles': expected, 'build': build, 'prebuiltV8': True, 'rtcEnabled': True, 'webtransportEnabled': True,
        'scope': 'Native DOM clone/import ownership, qualified names, document metadata, Attr values and template clone semantics; not full DOM/IDL conformance.',
        'stages': [], 'artifacts': {},
    }

    def measured(name, command, duration, marker=None):
        prefix = out / name
        started = time.monotonic()
        start_utc = utc()
        result = subprocess.run([sys.executable, 'tools/monitor_process.py', '--duration', str(duration),
            '--settle', '0', '--output', str(prefix.relative_to(root)), '--', *command],
            text=True, capture_output=True, timeout=duration + 35)
        (out / (name + '-collector.log')).write_text(result.stdout + result.stderr)
        monitor = json.loads(prefix.with_suffix('.json').read_text())
        log = prefix.with_suffix('.log').read_text(errors='replace')
        passed = monitor.get('natural_exit_code') == 0 and not monitor.get('ended_by_monitor') and not monitor.get('collector_error')
        passed = passed and not (monitor.get('owned_group_cleanup') or {}).get('error') and (marker is None or marker in log)
        summary['stages'].append({
            'stage': name, 'passed': passed, 'startedUTC': start_utc, 'finishedUTC': utc(),
            'durationSeconds': round(time.monotonic() - started, 3), 'command': command,
            'monitor': str(prefix.with_suffix('.json').relative_to(root)),
            'naturalExitCode': monitor.get('natural_exit_code'), 'endedByMonitor': monitor.get('ended_by_monitor'),
            'intervalCpuSamples': monitor.get('interval_cpu_samples'), 'intervalCpuPeakPercent': monitor.get('interval_cpu_peak_percent'),
            'rssPeakMiB': monitor.get('rss_peak_mb'),
            'performanceNote': 'Short stages with fewer than ten CPU intervals do not establish an idle/performance verdict.',
            'resultLines': [line for line in log.splitlines() if 'RESULTS ' in line or line.startswith('CLONE_BRANDS_RECEIPT')],
        })
        save(out / 'summary.json', summary)
        return passed

    targets = ['lumen', 'jsrun', 'test_frame_removal', 'test_wasm_async_deadline']
    built = measured('build', ['make', '-j3', 'BUILD=' + build, 'MODE=release', 'RTC=1', 'WT=1',
        *[build + '/' + name for name in targets]], 180)
    if built:
        for name in targets:
            path = root / build / name
            digest = sha(path)
            record = {'path': str(path.relative_to(root)), 'sha256': digest}
            if name in ('lumen', 'jsrun'):
                frozen = path.with_name(name + '-clone-metadata-' + digest[:16])
                if frozen.exists():
                    if sha(frozen) != digest:
                        raise RuntimeError('Immutable snapshot collision: ' + str(frozen))
                else:
                    shutil.copy2(path, frozen)
                    frozen.chmod(0o555)
                record['snapshot'] = str(frozen.relative_to(root))
            summary['artifacts'][name] = record
        artifact = summary['artifacts']['jsrun']
        measured('clone-metadata', [sys.executable, 'tests/test_clone_document_metadata.py', '--binary', artifact['snapshot']], 25, 'RESULTS clone-metadata PASS (36/36 checks)')
        measured('clone-brands', [sys.executable, 'tests/test_html_interface_clone_brands.py', '--binary', artifact['snapshot'],
            '--expected-sha256', artifact['sha256']], 25, 'RESULTS html-interface-clone-brands PASS (132/132 checks)')
        measured('xml-documents', [sys.executable, 'tests/test_xml_frame_documents.py', '--binary', artifact['snapshot']], 40, 'RESULTS xml-frame-documents PASS (33/33 checks)')
        for runtime, name in [('jsrun', 'jsrun'), ('application', 'lumen')]:
            measured('frame-load-' + runtime, [sys.executable, 'tests/test_frame_load_order.py', '--binary',
                summary['artifacts'][name]['snapshot'], '--runtime', runtime], 30, 'RESULTS frame-load-order ' + runtime + ' PASS (14/14 checks)')
        for name, marker in [('test_frame_removal', 'RESULTS frame-removal PASS'), ('test_wasm_async_deadline', 'RESULTS wasm-async-deadline PASS')]:
            profile = out / (name + '-profile')
            profile.mkdir()
            measured(name, ['env', 'LUMEN_PROFILE_DIR=' + str(profile), str(root / build / name)], 30, marker)
    for name, expected_hash in source_hashes.items():
        if sha(root / name) != expected_hash:
            raise RuntimeError('Compiled source changed before validation completed: ' + name)
    summary['passed'] = built and all(row['passed'] for row in summary['stages'])
    summary['finishedUTC'] = utc()
    save(out / 'summary.json', summary)
    print(json.dumps({'summary': str((out / 'summary.json').relative_to(root)), 'passed': summary['passed'],
        'stages': [{k: row[k] for k in ('stage', 'passed', 'durationSeconds')} for row in summary['stages']], 'artifacts': summary['artifacts']}))
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
