#!/usr/bin/env python3
"""Privately stage accepted HEAD sources and a portable Git bundle for review."""
import argparse, datetime, hashlib, io, json, os, pathlib, re, subprocess, tarfile, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNTIME = pathlib.Path(__file__).resolve().parent / 'runtime'
SENSITIVE_PATH = re.compile(r'(^|/)(?:\.env(?:\.[^/]*)?|credentials?(?:\.[^/]*)?|cookies?(?:\.(?:sqlite[^/]*|json|txt|db))?|\.netrc|\.aws|\.ssh|id_(?:rsa|dsa|ecdsa|ed25519)|viewer-token|browser-profile|profiles?|sessions?)(?:/|$)', re.I)
SECRET = re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----|\bAKIA[A-Z0-9]{16}\b|\bgh[pousr]_[A-Za-z0-9]{30,}\b')

def git(*args, **kw):
    return subprocess.run(['git', '-C', str(ROOT), *args], check=True, **kw)

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=pathlib.Path, default=RUNTIME / 'private-prepared' / 'src.tar.gz')
    args = ap.parse_args()
    private_root = (RUNTIME / 'private-prepared').resolve()
    args.output = args.output.resolve()
    if not args.output.is_relative_to(private_root):
        ap.error('Source snapshots must remain under runtime/private-prepared while publication is blocked')
    head = git('rev-parse', 'HEAD', capture_output=True, text=True).stdout.strip()
    objects = git('rev-list', '--objects', head, capture_output=True, text=True).stdout.splitlines()
    names = [line.split(' ', 1)[1] for line in objects if ' ' in line]
    denied = [name for name in names if SENSITIVE_PATH.search(name) or name.startswith('agent/progress/runtime/')]
    if denied:
        raise SystemExit(f'Snapshot blocked: {len(denied)} sensitive history paths require review; names are not printed.')
    hashes = [line.split(' ', 1)[0] for line in objects]
    p = subprocess.Popen(['git', '-C', str(ROOT), 'cat-file', '--batch'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    checked = 0
    try:
        for oid in hashes:
            p.stdin.write((oid + '\n').encode()); p.stdin.flush()
            header = p.stdout.readline().decode().split()
            if len(header) != 3: raise RuntimeError('Git history audit failed')
            size = int(header[2]); payload = p.stdout.read(size); p.stdout.read(1)
            if len(payload) != size: raise RuntimeError('Git object read was incomplete')
            if SECRET.search(payload): raise RuntimeError('Snapshot blocked: possible private credential in history; contents are not printed.')
            checked += 1
        p.stdin.close(); p.wait()
        if p.returncode: raise RuntimeError('Git history audit process failed')
    finally:
        if p.poll() is None: p.kill(); p.wait()
    args.output.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    with tempfile.TemporaryDirectory(prefix='lumen-source-', dir=args.output.parent) as folder:
        temp = pathlib.Path(folder); source = temp / 'sources.tar'; bundle = temp / 'lumen.git.bundle'; dest = temp / 'src.tar.gz'
        git('archive', '--format=tar', '--prefix=lumen/', '--output=' + str(source), head)
        git('bundle', 'create', str(bundle), 'HEAD', stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if git('rev-parse', 'HEAD', capture_output=True, text=True).stdout.strip() != head: raise RuntimeError('HEAD changed during snapshot; retry after the commit completes')
        git('bundle', 'verify', str(bundle), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        manifest = {'generated_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'commit': head, 'source': 'Accepted Git HEAD only; uncommitted working changes excluded', 'git_history': 'lumen.git.bundle; clone with git clone lumen.git.bundle lumen-history', 'audited_history_objects': checked, 'excludes': ['.git/config', '.git/hooks', 'untracked files', 'runtime credentials', 'browser profiles', 'private messages'], 'archive_creator': 'GPT-6 Astra Pro / built-in progress_and_deployment / RubyWebTerm', 'publication': 'Private review staging only; publication disabled pending destination-specific approval'}
        with tarfile.open(dest, 'w:gz', compresslevel=4) as out, tarfile.open(source) as src:
            for member in src:
                out.addfile(member, src.extractfile(member) if member.isfile() else None)
            out.add(bundle, arcname='lumen.git.bundle', recursive=False)
            raw = (json.dumps(manifest, indent=2) + '\n').encode(); info = tarfile.TarInfo('snapshot-manifest.json'); info.size = len(raw); info.mode = 0o644
            out.addfile(info, io.BytesIO(raw))
        os.replace(dest, args.output)
    checksum = hashlib.sha256(args.output.read_bytes()).hexdigest()
    result = {**manifest, 'path': str(args.output.relative_to(ROOT)) if args.output.is_relative_to(ROOT) else str(args.output), 'bytes': args.output.stat().st_size, 'sha256': checksum}
    args.output.with_suffix('.manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, sort_keys=True))

if __name__ == '__main__': main()
