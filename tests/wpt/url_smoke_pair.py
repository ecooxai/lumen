#!/usr/bin/env python3
"""Six official URL WPT variants, Lumen then Chrome, on the authorized Mac."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import stat
import subprocess
import sys
import time

import full_runner as runner


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path.cwd())
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--host-map', required=True)
    parser.add_argument('--chromium-host-rules', required=True)
    parser.add_argument('--fixture-manifest', type=Path)
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('Only execute on the authorized Mac through devinmac3d')
    root = args.repo.resolve(strict=True)
    output = (root / args.output).resolve()
    if shutil.disk_usage(root).free < 3 * 1024 ** 3:
        parser.error('Fewer than 3 GiB free; no browser launched')
    source = root / 'build/lumen'
    identity = runner.binary_identity(source)
    sha = runner.file_digest(source)
    snapshot = source.with_name('lumen-wpt-' + sha[:16])
    if snapshot.exists():
        if runner.file_digest(snapshot) != sha or snapshot.stat().st_mode & 0o222:
            raise ValueError('Conflicting or writable existing native snapshot')
    else:
        temporary = snapshot.with_name(snapshot.name + '.copy-' + str(os.getpid()))
        try:
            shutil.copy2(source, temporary)
            if runner.binary_identity(source) != identity or runner.file_digest(temporary) != sha:
                raise ValueError('Original executable changed during snapshot copy')
            temporary.chmod(stat.S_IMODE(temporary.stat().st_mode) & ~0o222)
            os.link(temporary, snapshot)
        finally:
            temporary.unlink(missing_ok=True)
    output.mkdir(parents=True, exist_ok=False)
    environment = os.environ.copy()
    for key in ('LUMEN_PRE_FILE', 'LUMEN_SHOT', 'LUMEN_SHOT_MS', 'LUMEN_NO_JS'):
        environment.pop(key, None)
    environment.update(LUMEN_HOST_MAP=args.host_map, LUMEN_HOST_MAP_MODE='exact',
                       SSL_CERT_FILE=str(root / 'output/wpt/upstream/tools/certs/cacert.pem'))
    script = Path(runner.__file__).resolve()
    state = {'controller_pid': os.getpid(), 'native_snapshot': str(snapshot), 'native_sha256': sha,
             'started_unix_ms': round(time.time() * 1000), 'status': 'prepared'}
    child, stop = None, False

    def save(**changes):
        state.update(changes)
        runner.write_json(output / 'controller.json', state)

    def interrupted(signum, frame):
        nonlocal stop
        stop = True
        state['requested_signal'] = signum
        if child is not None and child.poll() is None:
            child.send_signal(signal.SIGTERM)

    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, interrupted)
    save()
    try:
        for engine, binary in [('lumen', snapshot), ('chromium', Path('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'))]:
            if stop:
                raise RuntimeError('Requested stop; no subsequent browser launched')
            command = [sys.executable, str(script), 'run', '--inventory', str(root / 'output/wpt/inventory.json'),
                       '--engine', engine, '--binary', str(binary), '--output', str(output / engine),
                       '--types', 'testharness', '--prefix', '/url/', '--limit', '6',
                       '--origin', 'http://web-platform.test:8000', '--secure-origin', 'https://web-platform.test:18443',
                       '--h2-origin', 'https://web-platform.test:9000', '--timeout', '15', '--long-timeout', '65',
                       '--server-config', str(root / 'output/wpt/server.config.json'), '--strict-infrastructure']
            if args.fixture_manifest:
                command.extend(['--fixture-manifest', str((root / args.fixture_manifest).resolve(strict=True))])
            if engine == 'chromium':
                command.extend(['--chromium-arg=--host-resolver-rules=' + args.chromium_host_rules,
                                '--chromium-arg=--ignore-certificate-errors-spki-list=WXjEgm6G9rSgRQQVGbD5q+zOszPmv3ZBSfaIAMv+bY8='])
            log_path = output / (engine + '.runner.log')
            with log_path.open('wb') as log:
                child = subprocess.Popen(command, cwd=root, env=environment, stdout=log, stderr=subprocess.STDOUT,
                                         start_new_session=True)
                save(status='running', engine=engine, child_pid=child.pid, log=str(log_path), command=command)
                try:
                    code = child.wait(timeout=12 * (65 + 45))
                except subprocess.TimeoutExpired:
                    child.send_signal(signal.SIGTERM)
                    code = child.wait(timeout=155)
                    raise RuntimeError('Smoke controller watchdog expired; inspect cleanup evidence')
                finally:
                    if child.poll() is not None:
                        save(child_pid=None, exit_code=child.returncode)
            if code != 0 or stop:
                raise RuntimeError(engine + ' runner exited ' + str(code))
            meta = json.loads((output / engine / 'run.json').read_text())
            stops = {key: value for key, value in meta.items() if key.startswith('stopped_for_') and value}
            rows = list(runner.iter_jsonl(output / engine / 'results.jsonl'))  # fixed six-file smoke only
            if not meta.get('finished_schedule') or stops or len(rows) != 6:
                raise RuntimeError(engine + ' did not complete a safe six-file smoke: ' + json.dumps(stops))
            for row in rows:
                if (row.get('reporter_state') or {}).get('status') != 'ATTACHED' or not row.get('subtests') or 'subtest_count' not in row:
                    raise RuntimeError(engine + ' smoke reporting incomplete at ' + row['test'])
        comparison = output / 'comparison.json'
        with (output / 'compare.log').open('w') as log:
            code = subprocess.run([sys.executable, str(script), 'compare', '--lumen', str(output / 'lumen'),
                                   '--chromium', str(output / 'chromium'), '--output', str(comparison)],
                                  cwd=root, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=60).returncode
        if code:
            raise RuntimeError('Matched comparison failed; see compare.log')
        save(status='complete', comparison=str(comparison), completed_unix_ms=round(time.time() * 1000))
        report = json.loads(comparison.read_text())
        print(json.dumps({'comparison': str(comparison), 'lumen': report['lumen'], 'chromium': report['chromium'],
                          'parity': report['parity']['counts'], 'chromium_pass_retention': report['parity']['chromium_pass_retention']}))
    except BaseException as exc:
        if child is not None and child.poll() is None:
            child.send_signal(signal.SIGTERM)
            try:
                child.wait(timeout=155)
            except subprocess.TimeoutExpired:
                state['child_may_still_be_running'] = True
        if not state.get('child_may_still_be_running') and all((output / engine / 'run.json').is_file() for engine in ('lumen', 'chromium')):
            # Cleanup incidents block new browsers, not read-only comparison of
            # already recorded assertions. The report keeps both run stop flags.
            try:
                comparison = output / 'diagnostic-comparison.json'
                with (output / 'diagnostic-compare.log').open('w') as log:
                    code = subprocess.run([sys.executable, str(script), 'compare', '--lumen', str(output / 'lumen'),
                                           '--chromium', str(output / 'chromium'), '--output', str(comparison)],
                                          cwd=root, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=60).returncode
                if code == 0:
                    state.update(diagnostic_comparison=str(comparison), diagnostic_only=True)
                else:
                    state['diagnostic_compare_error'] = 'See diagnostic-compare.log'
            except Exception as diagnostic_error:
                state['diagnostic_compare_error'] = str(diagnostic_error)
        save(status='paused' if stop else 'blocked', error=type(exc).__name__ + ': ' + str(exc))
        raise


if __name__ == '__main__':
    main()
