#!/usr/bin/env python3
"""Audit an immutable source snapshot; never include Git history or runtime data."""
import datetime,hashlib,json,pathlib,re,subprocess,tarfile
root=pathlib.Path(__file__).resolve().parents[2];runtime=root/'agent/progress/runtime';runtime.mkdir(parents=True,exist_ok=True)
files=[x for x in subprocess.check_output(['git','ls-files','-z'],cwd=root).decode().split('\0') if x]
files += [str(p.relative_to(root)) for p in (root/'agent/progress').rglob('*') if p.is_file() and not any(k in p.relative_to(root/'agent/progress').parts for k in ['node_modules','runtime','__pycache__']) and p.name not in ['src.tar.gz','config.js']]
checks=[re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),re.compile(rb'gh[pousr]_[A-Za-z0-9_]{30,}'),re.compile(rb'github_pat_[A-Za-z0-9_]{40,}'),re.compile(rb'AKIA[A-Z0-9]{16}'),re.compile(rb'sk-(?:proj-)?[A-Za-z0-9_-]{40,}')]
selected=[];flagged=[]
for f in sorted(set(files)):
 p=root/f
 if not p.is_file() or p.is_symlink():continue
 if re.search(r'(^|/)(\.env($|\.)|id_rsa|id_ed25519|credentials(?:\.|$)|.*\.(?:pem|p12|jks|keystore))$',f,re.I) or any(c.search(p.read_bytes()) for c in checks):flagged.append(f)
 else:selected.append(f)
if flagged:raise SystemExit('Archive blocked for review; candidate paths: '+repr(flagged))
now=datetime.datetime.now(datetime.timezone.utc);stamp=now.strftime('%Y%m%dT%H%M%SZ');archive=runtime/('src-'+stamp+'.tar.gz')
head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root).decode().strip();dirty=bool(subprocess.check_output(['git','status','--porcelain'],cwd=root))
with tarfile.open(archive,'w:gz',compresslevel=6) as t:
 for f in selected:t.add(root/f,arcname='lumen-source/'+f,recursive=False)
report={'created_utc':now.isoformat(),'git_head':head,'working_tree_modified':dirty,'accepted_release':False,'files':len(selected),'bytes':archive.stat().st_size,'sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'immutable_archive':archive.name,'excluded':['Git history','runtime messages and tokens','node_modules','browser profiles','untracked files outside agent/progress'],'checks':['tracked-path boundary','no symlinks','private-key and common credential-pattern scan'],'scope':'Source snapshot only; working changes may be unvalidated. Scan cannot prove absence of every embedded secret.'}
(runtime/'archive-audit.json').write_text(json.dumps(report,indent=2)+'\n');(runtime/'src.tar.gz.tmp').write_bytes(archive.read_bytes());(runtime/'src.tar.gz.tmp').replace(runtime/'src.tar.gz');print(json.dumps(report))
