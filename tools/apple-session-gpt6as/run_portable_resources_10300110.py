"""Apply unchanged native scroll/navigation acceptance to the delivered app bytes."""
from datetime import datetime, timezone
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys
import time


def sha(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package-summary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path.cwd().resolve()
    if sys.platform != 'darwin' or str(root) != '/Users/devin/repos/lumen':
        parser.error('Use the actual canonical native Mac workspace')
    package_path = args.package_summary.resolve(); package_path.relative_to(root / 'out')
    package = json.loads(package_path.read_text())
    if not package.get('passed'):
        parser.error('Relocated package runtime acceptance must pass first')
    product_path = root / package['buildSummary']
    if sha(product_path) != package['buildSummarySHA256']:
        parser.error('Frozen source build receipt changed')
    product = json.loads(product_path.read_text())
    source_manifest = product_path.parent / 'compiled-source-sha256.json'
    if sha(source_manifest) != package['sourceManifestSHA256']:
        parser.error('Compiled source manifest differs')
    sources = json.loads(source_manifest.read_text())
    app = (root / package['relocation']['app']).resolve(); app.relative_to(root / 'out')
    executable = app / 'Contents/MacOS/lumen'
    expected = package['package']['binarySha256']
    if sha(executable) != expected:
        parser.error('The signed delivery executable changed')
    runner = root / 'tools/apple-session-gpt6as/run_native_resources_20261029_cpu30.py'
    runner_sha = sha(runner)
    for name, digest in sources.items():
        if sha(root / name) != digest:
            parser.error('Frozen compiled source differs: ' + name)
    out = args.output.resolve(); out.relative_to(root / 'out'); out.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    binding = {'passed': True,
        'scope': 'Source binding for the resource carrier: verified relinked/signed app; not a newly compiled build',
        'packageSummary': str(package_path.relative_to(root)), 'packageSummarySHA256': sha(package_path),
        'originalProductArtifact': product['artifacts']['lumen'],
        'sourceManifestSHA256': sha(source_manifest), 'sourceBaseCommit': product['sourceBaseCommit'],
        'artifacts': {'lumen': {'path': str(executable.relative_to(root)), 'sha256': expected,
                              'bytes': executable.stat().st_size}}}
    binding_path = out / 'package-resource-input.json'
    binding_path.write_text(json.dumps(binding, indent=2) + '\n')
    report = {'startedUTC': datetime.now(timezone.utc).isoformat(), 'accepted': False,
        'scope': 'Unchanged synthetic SDL scrolling and actual navigation workload on the signed relocated delivery app',
        'packageSummarySHA256': sha(package_path), 'runnerSHA256': runner_sha,
        'artifact': binding['artifacts']['lumen'], 'originalProductArtifact': binding['originalProductArtifact'],
        'sourceManifestSHA256': sha(source_manifest)}
    env = {key: value for key, value in os.environ.items() if not key.startswith('LUMEN_')}
    env['DEVELOPER_DIR'] = '/Library/Developer/CommandLineTools'
    try:
        with (out / 'runner.log').open('x') as log:
            result = subprocess.run([sys.executable, str(runner), '--build-summary', str(binding_path),
                '--output', str(out / 'workload')], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=390)
        workload_path = out / 'workload/summary.json'
        workload = json.loads(workload_path.read_text())
        report['workloadSummary'] = str(workload_path.relative_to(root))
        report['workloadSummarySHA256'] = sha(workload_path)
        report['scenarios'] = workload.get('scenarios')
        if result.returncode or not workload.get('accepted'):
            raise RuntimeError('Native signed-app resource acceptance failed: ' + str(workload.get('error')))
        if sha(executable) != expected or sha(runner) != runner_sha:
            raise RuntimeError('Package executable or workload wrapper changed during acceptance')
        for name, digest in sources.items():
            if sha(root / name) != digest:
                raise RuntimeError('Compiled source changed during signed-app resource acceptance')
        report['accepted'] = True
    except Exception as error:
        report['error'] = type(error).__name__ + ': ' + str(error)
    finally:
        report['finishedUTC'] = datetime.now(timezone.utc).isoformat()
        report['durationSeconds'] = round(time.monotonic() - started, 3)
        (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps({'accepted': report['accepted'], 'summary': str((out / 'summary.json').relative_to(root)),
            'artifactSHA256': expected, 'durationSeconds': report['durationSeconds'], 'error': report.get('error')}))
    return 0 if report['accepted'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
