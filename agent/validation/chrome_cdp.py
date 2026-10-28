#!/usr/bin/env python3
"""Minimal CDP runner using WPT's own vendored websockets dependency."""
import json,os,pathlib,subprocess,sys,tempfile,time
sys.path.insert(0,'/mnt/p3/project/browser/wpt/tools/third_party/websockets/src')
from websockets.sync.client import connect
from paired_wpt import PRE,full_url,monitor,endmonitor,stop

def run(binary,u,out,timeout):
 with tempfile.TemporaryDirectory(prefix='profile-',dir=out,ignore_cleanup_errors=True) as prof:
  f=(out/'browser.log').open('wb');p=subprocess.Popen([binary,'--headless=new','--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-component-update','--disable-sync','--no-proxy-server','--ignore-certificate-errors','--host-resolver-rules=MAP *.test 127.0.0.1','--window-size=800,640','--user-data-dir='+prof,'--remote-debugging-port=0','about:blank'],stdout=f,stderr=f,stdin=subprocess.DEVNULL)
  m=monitor(p.pid,out/'resources','chrome cdp tree '+u);result=None;ws=None
  try:
   active=pathlib.Path(prof)/'DevToolsActivePort'
   for _ in range(200):
    if active.exists():break
    if p.poll() is not None:raise RuntimeError('Chrome exited '+str(p.returncode))
    time.sleep(.05)
   lines=active.read_text().splitlines();ws=connect('ws://127.0.0.1:'+lines[0]+lines[1],open_timeout=10,max_size=32*1024*1024,compression=None);seq=0
   def call(method,params=None,session=None):
    nonlocal seq
    seq+=1;msg={'id':seq,'method':method,'params':params or {}}
    if session:msg['sessionId']=session
    ws.send(json.dumps(msg));deadline=time.monotonic()+12
    while time.monotonic()<deadline:
     r=json.loads(ws.recv(timeout=12))
     if r.get('id')==seq:
      if 'error' in r:raise RuntimeError(r['error'])
      return r.get('result',{})
    raise TimeoutError(method)
   version=call('Browser.getVersion');target=call('Target.createTarget',{'url':'about:blank'})['targetId'];session=call('Target.attachToTarget',{'targetId':target,'flatten':True})['sessionId']
   call('Page.enable',session=session);call('Page.addScriptToEvaluateOnNewDocument',{'source':PRE},session);call('Emulation.setDeviceMetricsOverride',{'width':748,'height':596,'deviceScaleFactor':1,'mobile':False},session)
   t0=time.monotonic();call('Page.navigate',{'url':full_url(u)},session)
   while time.monotonic()-t0<timeout:
    time.sleep(.2)
    req=call('Runtime.evaluate',{'expression':'window.__astraNativeRequest || null','returnByValue':True},session).get('result',{}).get('value')
    if req:
     from cdp_input import dispatch
     dispatch(call,session,req['ops']);call('Runtime.evaluate',{'expression':'window.__astraInputDone('+str(req['id'])+')'},session)
    try:r=call('Runtime.evaluate',{'expression':'window.__astraResult || null','returnByValue':True},session);result=r.get('result',{}).get('value')
    except RuntimeError:continue
    if result is not None:break
   if result is None:result={'status':'TIMEOUT','message':'No complete testharness result within external deadline','subtests':[]}
   result['elapsed_s']=round(time.monotonic()-t0,4);result['version']=version
   if os.environ.get('ASTRA_CAPTURE_SCREENSHOT')=='1':
    import base64
    call('Runtime.evaluate',{'expression':'new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)))','awaitPromise':True},session)
    shot=call('Page.captureScreenshot',{'format':'png','captureBeyondViewport':False},session)
    (out/'render.png').write_bytes(base64.b64decode(shot['data']))
  except Exception as e:result={'status':'HARNESS_ERROR','message':repr(e),'subtests':[]}
  finally:
   endmonitor(m)
   if ws:
    try:ws.send(json.dumps({'id':99999,'method':'Browser.close'}));ws.close()
    except Exception:pass
   stop(p);f.close()
  return result
