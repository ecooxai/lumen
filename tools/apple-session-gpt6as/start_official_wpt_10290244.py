"""Start the pinned upstream WPT server with an exact reporter-file alias."""
import base64, datetime, hashlib, http.client, json, os, pathlib, signal, socket, ssl, subprocess, time, urllib.request

ROOT=pathlib.Path.cwd();BASE=ROOT/'output/wpt-10290244';UPSTREAM=BASE/'upstream'
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
raise SystemExit(0 if receipt['fixture_ready'] and not receipt.get('error') else 1)
