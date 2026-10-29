#!/usr/bin/env python3
"""Publish the reviewed task progress view and committed source to Surge.

Uses a pinned local-prefix npm package, existing account credentials only,
noninteractive stdin, bounded commands, and private logs. Never prints tokens.
"""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import urllib.request

ROOT = Path.cwd()
TASK = os.environ.get('LUME_TASK_ID', '10290244')
if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', TASK):
    raise RuntimeError('Invalid task identifier')
BASE = ROOT / ('output/session-' + TASK)
STATE = BASE / 'progress-state'
DEPLOY = BASE / 'surge'
HOST = os.environ.get('LUME_SURGE_HOST', 'lume-' + TASK + '-gpt6-astra-pro.surge.sh')
if not re.fullmatch(r'[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.surge\.sh', HOST):
    raise RuntimeError('Expected a single valid Surge project hostname')
PREFIX = BASE / 'surge-cli'
PUBLIC = BASE / 'public'
receipt = {'schema': 1, 'startedUTC': datetime.now(timezone.utc).isoformat(),
           'domain': HOST, 'stages': [], 'deployed': False,
           'credentials': 'Existing Surge login or inherited SURGE_TOKEN only; no token is printed or newly created.',
           'reference': 'https://surge.sh/docs/cli/reference'}
BASE.mkdir(parents=True, exist_ok=True)


def utc():
    return datetime.now(timezone.utc).isoformat()


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def stage(name, command, timeout):
    log = BASE / ('surge-' + name + '.log')
    began = time.monotonic()
    row = {'name': name, 'startedUTC': utc(), 'log': str(log.relative_to(ROOT))}
    try:
        with log.open('w') as output:
            result = subprocess.run(command, cwd=ROOT, stdin=subprocess.DEVNULL,
                                    stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
        row['exitCode'] = result.returncode
        if result.returncode:
            raise RuntimeError(name + ' failed with exit ' + str(result.returncode) + '; private log retained')
    finally:
        row['finishedUTC'] = utc()
        row['wallSeconds'] = round(time.monotonic() - began, 3)
        receipt['stages'].append(row)
    return log


def get(url, limit=1024 * 1024):
    with urllib.request.urlopen(url, timeout=20) as response:
        data = response.read(limit + 1)
        if len(data) > limit:
            raise RuntimeError('Unexpectedly large publication probe response')
        return data


try:
    server = json.loads((STATE / 'server-receipt.json').read_text())
    live = server.get('publicURL')
    if not live or not server.get('publicProgressVerified'):
        raise RuntimeError('A verified live HTTPS progress endpoint is required first')
    health = json.loads(get(live + '/api/health'))
    if health.get('taskId') != TASK or not health.get('ok'):
        raise RuntimeError('Live endpoint identity differs')
    if 'https://' + HOST not in server.get('allowedOrigins', []):
        raise RuntimeError('Live endpoint must explicitly allow this Surge origin')
    receipt['liveEndpoint'] = live
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    manifest = json.loads((PUBLIC / 'manifest.json').read_text())
    if manifest.get('sourceCommit') != head or sha(PUBLIC / 'src.tar.gz') != manifest['sha256']:
        raise RuntimeError('Source archive must be refreshed to the current committed checkpoint')
    source = ROOT / 'tools/lume-progress-10290244'
    for name in ('public/index.html', 'public/app.js', 'public/style.css', 'build.mjs'):
        relative = str((source / name).relative_to(ROOT))
        committed = subprocess.check_output(['git', 'show', 'HEAD:' + relative])
        if hashlib.sha256(committed).hexdigest() != sha(source / name):
            raise RuntimeError('Publish only the reviewed committed progress view: ' + name)
    version_log = stage('version-resolution', ['npm', 'view', 'surge', 'version', '--json'], 30)
    version = json.loads(version_log.read_text())
    if not isinstance(version, str) or not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?', version):
        raise RuntimeError('Unexpected Surge registry version')
    receipt['surgeVersion'] = version
    stage('install', ['npm', 'install', '--prefix', str(PREFIX), '--no-audit', '--no-fund',
                      '--ignore-scripts', '--save-exact', 'surge@' + version], 180)
    receipt['packageLockSHA256'] = sha(PREFIX / 'package-lock.json')
    cli = PREFIX / 'node_modules/.bin/surge'
    auth_log = stage('existing-authentication', [str(cli), 'whoami'], 30)
    receipt['existingAuthenticationVerified'] = True
    # Empty the task-owned output view to prevent stale files from publishing.
    DEPLOY.mkdir(parents=True, exist_ok=True)
    allowed = {'index.html', 'app.js', 'style.css', 'config.json', 'src.tar.gz', 'source-manifest.json', 'CNAME'}
    unexpected = [path.name for path in DEPLOY.iterdir() if path.name not in allowed or not path.is_file()]
    if unexpected:
        raise RuntimeError('Unexpected files in static publish directory; inspect before publishing')
    stage('build-view', ['node', str(source / 'build.mjs'), '--live-base', live, '--output', str(DEPLOY)], 15)
    shutil.copyfile(PUBLIC / 'src.tar.gz', DEPLOY / 'src.tar.gz')
    shutil.copyfile(PUBLIC / 'manifest.json', DEPLOY / 'source-manifest.json')
    (DEPLOY / 'CNAME').write_text(HOST + '\n')
    receipt['sourceCommit'] = head
    receipt['archive'] = {'bytes': manifest['bytes'], 'sha256': manifest['sha256']}
    receipt['publishedFileHashes'] = {path.name: sha(path) for path in sorted(DEPLOY.iterdir())}
    stage('publish', [str(cli), str(DEPLOY), HOST, '--force', 'https'], 120)
    nonce = str(time.time_ns())
    for name in ('index.html', 'app.js', 'style.css', 'config.json', 'source-manifest.json'):
        public = get('https://' + HOST + '/' + name + '?checkpoint=' + nonce, limit=2 * 1024 * 1024)
        if hashlib.sha256(public).hexdigest() != sha(DEPLOY / name):
            raise RuntimeError('Published file differs from committed view: ' + name)
    with urllib.request.urlopen(urllib.request.Request('https://' + HOST + '/src.tar.gz', method='HEAD'), timeout=20) as response:
        if int(response.headers.get('Content-Length', '-1')) != manifest['bytes']:
            raise RuntimeError('Published source archive length differs')
    receipt['deployed'] = True
    receipt['publicURL'] = 'https://' + HOST
    receipt['publicViewHashesVerified'] = True
    receipt['publicArchiveLengthVerified'] = True
    receipt['archiveHashVerificationNote'] = 'Published archive length checked; the local committed source archive SHA-256 is recorded. Full public-byte hash is a separate probe.'
except Exception as error:
    receipt['error'] = type(error).__name__ + ': ' + str(error)
finally:
    receipt['finishedUTC'] = utc()
    target = BASE / 'surge-publication.json'
    temporary = target.with_suffix('.tmp')
    temporary.write_text(json.dumps(receipt, indent=2) + '\n')
    temporary.replace(target)
print(json.dumps(receipt, indent=2))
raise SystemExit(0 if receipt['deployed'] else 1)
