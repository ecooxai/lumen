#!/usr/bin/env python3
"""Renew the pinned fixture in a new evidence base without changing history."""
import argparse
from pathlib import Path
import hashlib
import json
import shutil

parser=argparse.ArgumentParser()
parser.add_argument('--output',type=Path,default=Path('output/session-102900614/wpt-fixture-1'))
args=parser.parse_args()
project=Path.cwd().resolve()
original_base=project/'output/wpt-10290244'
task_base=(project/args.output).resolve()
if project not in task_base.parents:raise ValueError('Fixture output must stay inside the project')
task_base.mkdir(parents=True,exist_ok=False)
original_names=['priority-selection-v1.json','inventory.json','server.config.json','fixture-readiness.json','browser-fixture-environment.json']
original_hashes={name:hashlib.sha256((original_base/name).read_bytes()).hexdigest() for name in original_names}
for name in ['priority-selection-v1.json','inventory.json']:shutil.copy2(original_base/name,task_base/name)
(task_base/'historical-input-hashes.json').write_text(json.dumps(original_hashes,indent=2)+'\n')

"""Start the pinned upstream WPT server with an exact reporter-file alias."""
import base64, datetime, hashlib, http.client, json, os, pathlib, signal, socket, ssl, subprocess, time, urllib.request

ROOT=pathlib.Path.cwd();BASE=task_base;UPSTREAM=original_base/'upstream'
REVISION='7f8d49b5a82606efc771f43d10aad5bce5ccbfeb'
REPORTER=ROOT/'tests/wpt/testharnessreport.js'
CA=UPSTREAM/'tools/certs/cacert.pem';CERT=UPSTREAM/'tools/certs/web-platform.test.pem'
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
def atomic(path,value):
    temporary=path.with_suffix('.tmp');temporary.write_text(json.dumps(value,indent=2)+'\n');temporary.replace(path)
revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=UPSTREAM,text=True).strip()
if revision!=REVISION:raise RuntimeError('Unexpected upstream WPT revision')
subprocess.run(['git','diff','--exit-code','HEAD','--'],cwd=UPSTREAM,check=True,capture_output=True)
inventory=json.loads((BASE/'inventory.json').read_text())
if inventory['wpt_revision']!=REVISION or inventory['manifest_sha256']!=sha(UPSTREAM/'MANIFEST.json'):raise RuntimeError('Manifest inventory identity mismatch')
ports={'http':[18800,18801],'http-local':[None],'http-public':[None],'https':[18443,18444],'https-local':[None],'https-public':[None],'h2':[18900],'ws':[None],'wss':[None],'webtransport-h3':[None],'dns':[None]}
for port in [value for values in ports.values() for value in values if value is not None]:
    with socket.socket() as probe:
        try:probe.bind(('127.0.0.1',port))
        except OSError:raise RuntimeError(f'WPT port {port} already in use; no existing process was modified')
