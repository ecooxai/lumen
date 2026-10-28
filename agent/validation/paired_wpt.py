#!/usr/bin/env python3
"""Run identical upstream testharness URLs in frozen Lumen binaries / local Chromium.
No edits to WPT resources; own profiles, frozen binaries, exact one-second monitor.
"""
import argparse,hashlib,json,os,pathlib,signal,socket,subprocess,tempfile,time,urllib.request,urllib.error
ROOT=pathlib.Path(__file__).resolve().parents[2]
PRE=r'''(function(){if(window.opener)return;var done=false;var log=console.error.bind(console);var stringify=JSON.stringify;Object.defineProperty(window,"__lumenWpt",{value:{harness:false,report:function(r){if(done)return;done=true;r.environment={width:innerWidth,height:innerHeight,dpr:devicePixelRatio,userAgent:navigator.userAgent};window.__astraResult=r;log("ASTRAWPT-H "+stringify({status:r.status,message:r.message,environment:r.environment}));r.subtests.forEach(function(s){log("ASTRAWPT-S "+stringify([String(s[0]).slice(0,900),s[1],s[2]==null?null:String(s[2]).slice(0,500)]));});log("ASTRAWPT-E");}}});})();'''
URLS=['/performance-timeline/idlharness.any.html','/performance-timeline/idlharness.any.worker.html','/performance-timeline/po-observe.any.html','/performance-timeline/po-observe.any.worker.html','/performance-timeline/po-observe-type.any.html','/performance-timeline/po-observe-type.any.worker.html','/user-timing/idlharness.any.html','/user-timing/idlharness.any.worker.html','/user-timing/mark-errors.any.html','/user-timing/mark-errors.any.worker.html','/user-timing/measure-with-dict.any.html','/user-timing/measure-with-dict.any.worker.html']
def full_url(u):
 name=u.split('?',1)[0].rsplit('/',1)[-1];https=any(x in name.split('.') for x in ('https','serviceworker','serviceworker-module'))
 return ('https://web-platform.test:8443' if https else 'http://web-platform.test:8000')+u

def monitor(pid,path,label):
 f=open(str(path)+'.monitor.log','wb')
 return subprocess.Popen(['python3',str(ROOT/'agent/progress/monitor.py'),'--root-pid',str(pid),'--output',str(path)+'.csv','--label',label],stdout=f,stderr=f,stdin=subprocess.DEVNULL),f

def endmonitor(m):
 p,f=m
 if p.poll() is None:p.terminate()
 try:p.wait(4)
 except subprocess.TimeoutExpired:p.kill();p.wait()
 f.close()

def stop(p):
 if p.poll() is None:
  p.terminate()
  try:p.wait(4)
  except subprocess.TimeoutExpired:p.kill();p.wait()

def lumen(binary,u,out,timeout):
 with tempfile.TemporaryDirectory(prefix='profile-',dir=out) as prof:
  pre=pathlib.Path(prof)/'pre.js';pre.write_text(PRE)
  env=dict(os.environ,XDG_DATA_HOME=prof,LUMEN_NO_SESSION='1',LUMEN_HOST_MAP='web-platform.test=127.0.0.1,not-web-platform.test=127.0.0.1',SSL_CERT_FILE='/tmp/lumen-wpt/ca.pem',LUMEN_PRE_FILE=str(pre),LUMEN_WINDOW='800x640',LUMEN_CONSOLE='2',LUMEN_JS_TIMEOUT_MS='60000',LUMEN_TESTDRIVER='1',SDL_VIDEODRIVER='offscreen',LUMEN_NO_GPU='1')
  log=out/'browser.log';f=log.open('wb',buffering=0);t0=time.monotonic()
  p=subprocess.Popen([binary,full_url(u)],env=env,stdout=f,stderr=f,stdin=subprocess.DEVNULL)
  m=monitor(p.pid,out/'resources','lumen '+u);result=None
  try:
   while time.monotonic()-t0<timeout and p.poll() is None:
    time.sleep(.2);text=log.read_text(errors='replace')
    if 'JS prelude failed' in text:
     result={'status':'BOOTSTRAP_ERROR','message':next(x for x in text.splitlines() if 'JS prelude failed' in x),'subtests':[]};break
    if 'ASTRAWPT-E' in text:
     result={'subtests':[]}
     for line in text.splitlines():
      for tag in ('ASTRAWPT-H ','ASTRAWPT-S '):
       pos=line.find(tag)
       if pos>=0:
        try:v=json.loads(line[pos+len(tag):])
        except ValueError:continue
        if tag=='ASTRAWPT-H ':result.update(v)
        else:result['subtests'].append(v)
     break
   elapsed=time.monotonic()-t0
   if result is None:result={'status':'CRASH' if p.poll() is not None else 'TIMEOUT','message':'Browser exited or no complete testharness result within external deadline','subtests':[]}
   result['elapsed_s']=round(elapsed,4)
   # Keep process alive until at least one interval sample exists.
   if elapsed<1.2:time.sleep(1.2-elapsed)
  finally:endmonitor(m);stop(p);f.close()
  result['errors']=[x[:500] for x in log.read_text(errors='replace').splitlines() if any(k in x.lower() for k in ('error','uncaught','unhandled','sigsegv','double free')) and 'ASTRAWPT-' not in x][:30]
  return result

