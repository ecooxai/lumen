"""Refresh the existing authenticated Surge domain without installing tools."""
from datetime import datetime, timezone
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import time
import urllib.request


def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--availability-receipt',required=True,type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    root=Path.cwd().resolve()
    if str(root)!='/Users/devin/repos/lumen':parser.error('Use the observed canonical Mac repository')
    args.availability_receipt.resolve().relative_to(root/'out')
    ready=json.loads(args.availability_receipt.read_text())
    if not ready.get('canReuseWithoutInstall') or not ready.get('existingAuthenticationVerified'):
        raise RuntimeError('The bounded existing-account check did not qualify publication')
    origin=ready['domain']
    if origin!='https://lume-102900614-gpt6-astra-pro.surge.sh':
        raise RuntimeError('Only the observed existing domain is authorized for this carrier')
    cli=Path(ready['existingCLI']).resolve();cli.relative_to(root)
    package=next((path/'package.json' for path in cli.parents if (path/'package.json').is_file()
                  and path.is_relative_to(root) and path.name=='surge'),None)
    if package is None:raise RuntimeError('Existing Surge package metadata is missing')
    tool=json.loads(package.read_text())
    if tool.get('name')!='surge' or not re.fullmatch(r'\d+\.\d+\.\d+(?:[-+][\w.-]+)?',tool.get('version','')):
        raise RuntimeError('Existing Surge package identity differs')
    tool_sha=sha(cli)
    base=root/'output/session-20261029-cpu30';state=base/'progress-state';public=base/'public'
    server=json.loads((state/'server-receipt.json').read_text());live=server['publicURL']
    if origin not in server.get('allowedOrigins',[]):raise RuntimeError('The existing live endpoint does not allow this Surge origin')
    status=json.loads((state/'status.json').read_text())
    if (status.get('activeTaskId')!='10300110' or not status.get('nativeMacDelivery',{}).get('published')
            or status.get('developmentCandidate',{}).get('hold',{}).get('active')):
        raise RuntimeError('Publish the exact accepted development app before this delivery refresh')
    head=subprocess.check_output(['git','rev-parse','HEAD'],text=True,timeout=5).strip()
    manifest=json.loads((public/'manifest.json').read_text());archive=public/'src.tar.gz'
    if manifest.get('sourceCommit')!=head or sha(archive)!=manifest['sha256']:
        raise RuntimeError('The committed source archive must match the current integration checkpoint')
    source=root/'tools/lume-progress-10290244'
    for name in ('public/index.html','public/app.js','public/style.css','build.mjs'):
        path=source/name
        committed=subprocess.check_output(['git','show','HEAD:'+str(path.relative_to(root))],timeout=5)
        if hashlib.sha256(committed).hexdigest()!=sha(path):raise RuntimeError('The static view has unreviewed edits')
    out=args.output.resolve();out.relative_to(root/'out');out.mkdir(parents=True,exist_ok=False)
    deploy=out/'public';deploy.mkdir()
    started=time.monotonic()
    report={'startedUTC':datetime.now(timezone.utc).isoformat(),'deployed':False,'domain':origin,
        'sourceCommit':head,'sourceArchiveSHA256':manifest['sha256'],'sourceArchiveBytes':manifest['bytes'],
        'liveEndpoint':live,'existingCLIVersion':tool['version'],'existingCLI_SHA256':tool_sha,
        'availabilityReceiptSHA256':sha(args.availability_receipt),
        'newInstallationPerformed':False,'credentialValuesOmitted':True,'stages':[]}
    env=dict(os.environ,LUME_TASK_ID='20261029-cpu30',LUME_MESSAGE_SESSION_ID='102900614')

    def stage(name,command,limit):
        start=time.monotonic();log=out/(name+'-private.log')
        with log.open('x') as stream:
            log.chmod(0o600)
            result=subprocess.run(command,stdin=subprocess.DEVNULL,stdout=stream,stderr=subprocess.STDOUT,
                                  env=env,cwd=root,timeout=limit)
        report['stages'].append({'name':name,'exitCode':result.returncode,'durationSeconds':round(time.monotonic()-start,3)})
        if result.returncode:raise RuntimeError(name+' failed; bounded private log retained')

    try:
        stage('build-view',['node',str(source/'build.mjs'),'--live-base',live,'--output',str(deploy)],15)
        shutil.copyfile(archive,deploy/'src.tar.gz');shutil.copyfile(public/'manifest.json',deploy/'source-manifest.json')
        (deploy/'CNAME').write_text(origin.removeprefix('https://')+'\n')
        names={'index.html','app.js','style.css','config.json','src.tar.gz','source-manifest.json','CNAME'}
        if {path.name for path in deploy.iterdir()}!=names:raise RuntimeError('Unexpected static publication contents')
        hashes={name:sha(deploy/name) for name in sorted(names)}
        stage('publish',[str(cli),str(deploy),origin.removeprefix('https://'),'--force','https'],120)
        nonce=str(time.time_ns())
        for name in ('index.html','app.js','style.css','config.json','source-manifest.json'):
            with urllib.request.urlopen(origin+'/'+name+'?checkpoint='+nonce,timeout=10) as response:
                payload=response.read(2097153)
            if len(payload)>2097152 or hashlib.sha256(payload).hexdigest()!=hashes[name]:
                raise RuntimeError('Public static file differs: '+name)
        public_hash=hashlib.sha256();size=0;download_start=time.monotonic()
        with urllib.request.urlopen(origin+'/src.tar.gz?checkpoint='+nonce,timeout=10) as response:
            while block:=response.read(1024*1024):
                public_hash.update(block);size+=len(block)
                if size>manifest['bytes'] or time.monotonic()-download_start>90:
                    raise RuntimeError('Public source readback exceeded declared byte/time budget')
        if size!=manifest['bytes'] or public_hash.hexdigest()!=manifest['sha256']:
            raise RuntimeError('Public source archive bytes differ')
        if sha(cli)!=tool_sha or sha(archive)!=manifest['sha256']:
            raise RuntimeError('Existing CLI or committed source changed during publication')
        config=json.loads((deploy/'config.json').read_text())
        if config.get('messageSessionId')!='102900614' or config.get('liveBase')!=live:
            raise RuntimeError('Static private-inbox session or live endpoint changed')
        report.update(deployed=True,publishedFileHashes=hashes,publicArchiveFullByteSHA256=public_hash.hexdigest(),
            publicArchiveBytes=size,publicViewByteHashesVerified=True,
            privateMessageSessionPreserved=True,nativeDownloadOrigin=live)
    except Exception as error:
        report['error']=type(error).__name__+': '+str(error)[:400]
    finally:
        report.update(finishedUTC=datetime.now(timezone.utc).isoformat(),durationSeconds=round(time.monotonic()-started,3))
        (out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
    return 0 if report['deployed'] else 1


if __name__=='__main__':raise SystemExit(main())