config={'server_host':'127.0.0.1','browser_host':'web-platform.test','alternate_hosts':{'alt':'not-web-platform.test'},'check_subdomains':False,'bind_address':True,'ports':ports,'aliases':[{'url-path':'/resources/testharnessreport.js','local-dir':str(REPORTER.parent)}],'logging':{'level':'info','suppress_handler_traceback':False}}
config_path=BASE/'server.config.json';atomic(config_path,config)
log=(BASE/'server.log').open('a')
started=datetime.datetime.now(datetime.timezone.utc).isoformat()
process=subprocess.Popen(['python3.12','wpt','serve','--config',str(config_path)],cwd=UPSTREAM,stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL,start_new_session=True)
receipt={'schema':1,'startedUTC':started,'server_pid':process.pid,'wpt_revision':REVISION,'manifest_sha256':inventory['manifest_sha256'],'server_config_sha256':sha(config_path),'reporter_sha256':sha(REPORTER),'ca_sha256':sha(CA),'ports':ports,'origin':'http://web-platform.test:18800','secure_origin':'https://web-platform.test:18443','h2_origin':'https://web-platform.test:18900','scope':'Pinned upstream WPT server with a custom completion reporter; sparse selected upstream sources, not a full official WPT run','limits':['DNS mapping and certificate trust are confined to test processes.','No system hosts file or global trust store is modified.','WebSocket, WebTransport and external address-space fixture listeners are disabled for this initial declared source selection.','HTTP/2 is configured but needs separate readiness evidence if selected cases require it.'],'fixture_ready':False}
opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
def http_get(port,resource):
    request=urllib.request.Request(f'http://127.0.0.1:{port}{resource}',headers={'Host':f'web-platform.test:{port}'})
    with opener.open(request,timeout=2) as response:
        data=response.read(1024*1024)
        return {'status':response.status,'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)}
def https_get(port,resource):
    context=ssl.create_default_context(cafile=str(CA))
    with socket.create_connection(('127.0.0.1',port),timeout=3) as raw:
        with context.wrap_socket(raw,server_hostname='web-platform.test') as secured:
            secured.sendall(f'GET {resource} HTTP/1.1\r\nHost: web-platform.test:{port}\r\nConnection: close\r\n\r\n'.encode())
            response=http.client.HTTPResponse(secured);response.begin();data=response.read(1024*1024)
            return {'status':response.status,'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'tls':secured.version(),'hostnameValidated':True,'caValidated':True}
try:
    deadline=time.monotonic()+45;last_error=''
    while time.monotonic()<deadline:
        if process.poll()is not None:raise RuntimeError('Official WPT server exited; inspect server.log')
        try:
            reporter=http_get(18800,'/resources/testharnessreport.js')
            if reporter['sha256']!=receipt['reporter_sha256']:raise RuntimeError('Exact reporter alias bytes differ')
            harness=http_get(18800,'/resources/testharness.js')
            if harness['sha256']!=sha(UPSTREAM/'resources/testharness.js'):raise RuntimeError('Upstream testharness bytes differ')
            secure=https_get(18443,'/resources/testharnessreport.js')
            if secure['sha256']!=receipt['reporter_sha256']:raise RuntimeError('Secure reporter alias bytes differ')
            alternate_http=http_get(18801,'/resources/testharnessreport.js')
            alternate_https=https_get(18444,'/resources/testharnessreport.js')
            receipt['readiness']={'reporter_http':reporter,'upstream_harness_http':harness,'reporter_https':secure,'alternate_http':alternate_http,'alternate_https':alternate_https}
            receipt['fixture_ready']=True;break
        except Exception as error:
            last_error=type(error).__name__+': '+str(error);time.sleep(.25)
    if not receipt['fixture_ready']:raise RuntimeError('Readiness deadline exceeded: '+last_error)
    publickey=subprocess.run(['openssl','x509','-in',str(CERT),'-pubkey','-noout'],capture_output=True,check=True).stdout
    der=subprocess.run(['openssl','pkey','-pubin','-outform','DER'],input=publickey,capture_output=True,check=True).stdout
    receipt['certificate_spki_sha256_base64']=base64.b64encode(hashlib.sha256(der).digest()).decode()
    receipt['server_start_identity']=subprocess.check_output(['ps','-p',str(process.pid),'-o','lstart='],text=True).strip()
    receipt['server_identity_error']=None
    subprocess.run(['git','diff','--exit-code','HEAD','--'],cwd=UPSTREAM,check=True,capture_output=True)
except Exception as error:
    receipt['fixture_ready']=False
    receipt['error']=type(error).__name__+': '+str(error)
    if process.poll()is None:
        os.killpg(process.pid,signal.SIGTERM)
        try:process.wait(timeout=5)
        except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait(timeout=3)
finally:
    log.close();receipt['finishedUTC']=datetime.datetime.now(datetime.timezone.utc).isoformat();atomic(BASE/'fixture-readiness.json',receipt)
print(json.dumps(receipt,indent=2))
if not receipt['fixture_ready'] or receipt.get('error'):raise RuntimeError('Renewed official WPT fixture did not become ready')

"""Derive exact process-local DNS and certificate overrides from upstream config."""
import datetime, hashlib, json, pathlib, subprocess

ROOT=pathlib.Path.cwd();BASE=task_base;UPSTREAM=original_base/'upstream'
readiness=BASE/'fixture-readiness.json';fixture=json.loads(readiness.read_text())
if not fixture.get('fixture_ready') or fixture.get('error'):raise RuntimeError('Official fixture is not ready')
identity=subprocess.check_output(['ps','-p',str(fixture['server_pid']),'-o','lstart='],text=True).strip()
if identity!=fixture['server_start_identity']:raise RuntimeError('WPT server identity changed')
python=UPSTREAM/'_venv3/bin/python'
code='''import json, logging, sys
from tools import localpaths
from tools.serve.serve import build_config
with build_config(logging.getLogger('lume-fixture'), config_path=sys.argv[1]) as config:
    print(json.dumps({'domains':sorted(config.domains_set),'nonexistent_domains':sorted(config.not_domains_set),'ports':config.ports,'browser_host':config.browser_host,'server_host':config.server_host}))
'''
result=subprocess.run([str(python),'-c',code,str(BASE/'server.config.json')],cwd=UPSTREAM,check=True,capture_output=True,text=True,timeout=15)
effective=json.loads(result.stdout)
domains=effective['domains'];nonexistent=effective['nonexistent_domains']
if not domains or not all(name.isascii() and ',' not in name and '=' not in name for name in domains):raise RuntimeError('Invalid authoritative DNS inventory')
if set(domains)&set(nonexistent):raise RuntimeError('Nonexistent WPT host would be mapped')
if effective['server_host']!='127.0.0.1' or effective['browser_host']!='web-platform.test':raise RuntimeError('Unexpected upstream server identity')
mapping=','.join(name+'=127.0.0.1' for name in domains)
rules=', '.join('MAP '+name+' 127.0.0.1' for name in domains)
value={'schema':1,'createdUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),'fixtureManifestSHA256':hashlib.sha256(readiness.read_bytes()).hexdigest(),'hostMapImplementationSHA256':hashlib.sha256((ROOT/'src/net/http.c').read_bytes()).hexdigest(),'validDomains':domains,'unmappedNonexistentDomains':nonexistent,'nativeEnvironment':{'LUMEN_HOST_MAP':mapping,'LUMEN_HOST_MAP_MODE':'exact','SSL_CERT_FILE':str(UPSTREAM/'tools/certs/cacert.pem')},'chromiumArguments':['--host-resolver-rules='+rules,'--ignore-certificate-errors-spki-list='+fixture['certificate_spki_sha256_base64']],'origins':{'http':fixture['origin'],'https':fixture['secure_origin'],'h2':fixture['h2_origin']},'scope':'Per-process exact host mapping and exact test certificate SPKI; no global DNS/trust changes or broad TLS bypass.'}
target=BASE/'browser-fixture-environment.json'
if target.exists() and json.loads(target.read_text()).get('fixtureManifestSHA256')!=value['fixtureManifestSHA256']:raise RuntimeError('Existing browser environment belongs to another fixture; preserve it before creating a new cohort')
target.write_text(json.dumps(value,indent=2)+'\n')
print(json.dumps({'output':str(target.relative_to(ROOT)),'validDomains':len(domains),'unmappedNonexistentDomains':len(nonexistent),'nativeHostMapMode':'exact','origins':value['origins'],'fixtureManifestSHA256':value['fixtureManifestSHA256']},indent=2))


for name,digest in original_hashes.items():
    if hashlib.sha256((original_base/name).read_bytes()).hexdigest()!=digest:raise RuntimeError('Historical fixture evidence changed: '+name)
import importlib.util
loader=importlib.util.spec_from_file_location('renewed_fixture_inputs',project/'tools/apple-session-gpt6as/run_priority_wpt_10290244.py')
module=importlib.util.module_from_spec(loader);loader.loader.exec_module(module)
module.BASE=task_base
frozen,categories,environment,hashes,runner=module.current_inputs()
summary={'fixture':str(task_base.relative_to(project)),'historicalInputsUnchanged':True,
         'serverPID':fixture['server_pid'],'serverStartIdentity':fixture['server_start_identity'],
         'frozenVariants':sum(map(len,categories.values())),'scheduleSHA256':frozen['schedule_sha256'],
         'upstreamRevision':frozen['wpt_revision'],'inputs':hashes,'ready':True}
(task_base/'restoration-receipt.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({'fixture':summary['fixture'],'ready':True,'frozenVariants':summary['frozenVariants'],'serverPID':summary['serverPID']}),flush=True)
