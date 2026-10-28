"""Bounded publication of an accepted checkpoint; no source mutation or login."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

p = argparse.ArgumentParser()
p.add_argument('--expected-head', required=True)
p.add_argument('--cohort', type=Path, help='Explicit existing immutable cohort, if refreshed reports are ready')
p.add_argument('--budget-seconds', type=int, default=480, help='Total bounded publication budget; Surge reserves time for the receipt')
p.add_argument('--receipt-generator', type=Path, default=Path('tools/apple-session-gpt6as/build_delivery_receipt_10290244.py'))
args = p.parse_args()
root = Path.cwd().resolve()
base = root / 'output/session-10290244'
base.mkdir(parents=True, exist_ok=True)
started = time.monotonic()
utc = lambda: datetime.now(timezone.utc).isoformat()
receipt = {'schema': 1, 'task': '10290244', 'startedUTC': utc(), 'stages': [],
           'sourceCommit': args.expected_head, 'accepted': False,
           'scope': 'Publication and delivery verification only; native performance and WPT results are not changed.'}

def need(value, message):
    if not value:
        raise RuntimeError(message)

def read(path):
    return json.loads(Path(path).read_text())

def sha(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()

def git(*arguments):
    return subprocess.run(['git', *arguments], check=True, capture_output=True,
                          text=True, timeout=30).stdout.strip()

def persist():
    target = base / 'publication-batch.json'
    temporary = target.with_suffix('.tmp')
    temporary.write_text(json.dumps(receipt, indent=2) + '\n')
    temporary.replace(target)

def stage(name, command, timeout, required=True):
    remaining = args.budget_seconds - (time.monotonic() - started)
    if name == 'surge-publication':
        remaining -= 75
    timeout = min(timeout, max(0, int(remaining)))
    row = {'name': name, 'startedUTC': utc(), 'timeoutSeconds': timeout,
           'required': required, 'passed': False}
    stage_started = time.monotonic()
    log = base / ('publication-' + name + '.log')
    row['log'] = str(log.relative_to(root))
    process = None
    try:
        need(timeout > 0, 'The total publication budget is exhausted')
        fd = os.open(log, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, 'w') as output:
            process = subprocess.Popen(command, cwd=root, stdin=subprocess.DEVNULL,
                                       stdout=output, stderr=subprocess.STDOUT,
                                       start_new_session=True)
            try:
                row['exitCode'] = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                row['timedOut'] = True
                # This process group was created by this exact stage. No
                # unrelated process, app, tunnel or server is selected.
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    pass
                # A helper may exit before its own child. Bound the entire
                # explicitly owned stage group, not only its root process.
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                if process.poll() is None:
                    process.wait(timeout=3)
                row['exitCode'] = process.returncode
            row['passed'] = row.get('exitCode') == 0 and not row.get('timedOut')
    except Exception as error:
        row['error'] = type(error).__name__ + ': ' + str(error)
    finally:
        row['finishedUTC'] = utc()
        row['wallSeconds'] = round(time.monotonic() - stage_started, 3)
        if log.is_file():
            row['logSHA256'] = sha(log)
        receipt['stages'].append(row)
        persist()
    if required and not row['passed']:
        raise RuntimeError(name + ' did not pass; private log retained at ' + row['log'])
    return row['passed']

try:
    need(60 <= args.budget_seconds <= 900, 'Publication budget must be between60 and900 seconds')
    branch = git('branch', '--show-current')
    need(branch == 'gpt6-astra-pro_release-integrator_lume-apple-wpt-10290244', 'Wrong task branch')
    need(git('rev-parse', 'HEAD') == args.expected_head, 'Source HEAD changed after acceptance')
    need(not git('diff', '--cached', '--name-only'), 'A different owner has staged changes')
    score = read(root / 'agent/scorecard-10290244.json')
    gates = {row['id']: row for row in score['gates']}
    native = score['current_baseline_binary_sha256']
    comparison = read(root / gates['wpt_original']['evidence'][0])
    need(comparison['candidate']['run_info']['browser_version'] == 'sha256:' + native,
         'Accepted native WPT artifact differs')
    need(comparison.get('comparable') and not comparison.get('blockers'), 'Accepted WPT receipt incomplete')
    summaries = [name for name in gates['repro_source']['evidence'] if name.endswith('/summary.json')]
    need(len(summaries) == 1, 'Accepted source summary is ambiguous')
    summary = read(root / summaries[0])
    artifact = summary['artifacts']['lumen']
    need(summary['passed'] and artifact['sha256'] == native and sha(root / artifact['snapshot']) == native,
         'Accepted immutable native artifact differs')
    compiled = read(root / Path(summaries[0]).with_name('compiled-source-sha256.json'))
    for name, digest in compiled.items():
        need(sha(root / name) == digest, 'Unaccepted source changed after native build: ' + name)
    receipt.update(branch=branch, nativeSHA256=native, acceptanceScore=score['score'],
                   acceptedIteration=score['history'][-1]['iteration'])
    scripts = root / 'tools/apple-session-gpt6as'
    if args.cohort:
        cohort = (root / args.cohort).resolve()
        need(cohort.is_relative_to(root / 'output/wpt-10290244'), 'Cohort must be the task-owned frozen evidence')
        plan = read(cohort / 'cohort.json')
        report = read(cohort / 'priority-comparison.json')
        need(plan['binaries']['lumen']['sha256'] == native, 'Explicit final cohort is not the accepted native artifact')
        need(report['cohort_sha256'] == sha(cohort / 'cohort.json'), 'Final cohort report identity differs')
        receipt['cohort'] = str(cohort.relative_to(root))
    else:
        cohort = None
        receipt['cohort'] = 'Existing public expanded cohort retained and explicitly labeled historical if its binary differs.'

    # A remote push failure does not prevent publishing the independent
    # committed-source bundle. The final delivery guard still requires an
    # exact origin-branch match before it declares durable completion.
    stage('git-push', ['git', 'push', 'origin', 'HEAD:refs/heads/' + branch], 60, required=False)
    stage('source-package', [sys.executable, str(scripts / 'package_source_10290244.py')], 180)
    need(git('rev-parse', 'HEAD') == args.expected_head, 'Concurrent source commit appeared during packaging')
    command = [sys.executable, str(scripts / 'update_priority_progress_10290244.py')]
    if cohort:
        command += ['--cohort', str(cohort)]
    stage('progress-refresh', command, 20)
    status = read(base / 'progress-state/status.json')
    manifest = read(base / 'public/manifest.json')
    need(status['wpt']['binarySha256'] == native and status['sourceCommit'] == args.expected_head,
         'Public status is stale after refresh')
    need(manifest['sourceCommit'] == args.expected_head and sha(base / 'public/src.tar.gz') == manifest['sha256'],
         'Current committed-source archive identity differs')
    receipt['sourceArchive'] = {key: manifest[key] for key in ('sourceCommit', 'bytes', 'sha256')}
    stage('public-probe', ['node', str(scripts / 'verify_progress_public_10290244.mjs')], 120)
    stage('message-reply-check', ['node', str(scripts / 'verify_progress_public_10290244.mjs'), 'reply-check'], 30)
    public = read(base / 'progress-public-probe.json')
    need(public['passed'] and public['wpt']['binarySha256'] == native and public['publicSourceArchive']['sourceCommit'] == args.expected_head,
         'Public artifact/source probe is stale')
    receipt['publicURL'] = public['publicURL']
    receipt['tlsAndLiveWebSocketVerified'] = bool(public['websocket']['tlsCertificateValidated'] and public['websocket']['handshakeAcceptVerified'])
    receipt['fullPublicSourceHashVerified'] = public['publicSourceArchive']['fullByteHashVerified']
    receipt['webSocketSampleIntervalSeconds'] = public['websocket']['observedSampleIntervalSeconds']
    stage('surge-publication', [sys.executable, str(scripts / 'publish_progress_surge_10290244.py')], 300, required=False)
    surge_file = base / 'surge-publication.json'
    if surge_file.is_file():
        surge = read(surge_file)
        receipt['surge'] = {key: surge[key] for key in ('deployed', 'publicURL', 'sourceCommit', 'error', 'finishedUTC') if key in surge}
    else:
        receipt['surge'] = {'deployed': False, 'error': 'No completed Surge receipt; inspect the bounded private stage log.'}
    need(git('rev-parse', 'HEAD') == args.expected_head, 'Concurrent source commit appeared before delivery receipt')
    need(args.receipt_generator.is_file(), 'Install the standalone delivery-receipt generator before this final batch')
    stage('delivery-receipt', [sys.executable, str(args.receipt_generator)], 60)
    delivery_path = base / 'GPT-6-Astra-Pro_release-integrator_builtin-subagents_devinmac3d_Lume-10290244_delivery-receipt.json'
    need(delivery_path.is_file(), 'Standalone delivery receipt was not produced')
    receipt['standaloneDeliveryReceipt'] = {'path': str(delivery_path.relative_to(root)), 'sha256': sha(delivery_path), 'bytes': delivery_path.stat().st_size}
    receipt['accepted'] = True
except Exception as error:
    receipt['error'] = type(error).__name__ + ': ' + str(error)
finally:
    receipt['finishedUTC'] = utc()
    receipt['wallSeconds'] = round(time.monotonic() - started, 3)
    persist()
print(json.dumps(receipt, indent=2))
raise SystemExit(0 if receipt['accepted'] else 1)