def request(base,path,data=None,method=None):
 req=urllib.request.Request(base+path,data=None if data is None else json.dumps(data).encode(),headers={'Content-Type':'application/json'},method=method or ('POST' if data is not None else 'GET'))
 with urllib.request.urlopen(req,timeout=15) as r:return json.load(r)['value']

def chromium(binary,driver,u,out,timeout):
 with tempfile.TemporaryDirectory(prefix='profile-',dir=out) as prof:
  sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close();base='http://127.0.0.1:'+str(port)
  f=(out/'driver.log').open('wb');p=subprocess.Popen([driver,'--port='+str(port),'--allowed-ips=127.0.0.1'],stdout=f,stderr=f,stdin=subprocess.DEVNULL);m=monitor(p.pid,out/'resources','chromium tree '+u);sid=None
  result=None
  try:
   for _ in range(100):
    try:request(base,'/status');break
    except Exception:time.sleep(.05)
   val=request(base,'/session',{'capabilities':{'alwaysMatch':{'browserName':'chrome','pageLoadStrategy':'none','goog:chromeOptions':{'binary':binary,'args':['--headless=new','--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-component-update','--disable-sync','--no-proxy-server','--ignore-certificate-errors','--host-resolver-rules=MAP *.test 127.0.0.1','--window-size=800,640','--user-data-dir='+prof]}}}})
   sid=val['sessionId'];path='/session/'+sid
   request(base,path+'/goog/cdp/execute',{'cmd':'Page.addScriptToEvaluateOnNewDocument','params':{'source':PRE}})
   request(base,path+'/goog/cdp/execute',{'cmd':'Emulation.setDeviceMetricsOverride','params':{'width':748,'height':596,'deviceScaleFactor':1,'mobile':False}})
   t0=time.monotonic();request(base,path+'/url',{'url':full_url(u)})
   while time.monotonic()-t0<timeout:
    time.sleep(.2)
    try:result=request(base,path+'/execute/sync',{'script':'return window.__astraResult || null','args':[]})
    except Exception:continue
    if result is not None:break
   if result is None:result={'status':'TIMEOUT','message':'No complete testharness result within external deadline','subtests':[]}
   result['elapsed_s']=round(time.monotonic()-t0,4);result['capabilities']=val['capabilities']
  except Exception as e:result={'status':'HARNESS_ERROR','message':repr(e),'subtests':[]}
  finally:
   endmonitor(m)
   if sid:
    try:request(base,'/session/'+sid,method='DELETE')
    except Exception:pass
   stop(p);f.close()
  return result

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--kind',choices=['lumen','chromium','chrome-cdp'],required=True);ap.add_argument('--binary',required=True);ap.add_argument('--driver',default='/home/a/.local/archpkg/pacman/usr/bin/chromedriver');ap.add_argument('--output',required=True);ap.add_argument('--timeout',type=float,default=60);ap.add_argument('--urls-file');ap.add_argument('--repeat',type=int,default=1);a=ap.parse_args()
 out=pathlib.Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True);urls=json.load(open(a.urls_file)) if a.urls_file else URLS
 meta={'kind':a.kind,'binary':os.path.realpath(a.binary),'binary_sha256':hashlib.sha256(open(a.binary,'rb').read()).hexdigest(),'started_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'urls':urls,'repeat':a.repeat,'method':'fresh browser process and empty profile per URL; WPT resources unchanged; offscreen software Lumen vs headless Chromium; no display/GPU claim'}
 json.dump(meta,open(out/'metadata.json','w'),indent=2)
 with open(out/'results.jsonl','a') as f:
  for rep in range(a.repeat):
   for i,u in enumerate(urls):
    dst=out/('%02d-%03d'%(rep,i));dst.mkdir(exist_ok=True)
    if a.kind=='chrome-cdp':
     from chrome_cdp import run as cdp_run
     r=cdp_run(os.path.realpath(a.binary),u,dst,a.timeout)
    else:r=lumen(os.path.realpath(a.binary),u,dst,a.timeout) if a.kind=='lumen' else chromium(os.path.realpath(a.binary),a.driver,u,dst,a.timeout)
    r.update(url=u,repeat=rep,evidence_dir=str(dst));json.dump(r,open(dst/'result.json','w'),indent=2);f.write(json.dumps(r)+'\n');f.flush()
    print(json.dumps({'url':u,'status':r['status'],'pass':sum(x[1]=='PASS' for x in r['subtests']),'total':len(r['subtests']),'seconds':r.get('elapsed_s')}),flush=True)
    if r['status']=='BOOTSTRAP_ERROR':
     meta['halted_reason']=r['message'];json.dump(meta,open(out/'metadata.json','w'),indent=2);return 2
 meta['finished_utc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime());json.dump(meta,open(out/'metadata.json','w'),indent=2)
if __name__=='__main__':main()
