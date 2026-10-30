"""Independently validate an immutable source package without changing repository state."""
from pathlib import Path
import argparse, datetime, hashlib, json, subprocess, tarfile, tempfile, time
p=argparse.ArgumentParser();p.add_argument('--expected-commit',required=True);p.add_argument('--expected-sha256',required=True);p.add_argument('--output',required=True);a=p.parse_args()
start=time.monotonic();root=Path.cwd();base=root/'output/session-102900614';public=base/'public'
meta=json.loads((public/'manifest.json').read_text());archive=public/meta['archive'];alias=public/'src.tar.gz'
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
if meta['sourceCommit']!=a.expected_commit or meta['sha256']!=a.expected_sha256 or sha(archive)!=a.expected_sha256 or sha(alias)!=a.expected_sha256 or archive.stat().st_size!=meta['bytes']:raise RuntimeError('Archive outer identities differ')
branch='gpt6-astra-pro_builtin-agents_lume-apple-wpt-102900614'
if meta['branch']!=branch:raise RuntimeError('Archive branch differs')
with tarfile.open(archive,'r:gz') as tar:
    members=tar.getmembers();byname={row.name:row for row in members}
    if len(byname)!=len(members):raise RuntimeError('Duplicate tar member')
    for row in members:
        parts=Path(row.name).parts
        if row.name.startswith('/') or '..' in parts or row.islnk():raise RuntimeError('Unexpected tar path or hard link')
    manifest=json.load(tar.extractfile('SOURCE-MANIFEST.json'))
    if manifest['sourceCommit']!=a.expected_commit or manifest['branch']!=branch or manifest['gitBundleSHA256']!=meta['gitBundleSHA256']:raise RuntimeError('Inner archive provenance differs')
    files={row['path']:row for row in manifest['sourceFiles']}
    if len(files)!=meta['sourcePaths'] or len(files)!=len(manifest['sourceFiles']):raise RuntimeError('Source file inventory differs')
    regular={row.name for row in members if row.isfile() and row.name.startswith('source/')}
    if regular!=set(files):raise RuntimeError('Source archive inventory differs')
    for name,expected in files.items():
        data=tar.extractfile(name).read()
        if len(data)!=expected['bytes'] or hashlib.sha256(data).hexdigest()!=expected['sha256']:raise RuntimeError('Source member hash differs: '+name)
    bundle_data=tar.extractfile('lumen.git.bundle').read()
    if hashlib.sha256(bundle_data).hexdigest()!=meta['gitBundleSHA256']:raise RuntimeError('Embedded bundle hash differs')
    with tempfile.TemporaryDirectory(prefix='archive-verify-',dir=base) as temp:
        bundle=Path(temp)/'lumen.git.bundle';bundle.write_bytes(bundle_data)
        check=subprocess.run(['git','--no-pager','bundle','verify',str(bundle)],capture_output=True,text=True,timeout=30)
        if check.returncode:raise RuntimeError('Embedded portable Git bundle did not verify')
        heads=subprocess.check_output(['git','--no-pager','bundle','list-heads',str(bundle)],text=True,timeout=30).splitlines()
        refs={line.split(' ',1)[1]:line.split(' ',1)[0] for line in heads}
        if refs!={'refs/heads/'+branch:a.expected_commit,'HEAD':a.expected_commit}:raise RuntimeError('Embedded bundle refs differ from recorded source commit')
receipt={'schema':1,'taskId':'102900614','verifiedUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),'sourceCommit':a.expected_commit,'branch':branch,'archiveSHA256':a.expected_sha256,'archiveBytes':meta['bytes'],'gitBundleSHA256':meta['gitBundleSHA256'],'bundleRefs':refs,'bundleVerified':True,'sourceFileHashesVerified':len(files),'publicAliasHashVerified':True,'durationSeconds':round(time.monotonic()-start,3),'scope':'Independent byte/manifest/bundle identity verification; no source extraction or Git mutation. Public TLS download verification remains a separate check.'}
out=root/a.output
if not out.resolve().is_relative_to(base.resolve()) or out.exists():raise RuntimeError('Receipt path must be new and task scoped')
out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))
