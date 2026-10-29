"""Publish a reviewed committed-source snapshot and portable Git history only."""
import datetime, hashlib, io, json, os, pathlib, re, shutil, subprocess, tarfile, tempfile
from source_credential_guard import inspect_blob

ROOT=pathlib.Path.cwd()
TASK=os.environ.get('LUME_TASK_ID','10290244')
if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}',TASK):raise RuntimeError('Invalid task identifier')
PUBLIC=ROOT/('output/session-'+TASK+'/public')
PUBLIC.mkdir(parents=True,exist_ok=True)
def git(*args):
    return subprocess.check_output(['git',*args],cwd=ROOT)

head=git('rev-parse','HEAD').decode().strip()
branch=git('branch','--show-current').decode().strip()
if branch!=os.environ.get('LUME_TASK_BRANCH','gpt6-astra-pro_release-integrator_lume-apple-wpt-10290244'):
    raise RuntimeError('Unexpected source branch')
tracked=git('ls-tree','-rz','--full-tree',head).split(b'\0')
entries=[]
for row in tracked:
    if not row:continue
    metadata,name=row.split(b'\t',1)
    mode,kind,oid=metadata.split()
    if kind!=b'blob':continue
    filename=name.decode()
    parts=pathlib.PurePosixPath(filename).parts
    if any(part in ['.git','.aws','.ssh','node_modules','__pycache__'] for part in parts) or filename.startswith(('output/', 'out/')) or '.DS_Store' in parts:
        raise RuntimeError('Unexpected non-source tracked path; review before publishing: '+filename)
    entries.append((filename,oid.decode(),mode.decode()))

# Read every reachable Git blob, not just HEAD, before publishing the bundle.
# Only a path is reported on failure; secret contents are never printed.
objects=git('rev-list','--objects',head).decode().splitlines()
names={line.split(' ',1)[0]:line.split(' ',1)[1] if ' ' in line else '(metadata)' for line in objects}
for filename in names.values():
    parts=pathlib.PurePosixPath(filename).parts
    if any(part in ['.git','.aws','.ssh','node_modules','__pycache__','message-access-key','verification-client.json'] for part in parts) or filename.startswith(('output/', 'out/')):
        raise RuntimeError('Private/generated path in reachable history; publication withheld: '+filename)
# Match the exact private message key in memory without displaying it.
private_values=[]
for key_path in ROOT.glob('output/session-*/progress-state/message-access-key'):
    value=key_path.read_bytes().strip()
    if len(value)>=24:private_values.append(value)
process=subprocess.Popen(['git','cat-file','--batch'],cwd=ROOT,stdin=subprocess.PIPE,stdout=subprocess.PIPE)
scanned=0;scanned_bytes=0;reviewed_fixture_matches=[]
try:
    for oid in names:
        process.stdin.write((oid+'\n').encode());process.stdin.flush()
        header=process.stdout.readline().decode().split()
        if len(header)!=3:raise RuntimeError('Git object inspection failed')
        size=int(header[2])
        if size>32*1024*1024:raise RuntimeError('Large Git object requires review: '+names[oid])
        data=process.stdout.read(size)
        if len(data)!=size or process.stdout.read(1)!=b'\n':raise RuntimeError('Truncated Git object inspection')
        if header[1]=='blob':
            scanned+=1;scanned_bytes+=size
            reviewed_fixture_matches.extend(inspect_blob(data,oid,names[oid],private_values))
finally:
    process.stdin.close();process.stdout.close();process.wait(timeout=10)

filename=f'gpt6-astra-pro_release-integrator_builtin-subagents_devinmac3d_lume-{TASK}_{head[:12]}_source.tar.gz'
archive=PUBLIC/filename
if archive.exists():
    metadata=json.loads((PUBLIC/(filename+'.manifest.json')).read_text())
    if metadata['sourceCommit']!=head or metadata['bytes']!=archive.stat().st_size or metadata['sha256']!=hashlib.sha256(archive.read_bytes()).hexdigest():
        raise RuntimeError('Existing immutable source package differs from its manifest')
    temporary_alias=PUBLIC/'src.tar.gz.tmp'
    shutil.copyfile(archive,temporary_alias);temporary_alias.replace(PUBLIC/'src.tar.gz')
    temporary_manifest=PUBLIC/'manifest.json.tmp'
    temporary_manifest.write_text(json.dumps(metadata,indent=2)+'\n');temporary_manifest.replace(PUBLIC/'manifest.json')
    print(json.dumps({**metadata,'existing':True,'publicAliasRefreshed':True},indent=2))
    raise SystemExit(0)

