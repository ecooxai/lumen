"""Publish only the accepted native Mac ZIP/receipt on the existing task route.

Requires exact package, four TLS cases and relocated resource receipts. It
preserves the same private inbox/capability and source route, replaces only the
verified owned server process, and hashes every byte through the public route.
"""
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
import argparse
import copy
import hashlib
import json
import os
import shlex
import signal
import stat
import subprocess
import sys
import time
import urllib.parse
import urllib.request
import zipfile


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def atomic(path,value):
    pending=path.with_suffix(path.suffix+'.10300110.tmp')
    with pending.open('x') as stream:
        json.dump(value,stream,indent=2);stream.write('\n')
    pending.chmod(0o600);os.replace(pending,path)


def command(argv):
    return subprocess.check_output(argv,text=True,stderr=subprocess.DEVNULL,timeout=5).strip()


def identity(pid):
    return {'pid':pid,'start':command(['ps','-p',str(pid),'-o','lstart=']),
        'args':command(['ps','-ww','-p',str(pid),'-o','args=']),
        'cwd':command(['lsof','-a','-p',str(pid),'-d','cwd','-Fn'])}


def get_json(url):
    with urllib.request.urlopen(url,timeout=8) as response:
        return json.loads(response.read(262144))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package-summary',type=Path,required=True)
    parser.add_argument('--tls-summary',type=Path,required=True)
    parser.add_argument('--resources-summary',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    root=Path.cwd().resolve()
    if sys.platform!='darwin' or str(root)!='/Users/devin/repos/lumen':
        parser.error('Use the observed canonical Mac repository')
    sys.path.insert(0,str(root/'tools'))
    from macos_bundle_dependencies import verify_recorded_bundle
    paths={name:getattr(args,name+'_summary').resolve() for name in ('package','tls','resources')}
    for path in [*paths.values(),args.output.resolve()]:path.relative_to(root/'out')
    data={name:json.loads(path.read_text()) for name,path in paths.items()}
    package,tls,resources=(data[name] for name in ('package','tls','resources'))
    package_sha=sha(paths['package']);binary_sha=package['package']['binarySha256']
    required_tls={'release-rejects-untrusted','explicit-trust-override','bundled-local-ca','explicit-override-wins'}
    if (not package.get('passed') or not tls.get('passed') or not resources.get('accepted')
            or tls.get('packageSummarySHA256')!=package_sha or resources.get('packageSummarySHA256')!=package_sha
            or tls.get('packageBinarySHA256')!=binary_sha or resources['artifact']['sha256']!=binary_sha
            or {row['name'] for row in tls['cases']}!=required_tls or len(tls['cases'])!=4
            or not all(row.get('passed') for row in tls['cases'])):
        raise RuntimeError('Exact delivered app/package/TLS/resource acceptance is incomplete')
    build_path=(root/package['buildSummary']).resolve();build_path.relative_to(root/'out')
    if sha(build_path)!=package['buildSummarySHA256']:raise RuntimeError('Product build receipt changed')
    build=json.loads(build_path.read_text())
    if not build.get('passed'):raise RuntimeError('Product build was not accepted')
    app=(root/package['relocation']['app']).resolve();app.relative_to(root/'out')
    executable=app/'Contents/MacOS/lumen'
    if sha(executable)!=binary_sha:raise RuntimeError('Relocated app bytes changed')
    archive=(root/package['archive']['path']).resolve();archive.relative_to(root/'out')
    if archive.is_symlink() or sha(archive)!=package['archive']['sha256']:
        raise RuntimeError('Accepted archive bytes changed')
    closure=package['relocation']['deploymentClosure']
    bundle_verified=verify_recorded_bundle(closure,app,package['package']['runtimeResources'])
    if (closure.get('architecture')!='arm64' or closure.get('platform')!='macos'
            or not closure.get('nonSystemClosureFullyResolved')
            or not package['loadedImages'].get('allRequiredBundleImagesObserved')
            or package['loadedImages'].get('unexpectedExternalNonSystem')):
        raise RuntimeError('Actual native architecture/dependency closure was not established')
    v8_images=[row for row in package['package']['embeddedDependencies']['images']
               if Path(row['source']).name.startswith('libv8')]
    v8_versions=set()
    for row in v8_images:
        parts=Path(row['source']).parts
        for index,name in enumerate(parts[:-2]):
            if name=='Cellar' and parts[index+1]=='v8':v8_versions.add(parts[index+2])
    if not v8_images or len(v8_versions)!=1:
        raise RuntimeError('Inspected prebuilt V8 distribution version is not unambiguous')
    # Only the already constructed delivery directory may be exposed. The TLS
    # fixture, private keys, profiles, inbox and arbitrary project files are out.
    with zipfile.ZipFile(archive) as zipped:
        entries=zipped.infolist()
        if len(entries)>10000 or sum(row.file_size for row in entries)>2*1024**3:
            raise RuntimeError('Unexpected native delivery archive scope')
        for row in entries:
            path=PurePosixPath(row.filename)
            if (path.is_absolute() or '..' in path.parts or not path.parts
                    or path.parts[0] not in ('delivery','__MACOSX')
                    or stat.S_ISLNK(row.external_attr>>16)
                    or any(part in ('private-fixture','messages','.git') or part.endswith('-profile') for part in path.parts)
                    or path.name.lower().endswith(('.key','.p12','.mobileprovision'))):
                raise RuntimeError('Unexpected or private native archive member: '+row.filename)
        if zipped.testzip() is not None:raise RuntimeError('Native archive CRC validation failed')
    state=root/'output/session-20261029-cpu30/progress-state'
    state_receipt=state/'server-receipt.json';before_server=state_receipt.read_bytes();server=json.loads(before_server)
    status_path=state/'status.json';before_status=status_path.read_bytes();status=json.loads(before_status)
    source_binary=build['artifacts']['lumen']['sha256']
    active=status.get('developmentCandidate') or {}
    current=status.get('priorityObserved') or status.get('currentWpt') or {}
    if (status.get('taskId')!='20261029-cpu30' or status.get('activeTaskId')!='10300110'
            or source_binary not in (active.get('binarySha256'),current.get('binarySha256'))
            or active.get('hold',{}).get('active') or status.get('securityHold',{}).get('active')):
        raise RuntimeError('Live status identifies another or held candidate; refresh its measured evidence first')
    origin=server['publicURL'];parsed=urllib.parse.urlsplit(origin)
    if (parsed.scheme!='https' or parsed.username or parsed.password or parsed.path not in ('','/')
            or parsed.query or parsed.fragment or server.get('taskId')!='20261029-cpu30'
            or server.get('port')!=18785 or server.get('messageSessionId')!='102900614'):
        raise RuntimeError('Existing task transport identity differs')
    origin=origin.rstrip('/');local='http://127.0.0.1:18785'
    for base in (local,origin):
        if get_json(base+'/api/health').get('taskId')!='20261029-cpu30':raise RuntimeError('Existing route identity differs')
    prior_pid=int(server['pid']);prior_identity=identity(prior_pid)
    argv=shlex.split(prior_identity['args'])
    if (len(argv)!=2 or Path(argv[0]).name!='node'
            or (root/argv[1]).resolve()!=root/'tools/lume-progress-10290244/server.mjs'
            or 'n'+str(root) not in prior_identity['cwd'].splitlines()
            or '127.0.0.1:18785' not in command(['lsof','-nP','-a','-p',str(prior_pid),'-iTCP:18785','-sTCP:LISTEN'])):
        raise RuntimeError('Owned server PID/command/cwd/listener identity differs')
    key=state/'message-access-key'
    if not key.is_file() or key.is_symlink() or key.stat().st_size>256:raise RuntimeError('Private capability metadata differs')
    key_sha=sha(key)
    messages={path.name:sha(path) for path in (state/'messages').glob('*.json') if path.is_file() and not path.is_symlink()}
    source_archive=root/'output/session-20261029-cpu30/public/src.tar.gz';source_sha=sha(source_archive)
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    (out/'before-server-receipt.json').write_bytes(before_server);(out/'before-server-receipt.json').chmod(0o600)
    (out/'before-status.json').write_bytes(before_status);(out/'before-status.json').chmod(0o600)
    created=datetime.now(timezone.utc).isoformat()
    public_receipt={'schemaVersion':1,'createdUTC':created,'task':'10.30 01:10',
        'nativeMacDevelopmentApp':True,'platform':closure['platform'],'architecture':closure['architecture'],
        'minimumSystemVersion':package['package']['minimumSystemVersion'],
        'highestDeclaredDependencyMinimum':closure['maximumDeclaredMinimum'],
        'sourceCommit':build['sourceBaseCommit'],'reviewedProductSource':build.get('reviewedProductSource'),
        'sourceBinarySHA256':source_binary,'relocatedSignedBinarySHA256':binary_sha,
        'archive':{**package['archive'],'path':'/lumen-macos.zip','url':origin+'/lumen-macos.zip'},
        'prebuiltV8':{'source':build['prebuiltV8'],'inspectedDistributionVersion':next(iter(v8_versions)),
            'versionEvidence':'Resolved versioned Homebrew source paths and immutable copied library hashes',
            'sourceCompilationPerformed':False,
            'originalLibraries':[{'name':Path(row['source']).name,'sha256':row['sourceSha256']} for row in v8_images]},
        'nativeICU':build.get('icuAttestation'),
        'runtimeResources':[{'name':row['name'],'sha256':row['sha256'],'bytes':row['bytes']} for row in package['package']['runtimeResources']],
        'dependencyImages':[{'name':Path(row['path']).name,'sha256':row['sha256'],
            'minimumMacOSVersion':row['minimumMacOSVersion']} for row in closure['files']],
        'acceptance':{'packageSummarySHA256':package_sha,'tlsSummarySHA256':sha(paths['tls']),
            'recordedRuntimeClosureVerified':bundle_verified,
            'resourcesSummarySHA256':sha(paths['resources']),'tlsCases':[{key:row[key] for key in ('name','passed','expectedCertificateAccepted')} for row in tls['cases']],
            'relocatedResourceScenarios':resources['scenarios'],
            'resourceScope':resources['scope'],'wptScope':'Frozen priority WPT results and native queue stress are reported separately on the progress page.'},
        'limitations':{'notarized':False,'testedOnAnotherMac':False,'olderMacOSRuntimeTested':False,
            'sharedSignedDeviceIPAExecuted':False,'fullOfficialWPTPassRate':None},
        'createdBy':{'model':'GPT-6 Astra Pro','agents':'built-in same-model subagents','remoteTool':'devinmac3d'}}
    receipt_path=out/'public-native-mac-receipt.json';atomic(receipt_path,public_receipt)
    receipt_sha=sha(receipt_path)
    report={'startedUTC':created,'published':False,'archive':public_receipt['archive'],
        'publicReceiptSHA256':receipt_sha,'priorServerIdentity':prior_identity,
        'scope':'Explicit native ZIP/receipt routes on the existing task server; no directory browsing',
        'privateCapabilityPublished':False,'privateInboxContentsPublished':False}
    log=(out/'server-restart.log').open('x');os.chmod(out/'server-restart.log',0o600)
    process=None;stopped=False

    def launch(native):
        env=dict(os.environ,PORT='18785',LUME_TASK_ID='20261029-cpu30',LUME_PROGRESS_STATE=str(state),
            LUME_SCORECARD=str(root/'agent/scorecard-20261029-cpu30.json'),
            LUME_SOURCE_ARCHIVE=str(source_archive),LUME_MESSAGE_SESSION_ID='102900614',
            LUME_ALLOWED_ORIGINS=','.join(server.get('allowedOrigins',[origin,'https://lume-102900614-gpt6-astra-pro.surge.sh'])))
        for key_name in ('LUME_NATIVE_MAC_ARCHIVE','LUME_NATIVE_MAC_RECEIPT'):env.pop(key_name,None)
        if native:env.update(LUME_NATIVE_MAC_ARCHIVE=str(archive),LUME_NATIVE_MAC_RECEIPT=str(receipt_path))
        child=subprocess.Popen(['node','tools/lume-progress-10290244/server.mjs'],cwd=root,env=env,
            stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        try:
            deadline=time.monotonic()+12
            while time.monotonic()<deadline:
                if child.poll() is not None:raise RuntimeError('Task server exited before readiness')
                try:
                    if (get_json(local+'/api/health').get('taskId')=='20261029-cpu30'
                            and '127.0.0.1:18785' in command(['lsof','-nP','-a','-p',str(child.pid),'-iTCP:18785','-sTCP:LISTEN'])):
                        return child
                except (OSError,ValueError,subprocess.SubprocessError):pass
                time.sleep(.2)
            raise RuntimeError('Task server readiness deadline exceeded')
        except Exception:
            if child.poll() is None:child.terminate();child.wait(timeout=8)
            raise

    def preserve_private():
        if sha(key)!=key_sha or any(sha(state/'messages'/name)!=digest for name,digest in messages.items()):
            raise RuntimeError('Existing private capability or inbox bytes changed during publication')
        if sha(source_archive)!=source_sha:raise RuntimeError('Existing source archive changed during native publication')

    try:
        if identity(prior_pid)!=prior_identity:raise RuntimeError('Task server identity changed before restart')
        os.kill(prior_pid,signal.SIGTERM)
        deadline=time.monotonic()+12
        while time.monotonic()<deadline:
            probe=subprocess.run(['ps','-p',str(prior_pid),'-o','stat=,lstart='],capture_output=True,text=True,timeout=3)
            if probe.returncode or not probe.stdout.strip() or probe.stdout.strip().startswith('Z') or prior_identity['start'] not in probe.stdout:
                stopped=True;break
            time.sleep(.2)
        if not stopped:raise RuntimeError('Owned task server did not stop; no forced kill attempted')
        process=launch(True)
        for base in (local,origin):
            if get_json(base+'/api/health').get('taskId')!='20261029-cpu30':raise RuntimeError('Restarted task route identity differs')
        if get_json(origin+'/lumen-macos-receipt.json')!=public_receipt:raise RuntimeError('Public native receipt differs')
        digest=hashlib.sha256();size=0;download_started=time.monotonic()
        with urllib.request.urlopen(origin+'/lumen-macos.zip',timeout=10) as response:
            if int(response.headers.get('Content-Length','-1'))!=archive.stat().st_size:
                raise RuntimeError('Public native ZIP byte length differs')
            while block:=response.read(1024*1024):
                digest.update(block);size+=len(block)
                if size>archive.stat().st_size or time.monotonic()-download_started>90:
                    raise RuntimeError('Public native ZIP exceeded declared byte/time budget')
        if size!=archive.stat().st_size or digest.hexdigest()!=package['archive']['sha256']:
            raise RuntimeError('Public native ZIP full-byte hash differs')
        preserve_private()
        if status_path.read_bytes()!=before_status:raise RuntimeError('Live status changed during publication')
        updated=copy.deepcopy(status)
        updated['nativeMacDelivery']={'published':True,'publishedUTC':datetime.now(timezone.utc).isoformat(),
            'sourceBinarySHA256':source_binary,'signedBinarySHA256':binary_sha,
            'archiveSHA256':digest.hexdigest(),'archiveBytes':size,
            'minimumSystemVersion':public_receipt['minimumSystemVersion'],
            'architecture':'arm64','platform':'macos','publicReceiptSHA256':receipt_sha,
            'downloadURL':origin+'/lumen-macos.zip','receiptURL':origin+'/lumen-macos-receipt.json',
            'scope':'Self-contained native Mac development app; signed device IPA remains unverified'}
        updated['updatedUTC']=datetime.now(timezone.utc).isoformat();atomic(status_path,updated)
        if get_json(origin+'/api/progress')['status'].get('nativeMacDelivery')!=updated['nativeMacDelivery']:
            raise RuntimeError('Public progress did not expose the verified delivery identity')
        server.update(pid=process.pid,nativeMacArchive=str(archive.relative_to(root)),
            nativeMacReceipt=str(receipt_path.relative_to(root)),nativeMacPublished=True,
            restartedUTC=datetime.now(timezone.utc).isoformat(),serverAlive=True)
        atomic(state_receipt,server)
        report.update(published=True,newServerIdentity=identity(process.pid),
            publicArchiveFullByteSHA256=digest.hexdigest(),publicArchiveBytes=size,
            publicReadbackSeconds=round(time.monotonic()-download_started,3),
            privateCapabilityUnchanged=True,priorInboxMessagesUnchanged=len(messages),sourceArchiveUnchanged=True)
    except Exception as error:
        report['error']=type(error).__name__+': '+str(error)[:500]
        if process is not None and process.poll() is None:
            process.terminate()
            try:process.wait(timeout=8)
            except subprocess.TimeoutExpired:report['newServerCleanupIssue']='Owned replacement did not terminate promptly'
        if stopped and (process is None or process.poll() is not None):
            try:
                rollback=launch(False);server.update(pid=rollback.pid,serverAlive=True,nativeMacPublished=False)
                atomic(state_receipt,server)
                if status_path.read_bytes()!=before_status:
                    observed=json.loads(status_path.read_bytes())
                    if observed.get('nativeMacDelivery',{}).get('publicReceiptSHA256')==receipt_sha:atomic(status_path,status)
                report['priorRouteRestored']=get_json(origin+'/api/health').get('taskId')=='20261029-cpu30'
                report['rollbackPID']=rollback.pid;preserve_private()
            except Exception as recovery:report['rollbackError']=type(recovery).__name__+': '+str(recovery)[:250]
    finally:
        report['finishedUTC']=datetime.now(timezone.utc).isoformat();atomic(out/'receipt.json',report);log.close()
        print(json.dumps(report,indent=2))
    return 0 if report['published'] else 1


if __name__=='__main__':
    raise SystemExit(main())
