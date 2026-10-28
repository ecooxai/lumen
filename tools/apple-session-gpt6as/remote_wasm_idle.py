"""Bounded native post-WASM idle observation; no user profiles or device capture."""
import argparse, hashlib, json, math, os, pathlib, secrets, signal, subprocess, sys, threading, time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit, parse_qs

ROOT=pathlib.Path.cwd().resolve()
MIB=1024*1024
TELEMETRY=ROOT/'output/session-20261029/native-qa-kit-v1/source/scripts/qa-apple-gpt6/telemetry.py'
TELEMETRY_SHA='70a9c367e6b7649e5469167c4431f7dc0cb4ad428a9d89ceb291e7834aaa4ec2'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def utc():return datetime.now(timezone.utc).isoformat()
def idle_validation_issues(summary, minimum_samples=60, cpu_ceiling=3.0):
 """A recorded status is not an acceptance verdict without its measurements."""
 idle=summary.get('groups',{}).get('lumen',{}).get('idle',{})
 issues=[]
 def measured(value):return isinstance(value,(int,float))and not isinstance(value,bool)and math.isfinite(value)and value>=0
 samples=idle.get('observed_samples');duration=idle.get('duration_seconds');cpu=idle.get('cpu_p95_percent')
 if not measured(samples)or samples<minimum_samples:issues.append(f'Fewer than{minimum_samples} observed one-second settled idle samples')
 if not measured(duration)or duration<minimum_samples-1:issues.append('Measured settled idle duration is incomplete or unverified')
 if idle.get('cpu_status')!='within_threshold':issues.append('Settled idle CPU status is not within threshold')
 if not measured(cpu):issues.append('Settled idle CPU p95 is missing or invalid')
 elif cpu>cpu_ceiling:issues.append(f'Settled idle CPU p95 exceeds{cpu_ceiling:g}%')
 if summary.get('errors')or summary.get('sampling_gaps_over_1_5_seconds'):issues.append('Telemetry contains errors or gaps; inspect raw data')
 return issues
def atomic(p,d):
 t=p.with_suffix('.tmp');t.write_text(json.dumps(d,indent=2)+'\n');t.replace(p)
def stop(p):
 if p is None:return None
 forced=False
 if p.poll() is None:
  try:os.killpg(p.pid,signal.SIGTERM)
  except ProcessLookupError:pass
  try:p.wait(timeout=5)
  except subprocess.TimeoutExpired:
   forced=True
   try:os.killpg(p.pid,signal.SIGKILL)
   except ProcessLookupError:pass
   p.wait(timeout=3)
 return {'pid':p.pid,'returnCode':p.returncode,'forced':forced}

PAGE='''<!doctype html><meta charset="utf-8"><title>Native WASM idle verification</title><style>body{font:20px system-ui;background:#eaf4fc;color:#12344e;padding:50px}h1{font-size:32px}pre{white-space:pre-wrap}</style><h1>Native WebAssembly → idle</h1><pre id="result">Compiling eight real modules…</pre><script>
(async()=>{const started=performance.now(),values=[];let error='';try{
const leb=n=>{const a=[];do{let b=n&127;n>>>=7;if(n)b|=128;a.push(b);}while(n);return a;};
const section=(id,payload)=>[id,...leb(payload.length),...payload],count=2000;
const code=[...leb(count)];for(let n=0;n<count;n++)code.push(4,0,65,42,11);
const bytes=new Uint8Array([0,97,115,109,1,0,0,0,...section(1,[1,96,0,1,127]),...section(3,[...leb(count),...Array(count).fill(0)]),...section(7,[1,6,97,110,115,119,101,114,0,0]),...section(10,code)]);
for(let n=0;n<8;n++){const r=await WebAssembly.instantiate(bytes);values.push(r.instance.exports.answer());}
}catch(e){error=String(e);}
const report={values,error,compileMs:performance.now()-started,nonce:new URL(location.href).searchParams.get('nonce')};
document.getElementById('result').textContent=JSON.stringify(report,null,2)+'\\nNo application timers, animation frames, workers or media remain.';
await fetch('/report',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(report)});
})().catch(e=>console.error('WASM idle report failed',e));
</script>'''.encode()

