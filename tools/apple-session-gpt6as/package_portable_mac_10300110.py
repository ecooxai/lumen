"""Relocate, sign and actually launch one frozen native Mac candidate.

The resulting development app carries its inspected prebuilt dylibs, ICU and CA
resources. This gate is narrower than notarization, another-Mac execution, iOS
compatibility or full browser acceptance. It never modifies installed libraries.
"""
from datetime import datetime, timezone
from pathlib import Path
import argparse
import csv
import hashlib
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import time


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-summary', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    root = Path.cwd().resolve()
    if sys.platform != 'darwin' or str(root) != '/Users/devin/repos/lumen':
        parser.error('Use the actual canonical native Mac workspace')
    sys.path.insert(0, str(root / 'tools'))
    from macos_deployment_closure import inspect_closure
    from macos_bundle_dependencies import require_embedded_closure, system_install_name
    product_path = args.build_summary.resolve()
    product_path.relative_to(root / 'out')
    product = json.loads(product_path.read_text())
    if not product.get('passed'):
        parser.error('A successful frozen native build/regression receipt is required')
    manifest = product_path.parent / 'compiled-source-sha256.json'
    if sha(manifest) != product['sourceManifestSHA256']:
        parser.error('The compiled product source manifest differs')
    sources = json.loads(manifest.read_text())
    tool_names = ['tools/mkdevapp.py', 'tools/macos_bundle_dependencies.py',
        'tools/macos_deployment_closure.py', 'tools/monitor_process.py',
        'tools/apple-session-gpt6as/package_portable_mac_10300110.py']
    tools = {name: sha(root / name) for name in tool_names}
    for name, digest in sources.items():
        if sha(root / name) != digest:
            parser.error('Frozen product source changed: ' + name)
    artifact = product['artifacts']['lumen']
    binary = (root / artifact['path']).resolve()
    binary.relative_to(root / 'out')
    if sha(binary) != artifact['sha256']:
        parser.error('The frozen native candidate executable differs')
    out = args.output.resolve()
    out.relative_to(root / 'out')
    out.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    env = {key: value for key, value in os.environ.items() if not key.startswith('LUMEN_')}
    env['DEVELOPER_DIR'] = '/Library/Developer/CommandLineTools'
    report = {'startedUTC': datetime.now(timezone.utc).isoformat(), 'passed': False,
        'scope': 'Relocated self-contained native macOS development app on this Mac; no mobile IPA, notarization or another-Mac claim',
        'sourceBaseCommit': product['sourceBaseCommit'], 'reviewedProductSource': product.get('reviewedProductSource'),
        'sourceManifestSHA256': sha(manifest), 'buildSummary': str(product_path.relative_to(root)),
        'buildSummarySHA256': sha(product_path),
        'productArtifact': artifact, 'reviewedToolHashes': tools, 'stages': [],
        'installedDependenciesModified': False, 'notarized': False,
        'olderMacOSRuntimeTested': False, 'sameDeviceIPAExecuted': False}

    def save():
        temporary = out / 'summary.json.tmp'
        temporary.write_text(json.dumps(report, indent=2) + '\n')
        temporary.replace(out / 'summary.json')

    def measured(name, command, limit, environment=env):
        prefix = out / name
        completed = subprocess.run([sys.executable, 'tools/monitor_process.py', '--duration', str(limit),
            '--settle', '0', '--output', str(prefix.relative_to(root)), '--', *command],
            env=environment, capture_output=True, text=True, timeout=limit + 35)
        (out / (name + '-collector.log')).write_text(completed.stdout + completed.stderr)
        data = json.loads(prefix.with_suffix('.json').read_text())
        okay = (data.get('natural_exit_code') == 0 and not data.get('collector_error')
            and not data.get('ended_by_monitor') and not data.get('owned_group_cleanup', {}).get('error'))
        report['stages'].append({'name': name, 'passed': okay, 'durationSeconds': data.get('duration_s'),
            'monitorSHA256': sha(prefix.with_suffix('.json')), 'logSHA256': sha(prefix.with_suffix('.log')),
            'rssPeakMiB': data.get('rss_peak_mb'), 'maxSamplingGapSeconds': data.get('max_sampling_gap_s'),
            'intervalCPU_P95Percent': data.get('steady_interval_cpu_p95_percent'),
            'intervalCPU_PeakPercent': data.get('interval_cpu_peak_percent'), 'cpuUnit': data.get('cpu_unit')})
        save()
        if not okay:
            raise RuntimeError(name + ' failed; raw monitor and log retained')
        return data

    try:
        staging, delivery = out / 'staging', out / 'delivery'
        staging.mkdir(); delivery.mkdir()
        app_name = 'GPT6-Astra-Pro_builtin-agents_devinmac3d_Lumen-10.30-0110.app'
        original_app = staging / app_name
        measured('package', [sys.executable, 'tools/mkdevapp.py', '--binary', str(binary),
            '--expected-binary-sha256', artifact['sha256'], '--output', str(original_app),
            '--identifier', 'ai.ecoox.lumen.gpt6as.10300110', '--bundle-dependencies'], 180)
        metadata = next(json.loads(line) for line in reversed((out / 'package.log').read_text().splitlines())
                        if line.startswith('{"app":'))
        if (metadata.get('binaryBeforeBundleSigningSha256') != artifact['sha256']
                or not metadata.get('expectedBinarySha256Verified')
                or not metadata.get('embeddedDependencies', {}).get('linkedClosureEmbedded')):
            raise RuntimeError('Self-contained packaging or immutable source proof is incomplete')
        report['package'] = metadata
        app = delivery / app_name
        original_app.rename(app)
        executable = app / 'Contents/MacOS/lumen'
        resources = app / 'Contents/Resources'
        if sha(executable) != metadata['binarySha256']:
            raise RuntimeError('Relocation changed the signed app executable')
        relocated = inspect_closure(root, executable)
        require_embedded_closure(relocated, app)
        if len(relocated['files']) != len(metadata['deploymentClosure']['files']):
            raise RuntimeError('The relocated dependency graph differs')
        for entry in metadata['runtimeResources']:
            if sha(resources / entry['name']) != entry['sha256']:
                raise RuntimeError('A runtime resource differs after relocation')
        report['relocation'] = {'app': str(app.relative_to(root)),
            'originalLocationAbsent': not original_app.exists(), 'deploymentClosure': relocated,
            'sourceAndBundleExecutableHashesDifferBySigningOrRelinking': artifact['sha256'] != sha(executable)}
        measured('signature', ['/usr/bin/codesign', '--verify', '--deep', '--strict', str(app)], 20)
        fixture = out / 'package-launch.html'
        fixture.write_text('''<!doctype html><html><head><meta charset="utf-8"><title>Lumen 10.30 01:10</title>
<style>body{background:#edf6ff;color:#142d48;font:20px system-ui;margin:40px}h1{font-size:36px}p{line-height:1.6}</style></head>
<body><h1>Lumen 10.30 01:10</h1><p>Self-contained native Mac development application</p><p id="status">Checking runtime…</p>
<script>try{let decoded=new TextDecoder().decode(new Uint8Array([76,117,109,101,110]));
let formatted=new Intl.DateTimeFormat('en-US',{timeZone:'UTC'}).format(new Date(0));
let segmenter=new Intl.Segmenter('ja',{granularity:'word'});let segments=Array.from(segmenter.segment('日本語'));
if(decoded!=='Lumen'||!formatted||!segments.length)throw Error('Runtime validation failed');
document.getElementById('status').textContent='V8, text decoding and ICU locale data: ready';
console.log('LUMEN_PORTABLE_MAC_READY');}catch(e){console.error('LUMEN_PORTABLE_MAC_FAILED',String(e));}</script></body></html>''')
        profile = out / 'launch-profile'; profile.mkdir()
        capture = out / 'GPT6-Astra-Pro_builtin-agents_devinmac3d_mac-app.png'
        launch_env = dict(env, LUMEN_PROFILE_DIR=str(profile), LUMEN_CONSOLE='1', LUMEN_NO_SIDEBAR='1',
            LUMEN_WINDOW='1000x700', LUMEN_MEM_STATS='1', LUMEN_SHOT_MS='16000', LUMEN_SHOT=str(capture))
        # Clear only owned-launch dyld overrides so a host library search path
        # cannot hide a missing bundle dependency. Existing network trust stays.
        for key in list(launch_env):
            if key.startswith('DYLD_'):
                del launch_env[key]
        launch_env['DYLD_PRINT_LIBRARIES'] = '1'
        monitor = measured('launch', [str(executable), fixture.as_uri()], 35, launch_env)
        log = (out / 'launch.log').read_text(errors='replace')
        if ('LUMEN_PORTABLE_MAC_READY' not in log or any(term in log for term in
                ('LUMEN_PORTABLE_MAC_FAILED', 'gpu: error', 'Validation Error', 'fatal signal', 'panicked at', 'panic in a function'))):
            raise RuntimeError('The relocated application did not prove its HTML/V8/ICU runtime')
        image_paths = {Path(match.group(1)).resolve() for line in log.splitlines()
                       if (match := re.match(r'^dyld\[\d+\]:\s+(?:<[^>]+>\s+)?(/.+)$', line))}
        expected_images = {Path(row['path']).resolve() for row in relocated['files']}
        missing = expected_images - image_paths
        outside = [str(path) for path in image_paths if not path.is_relative_to(app)
                   and not system_install_name(str(path))]
        report['loadedImages'] = {'observed': len(image_paths), 'requiredNonSystem': len(expected_images),
            'missingRequired': sorted(map(str, missing)), 'unexpectedExternalNonSystem': sorted(outside),
            'allRequiredBundleImagesObserved': not missing, 'paths': sorted(map(str, image_paths))}
        if missing or outside:
            raise RuntimeError('Actual dyld images do not establish the relocated bundle dependency closure')
        header = capture.read_bytes()[:33]
        if len(header) != 33 or header[:8] != b'\x89PNG\r\n\x1a\n' or header[12:16] != b'IHDR':
            raise RuntimeError('The native renderer did not produce its owned PNG capture')
        width, height = struct.unpack('>II', header[16:24])
        with (out / 'launch.csv').open() as stream:
            samples = list(csv.DictReader(stream))
        idle = [float(row['interval_cpu_percent']) for row in samples if row.get('interval_cpu_percent')
                and 6 <= float(row['elapsed_s']) <= 15]
        if len(idle) < 8 or monitor['max_sampling_gap_s'] > 1.5:
            raise RuntimeError('One-second packaged-app idle monitoring is incomplete')
        idle_p95 = sorted(idle)[math.ceil(.95 * len(idle)) - 1]
        report['launch'] = {'mode': 'Owned direct .app executable after directory relocation',
            'runtimeMarkerObserved': True, 'ICUSegmenterAndDateFormatterExecuted': True,
            'renderCapture': str(capture.relative_to(root)), 'renderCaptureSHA256': sha(capture),
            'width': width, 'height': height, 'renderCaptureIsOSWindowScreenshot': False,
            'idleP95OneCoreCPUPercent': idle_p95, 'idleSamples': len(idle),
            'idleScope': 'Simple static package launch page from elapsed6–15s; separate from interaction/navigation acceptance'}
        if idle_p95 > 3:
            raise RuntimeError('The packaged static page has abnormal post-startup idle CPU (>3% of one core)')
        (delivery / 'README-Mac-development.txt').write_text(
            'Lumen 10.30 01:10 — GPT-6 Astra Pro / built-in agents / devinmac3d\n\n'
            'Native arm64 macOS development app, tested after relocation on this Mac.\n'
            'The inspected prebuilt dylibs, matching V8 ICU data and CA bundle are included.\n'
            'Actual dyld loaded images were checked for external non-system dependencies.\n'
            'Ad-hoc signed; not notarized. It is not a signed iPhone/iPad device IPA.\n'
            'Another Mac and older macOS versions have not been tested.\n'
            'Declared minimum from actual dependencies: ' + metadata['minimumSystemVersion'] + '\n'
            'Product source: ' + product['sourceBaseCommit'] + '\n'
            'Original candidate SHA-256: ' + artifact['sha256'] + '\n'
            'Relinked/signed app executable SHA-256: ' + metadata['binarySha256'] + '\n')
        (delivery / 'dependency-closure.json').write_text(json.dumps({'package': metadata,
            'relocation': report['relocation'], 'actualLoadedImages': report['loadedImages']}, indent=2) + '\n')
        archive = out / 'GPT6-Astra-Pro_builtin-agents_devinmac3d_Lumen-10.30-0110-Mac-development.zip'
        measured('archive', ['/usr/bin/ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(delivery), str(archive)], 120)
        report['archive'] = {'path': str(archive.relative_to(root)), 'bytes': archive.stat().st_size, 'sha256': sha(archive)}
        if sha(binary) != artifact['sha256'] or sha(executable) != metadata['binarySha256']:
            raise RuntimeError('Frozen source or signed bundle changed during validation')
        for name, digest in {**sources, **tools}.items():
            if sha(root / name) != digest:
                raise RuntimeError('Reviewed source/tool changed during native package validation: ' + name)
        report['passed'] = True
    except Exception as error:
        report['error'] = type(error).__name__ + ': ' + str(error)
    finally:
        report['finishedUTC'] = datetime.now(timezone.utc).isoformat()
        report['durationSeconds'] = round(time.monotonic() - started, 3)
        save()
        print(json.dumps({'passed': report['passed'], 'summary': str((out / 'summary.json').relative_to(root)),
            'archive': report.get('archive'), 'durationSeconds': report['durationSeconds'], 'error': report.get('error')}), flush=True)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