with tempfile.TemporaryDirectory(prefix='source-package-',dir=PUBLIC.parent) as directory:
    work=pathlib.Path(directory)
    bundle=work/'lumen.git.bundle'
    subprocess.run(['git','bundle','create',str(bundle),branch,'HEAD'],cwd=ROOT,check=True,capture_output=True)
    subprocess.run(['git','bundle','verify',str(bundle)],cwd=ROOT,check=True,capture_output=True)
    heads=subprocess.check_output(['git','bundle','list-heads',str(bundle)],cwd=ROOT).decode().splitlines()
    if not heads or any(line.split()[0]!=head for line in heads):
        raise RuntimeError('Source HEAD changed during portable bundle creation')
    # git archive reads the selected commit, never the mutable working tree.
    source_tar=work/'source.tar'
    with source_tar.open('wb') as target:
        subprocess.run(['git','archive','--format=tar','--prefix=source/',head],cwd=ROOT,stdout=target,check=True)
    source_manifest=[]
    with tarfile.open(source_tar,'r') as inputs:
        for member in inputs.getmembers():
            if member.isfile():
                content=inputs.extractfile(member).read()
                source_manifest.append({'path':member.name,'bytes':len(content),'sha256':hashlib.sha256(content).hexdigest()})
    manifest={'schema':1,'createdUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),'sourceCommit':head,'branch':branch,'scope':'Committed source and portable Git bundle; no mutable working tree, runtime output, configuration or credentials','sourceFiles':source_manifest,'gitBundleSHA256':hashlib.sha256(bundle.read_bytes()).hexdigest(),'scannedReachableBlobs':scanned,'scannedBytes':scanned_bytes,'reviewedCredentialFixtureMatches':reviewed_fixture_matches,'tooling':'GPT-6 Astra Pro / built-in subagents / devinmac3d / Git / Python'}
    restore='Lume task '+TASK+' source snapshot\n\nThe source/ directory is the exact committed source tree.\nTo restore portable Git history: git clone lumen.git.bundle lumen\nThen: git -C lumen checkout '+head+'\n\nRuntime data, user profiles, tunnel credentials and machine-specific Git configuration are excluded.\n'
    temporary=archive.with_suffix('.tmp')
    with tarfile.open(temporary,'w:gz') as output:
        with tarfile.open(source_tar,'r') as inputs:
            for member in inputs.getmembers():
                output.addfile(member,inputs.extractfile(member) if member.isfile() else None)
        output.add(bundle,arcname='lumen.git.bundle',recursive=False)
        for name,data in [('SOURCE-MANIFEST.json',(json.dumps(manifest,indent=2)+'\n').encode()),('RESTORE.txt',restore.encode())]:
            info=tarfile.TarInfo(name);info.size=len(data);info.mode=0o644;output.addfile(info,io.BytesIO(data))
    temporary.replace(archive)
    metadata={'archive':filename,'sourceCommit':head,'branch':branch,'bytes':archive.stat().st_size,'sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'sourcePaths':len(source_manifest),'gitBundleSHA256':manifest['gitBundleSHA256'],'scannedReachableBlobs':scanned,'reviewedCredentialFixtureMatches':reviewed_fixture_matches,'createdUTC':manifest['createdUTC'],'scope':manifest['scope']}
    (PUBLIC/(filename+'.manifest.json')).write_text(json.dumps(metadata,indent=2)+'\n')
    alias=PUBLIC/'src.tar.gz'
    temporary_alias=PUBLIC/'src.tar.gz.tmp'
    shutil.copyfile(archive,temporary_alias);temporary_alias.replace(alias)
    (PUBLIC/'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n')
print(json.dumps(metadata,indent=2))