def worker(args):
 binary=(ROOT/args.binary).resolve();binary.relative_to(ROOT)
 output=(ROOT/args.output).resolve();output.relative_to(ROOT)
 if not binary.is_file()or sha(binary)!=args.sha256:raise RuntimeError('Selected native binary hash mismatch')
 if not TELEMETRY.is_file()or sha(TELEMETRY)!=TELEMETRY_SHA:raise RuntimeError('Pinned Mac telemetry source hash mismatch')
 output.mkdir(parents=True,exist_ok=False)
 profile=output/'profile';profile.mkdir()
 nonce=secrets.token_hex(16);report={};report_lock=threading.Lock()
 receipt={'startedUTC':utc(),'scope':'Native Mac application executes real asynchronous WASM then >=60 seconds of settled process-tree idle sampling; no iPad/IPA or media claim','binary':str(binary.relative_to(ROOT)),'binarySHA256':args.sha256,'telemetrySHA256':TELEMETRY_SHA,'samplingSeconds':1,'profile':'new isolated task-owned profile','issues':[]}
 class Handler(BaseHTTPRequestHandler):
  def log_message(self,*_):pass
  def do_GET(self):
   q=urlsplit(self.path)
   if q.path!='/'or parse_qs(q.query).get('nonce')!=[nonce]:self.send_error(404);return
   self.send_response(200);self.send_header('Content-Type','text/html;charset=utf-8');self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(PAGE)));self.end_headers();self.wfile.write(PAGE)
  def do_POST(self):
   if self.path!='/report':self.send_error(404);return
   try:
    size=int(self.headers.get('Content-Length','0'))
    if not 0<size<=4096:raise ValueError('Invalid body size')
    d=json.loads(self.rfile.read(size))
    if d.get('nonce')!=nonce:raise ValueError('Wrong owned-fixture nonce')
    with report_lock:
     if report:raise ValueError('Duplicate report')
     report.update({'receivedUTC':utc(),'receivedMonotonic':time.monotonic(),'value':d})
    self.send_response(200);self.send_header('Content-Length','2');self.end_headers();self.wfile.write(b'{}')
   except (ValueError,TypeError):self.send_error(400)
 server=ThreadingHTTPServer(('127.0.0.1',0),Handler);server.daemon_threads=True
 serving=threading.Thread(target=server.serve_forever,daemon=True);serving.start()
 targets=output/'targets.json';samples=output/'process-samples.jsonl'
 def phase(name,pid=None):atomic(targets,{'phase':name,'groups':[{'name':'lumen','pids':[pid]}]if pid else []})
 app=sampler=None;streams=[];reader=None;log_issue=[];idle_started=None
 def read_log(stream):
  total=0
  with (output/'lumen.log').open('xb')as log:
   while True:
    chunk=stream.read(4096)
    if not chunk:break
    total+=len(chunk)
    if total>MIB:log_issue.append('Owned application log exceeded1MiB');break
    log.write(chunk);log.flush()
 try:
  phase('baseline')
  slog=(output/'sampler.log').open('x');streams.append(slog)
  sampler=subprocess.Popen([sys.executable,str(TELEMETRY),'collect','--targets',str(targets),'--output',str(samples),'--seconds','120'],stdout=slog,stderr=subprocess.STDOUT,start_new_session=True,stdin=subprocess.DEVNULL)
  environment=dict(os.environ);environment.update({'LUMEN_PROFILE_DIR':str(profile),'LUMEN_CONSOLE':'1','LUMEN_NO_SIDEBAR':'1','LUMEN_WINDOW':'1000x700','LUMEN_SHOT':str(output/'gpt6-astra-pro_qa-deploy_devinmac3d_wasm-idle.png'),'LUMEN_SHOT_MS':'80000'})
  url=f'http://127.0.0.1:{server.server_port}/?nonce={nonce}'
  receipt['urlScope']='Owned ephemeral loopback HTTP fixture; no external traffic';receipt['fixtureSHA256']=hashlib.sha256(PAGE).hexdigest()
  app=subprocess.Popen([str(binary),url],env=environment,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True,stdin=subprocess.DEVNULL)
  receipt['appPID']=app.pid;receipt['appStartedUTC']=utc();phase('functional',app.pid)
  reader=threading.Thread(target=read_log,args=(app.stdout,),daemon=True);reader.start()
  started=time.monotonic()
  while app.poll()is None:
   now=time.monotonic()
   if sampler.poll()is not None:raise RuntimeError('Sampler exited before the application')
   if log_issue:raise RuntimeError(log_issue[0])
   if now-started>105:raise RuntimeError('Native application failed its80-second screenshot/exit deadline')
   with report_lock:observed=dict(report)
   if observed:
    d=observed['value']
    if d.get('error')or d.get('values')!=[42]*8:raise RuntimeError('Real WASM computation did not match the eight expected results')
    if idle_started is None and now-observed['receivedMonotonic']>=5:
     idle_started=now;receipt['idleStartedUTC']=utc();phase('idle',app.pid)
   elif now-started>25:raise RuntimeError('No actual WASM completion report within25 seconds')
   time.sleep(.2)
  receipt['naturalAppReturnCode']=app.returncode
  receipt['idleWindowSeconds']=round(time.monotonic()-idle_started,3)if idle_started else 0
  if app.returncode!=0:raise RuntimeError('Native application did not exit cleanly')
  if receipt['idleWindowSeconds']<60:raise RuntimeError('Settled idle window was shorter than60 seconds')
 except BaseException as e:
  receipt['issues'].append(type(e).__name__+': '+str(e))
 finally:
  phase('shutdown',app.pid if app and app.poll()is None else None)
  receipt['appTermination']=stop(app);receipt['samplerTermination']=stop(sampler)
  server.shutdown();server.server_close();serving.join(timeout=2)
  if reader:reader.join(timeout=2)
  for s in streams:s.close()
  if log_issue:receipt['issues'].extend(log_issue)
  with report_lock:
   if report:
    data=dict(report['value']);data.pop('nonce',None);receipt['wasmReport']=data;receipt['reportReceivedUTC']=report['receivedUTC']
  receipt['binarySHA256After']=sha(binary);receipt['finishedUTC']=utc()
  if receipt['binarySHA256After']!=args.sha256:receipt['issues'].append('Binary changed during observation')
  if samples.exists():
   result=subprocess.run([sys.executable,str(TELEMETRY),'summarize',str(samples),'--output',str(output/'process-summary.json')],capture_output=True,text=True,timeout=10)
   receipt['summaryExit']=result.returncode
   if result.returncode:receipt['issues'].append('Telemetry summary failed: '+result.stderr[-1000:])
   else:
    summary=json.loads((output/'process-summary.json').read_text());idle=summary.get('groups',{}).get('lumen',{}).get('idle',{})
    receipt['idleCPUStatus']=idle.get('cpu_status');receipt['idleSamples']=idle.get('observed_samples');receipt['idleCPUP95Percent']=idle.get('cpu_p95_percent');receipt['idleRSSChangeMiB']=idle.get('rss_change_mib')
    receipt['issues'].extend(idle_validation_issues(summary))
  atomic(output/'receipt.json',receipt)
 print(json.dumps({'receipt':str((output/'receipt.json').relative_to(ROOT)),'issues':receipt['issues'],'idleCPUStatus':receipt.get('idleCPUStatus'),'idleSamples':receipt.get('idleSamples'),'idleCPUP95Percent':receipt.get('idleCPUP95Percent')}))
 return 1 if receipt['issues']else 0

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--binary',required=True);p.add_argument('--sha256',required=True);p.add_argument('--output',required=True)
 sys.exit(worker(p.parse_args()))
