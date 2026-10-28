"""Restore a sparse, unmodified pinned upstream WPT fixture for task 10290244."""
import datetime, hashlib, json, os, pathlib, shutil, signal, subprocess, time

ROOT = pathlib.Path.cwd()
BASE = ROOT / 'output/wpt-10290244'
UPSTREAM = BASE / 'upstream'
REVISION = '7f8d49b5a82606efc771f43d10aad5bce5ccbfeb'
SPARSE = ['resources', 'common', 'tools', 'docs', 'interfaces', 'url', 'encoding', 'fetch/api/headers', 'fetch/api/resources', 'fetch/resources', 'html/webappapis/microtask-queuing', 'html/resources', 'dom/nodes', 'dom/events', 'dom/resources', 'webstorage']
BASE.mkdir(parents=True, exist_ok=True)
receipt = {'schema': 1, 'task': '10290244', 'startedUTC': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'revision': REVISION, 'sparsePaths': SPARSE, 'scope': 'Pinned sparse upstream fixture; not a full WPT checkout or result', 'stages': []}

def save():
    target = BASE / 'bootstrap-receipt.json'
    target.with_suffix('.tmp').write_text(json.dumps(receipt, indent=2) + '\n')
    target.with_suffix('.tmp').replace(target)

def run(name, args, cwd=ROOT, timeout=180):
    started = time.monotonic()
    entry = {'name': name, 'argv': args, 'startedUTC': datetime.datetime.now(datetime.timezone.utc).isoformat()}
    receipt['stages'].append(entry)
    save()
    with (BASE / (name + '.log')).open('w') as log:
        proc = subprocess.Popen(args, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGTERM)
            try: proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                proc.wait(timeout=3)
            entry.update(timedOut=True, returncode=proc.returncode, seconds=round(time.monotonic()-started,3))
            save()
            raise RuntimeError(name + ' exceeded owned process timeout')
    entry.update(returncode=code, seconds=round(time.monotonic()-started,3), finishedUTC=datetime.datetime.now(datetime.timezone.utc).isoformat())
    save()
    if code:
        raise RuntimeError(name + ' failed; inspect ' + str((BASE/(name+'.log')).relative_to(ROOT)))

try:
    if not (UPSTREAM / '.git').exists():
        UPSTREAM.mkdir(parents=True, exist_ok=True)
        run('git-init', ['git', 'init'], UPSTREAM, 20)
        run('git-remote', ['git', 'remote', 'add', 'origin', 'https://github.com/web-platform-tests/wpt.git'], UPSTREAM, 20)
    current = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=UPSTREAM, text=True, capture_output=True).stdout.strip()
    if current != REVISION:
        run('sparse-init', ['git', 'sparse-checkout', 'init', '--cone'], UPSTREAM, 20)
        run('sparse-paths', ['git', 'sparse-checkout', 'set', *SPARSE], UPSTREAM, 20)
        run('git-fetch', ['git', 'fetch', '--depth=1', '--filter=blob:none', 'origin', REVISION], UPSTREAM, 180)
        run('git-checkout', ['git', 'checkout', '--detach', 'FETCH_HEAD'], UPSTREAM, 180)
    else:
        run('sparse-refresh', ['git', 'sparse-checkout', 'set', *SPARSE], UPSTREAM, 60)
    current = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=UPSTREAM, text=True, capture_output=True, check=True).stdout.strip()
    if current != REVISION: raise RuntimeError('Pinned upstream revision mismatch')
    run('tracked-clean', ['git', 'diff', '--exit-code', 'HEAD', '--'], UPSTREAM, 30)
    receipt['pythonCandidates'] = {name: shutil.which(name) for name in ['python3.12', 'python3.11', 'python3.10', 'python3']}
    receipt['rootFiles'] = sorted(p.name for p in UPSTREAM.iterdir() if p.is_file())
    receipt['requirements'] = sorted(str(p.relative_to(UPSTREAM)) for p in (UPSTREAM/'tools').rglob('*requirements*.txt'))
    receipt['checkoutComplete'] = True
except Exception as exc:
    receipt['error'] = type(exc).__name__ + ': ' + str(exc)
    receipt['checkoutComplete'] = False
finally:
    receipt['finishedUTC'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    save()
print(json.dumps(receipt, indent=2))
raise SystemExit(0 if receipt.get('checkoutComplete') else 1)
