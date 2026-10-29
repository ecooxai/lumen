#!/usr/bin/env python3
"""Run frozen SharedWorker public-API fixtures through the actual native Mac GUI.

The page/worker bytes and expected names come directly from the frozen console
oracles; this adapter does not inject APIs or substitute a headless engine.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import http.server
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import threading
import time


ROOT = Path(__file__).resolve().parents[1]
SUITES = {
    'core': ('test_shared_worker.py', 'SHARED_WORKER_CHECK ', 'SHARED_WORKER_RESULTS ',
        'SHARED_WORKER_INFRA ', 'bfce5fed315f65f67087c6bf231b83217aec34c1efae9921566c16f84e4907d0'),
    'boundaries': ('test_shared_worker_boundaries.py', 'SHARED_BOUNDARY_CHECK ', 'SHARED_BOUNDARY_RESULTS ',
        'SHARED_BOUNDARY_INFRA ', 'ae9398fdb1c16007efce49415b503058b89bc0ffff101ba921c5f81ef98e11cf'),
}


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def load(name, path):
    specification = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--suite', choices=SUITES, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        raise RuntimeError('This acceptance adapter requires the actual native Mac GUI')
    if args.output.exists():
        raise RuntimeError('Preserve existing evidence; choose a fresh output')
    binary = args.binary.resolve(strict=True)
    if sha(binary) != args.sha256:
        raise RuntimeError('Native GUI binary differs from the explicitly selected artifact')
    name, case_marker, done_marker, infra_marker, expected_fixture = SUITES[args.suite]
    fixture_path = ROOT / 'tests' / name
    if sha(fixture_path) != expected_fixture:
        raise RuntimeError('Frozen SharedWorker oracle bytes changed')
    fixture = load('shared_worker_gui_' + args.suite, fixture_path)
    common_path = ROOT / 'tests/wpt/full_runner.py'
    common = load('shared_worker_gui_owned_transport', common_path)
    error = common.monitor_module().pid_namespace_error()
    if error:
        raise RuntimeError(error)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    prefix = args.output.with_suffix('')
    log_path = prefix.with_suffix('.stdout.log')
    profile = Path(tempfile.mkdtemp(prefix='owned-shared-worker-gui-', dir=args.output.parent))
    (profile / 'downloads').mkdir()
    servers = [http.server.ThreadingHTTPServer(('127.0.0.1', 0), fixture.Handler) for _ in range(2)]
    origins = ['http://127.0.0.1:' + str(server.server_port) for server in servers]
    page = fixture.PAGE.replace('__CROSS_ORIGIN__', json.dumps(origins[1])).encode()
    threads = []
    for server in servers:
        server.page, server.daemon_threads = page, True
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        threads.append(thread)
    environment = {key: value for key, value in os.environ.items() if not key.startswith(('JSRUN_', 'LUMEN_'))}
    environment.update(LUMEN_CONSOLE='1', LUMEN_PROFILE_DIR=str(profile),
        LUMEN_DOWNLOAD_DIR=str(profile / 'downloads'))
    start = time.monotonic()
    receipt = {'kind': 'actual-native-mac-shared-worker-gui', 'officialWPT': False,
        'suite': args.suite, 'nativeGui': True, 'nativeLumenExecuted': False,
        'binary': str(binary), 'binarySHA256': args.sha256, 'fixtureSHA256': expected_fixture,
        'adapterSHA256': sha(__file__), 'ownedTransportSHA256': sha(common_path),
        'pageTemplateSHA256': hashlib.sha256(fixture.PAGE.encode()).hexdigest(),
        'workerSHA256': hashlib.sha256(fixture.WORKER.encode()).hexdigest(),
        'startedUTC': datetime.now(timezone.utc).isoformat(), 'rows': [], 'infrastructureErrors': []}
    process = sampler = None
    text = ''
    try:
        with log_path.open('wb') as log:
            command = [str(binary), origins[0] + '/']
            process = subprocess.Popen(command, cwd=ROOT, env=environment,
                stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            receipt['nativeLumenExecuted'] = True
            sampler = common.Sampler(process.pid, prefix.with_suffix('.metrics.jsonl'))
            deadline = time.monotonic() + 65
            while time.monotonic() < deadline:
                text = log_path.read_text(errors='replace')
                if done_marker in text or process.poll() is not None:
                    break
                time.sleep(0.025)
            receipt['naturalExitCode'] = process.poll()
    except Exception as failure:
        receipt['infrastructureErrors'].append(type(failure).__name__ + ': ' + str(failure))
    finally:
        if sampler:
            receipt['metrics'] = sampler.stop()
        if process:
            receipt['cleanup'] = common.terminate_owned(process, sampler)
        cleanup = receipt.get('cleanup') or {}
        if not cleanup.get('error') and cleanup.get('process_group_gone') is True:
            shutil.rmtree(profile)
            receipt['profileRemoved'] = True
        for server in servers:
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join(timeout=2)
    summaries = []
    for line in text.splitlines():
        for marker, destination in [(case_marker, receipt['rows']), (done_marker, summaries),
                                    (infra_marker, receipt['infrastructureErrors'])]:
            at = line.find(marker)
            if at >= 0:
                try:
                    destination.append(json.loads(line[at + len(marker):]))
                except json.JSONDecodeError as failure:
                    receipt['infrastructureErrors'].append('Malformed result JSON: ' + str(failure))
    rows, expected = receipt['rows'], fixture.EXPECTED_NAMES
    if [row.get('i') for row in rows] != list(range(len(expected))) or [row.get('name') for row in rows] != expected:
        receipt['infrastructureErrors'].append('Frozen assertion identities are missing, changed, reordered, or duplicated')
    summary = {'total': len(expected), 'passed': sum(row.get('ok') is True for row in rows),
        'failed': len(expected) - sum(row.get('ok') is True for row in rows)}
    if summaries != [summary]:
        receipt['infrastructureErrors'].append('Missing or inconsistent fixture summary')
    if (receipt.get('metrics') or {}).get('errors') or not (receipt.get('metrics') or {}).get('samples') or (receipt.get('cleanup') or {}).get('error') or not receipt.get('profileRemoved'):
        receipt['infrastructureErrors'].append('Unqualified process sampling or owned cleanup')
    if sha(binary) != args.sha256 or sha(fixture_path) != expected_fixture:
        receipt['infrastructureErrors'].append('Runtime or frozen fixture changed during execution')
    receipt.update(**summary, recorded=len(rows), unrun=len(expected) - len(rows),
        finishedUTC=datetime.now(timezone.utc).isoformat(), durationSeconds=round(time.monotonic() - start, 6),
        stdoutSHA256=sha(log_path))
    payload = json.dumps(receipt, indent=2) + '\n'
    temporary = args.output.with_suffix(args.output.suffix + '.tmp')
    with temporary.open('w') as stream:
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(args.output)
    if args.output.read_text() != payload:
        raise RuntimeError('GUI evidence readback mismatch')
    print(json.dumps({key: value for key, value in receipt.items() if key not in ('rows', 'metrics', 'cleanup')}), flush=True)
    return 0 if receipt['passed'] == len(expected) and not receipt['infrastructureErrors'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
