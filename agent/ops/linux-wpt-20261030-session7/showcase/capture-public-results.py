"""Capture hash-pinned public results with isolated Chromium; never launch Lumen.
Run only after final browser gates drain, via tools/codex_compile.py.
This writes PNG files and an encoding command; it does not execute FFmpeg.
"""
import argparse,base64,datetime,hashlib,json,os,pathlib,re,socket,struct,subprocess,sys,tempfile,time,urllib.parse,urllib.request,unicodedata
ROOT=pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'agent/validation'))
from paired_wpt import request,stop,monitor,endmonitor
from performance.compare import processes,start_identity,matched_process,cleanup_owned
CHROME=ROOT/'build/references/chrome-for-testing-156.0.8078.80/chrome-linux64/chrome'
DRIVER=ROOT/'build/references/chrome-for-testing-156.0.8078.80/chromedriver-linux64/chromedriver'
PINS={CHROME:'1bcad71ad1bff85955db13c6d8dd9f32374934d5a5cb079b526c46fb51f9bd97',DRIVER:'d7f1682dcf9e4087bae6a52ca03c9913390a79ab86e09796562db7ed6f534587'}
CREDITS=['GPT-6 Astra Pro','Built-in agents','RubyWebterm','Chromium','FFmpeg']
def sha(path):
 h=hashlib.sha256()
 with pathlib.Path(path).open('rb') as f:
  for block in iter(lambda:f.read(1048576),b''):h.update(block)
 return h.hexdigest()
def normalized(text):
 return ' '.join(unicodedata.normalize('NFKC',text).replace('\u2011','-').replace('\u2010','-').split()).casefold()
def check_assets(items):
 checked=[]
 for item in items:
  url=item['url'];pin=item['sha256'];parsed=urllib.parse.urlsplit(url)
  if parsed.scheme not in ('http','https') or parsed.username or parsed.password:raise ValueError('Require a public HTTP(S) URL without credentials')
  if not re.fullmatch('[0-9a-f]{64}',pin):raise ValueError('Invalid content SHA256 pin')
  req=urllib.request.Request(url,headers={'User-Agent':'Lumen-Public-Results-Capture/1.0','Accept-Encoding':'identity'})
  with urllib.request.urlopen(req,timeout=15) as response:
   body=response.read(67108865);resolved=response.url
  if len(body)>67108864:raise ValueError('Pinned asset exceeds64MiB capture limit')
  actual=hashlib.sha256(body).hexdigest()
  if actual!=pin:raise ValueError('Public asset content hash mismatch')
  checked.append({'url':url,'resolved_url':resolved,'sha256':actual,'bytes':len(body)})
 return checked
SCROLL="""const s=arguments[0]; const el=s==='top'?document.documentElement:document.querySelector(s); if(!el)return {found:false}; const y=s==='top'?0:Math.max(0,scrollY+el.getBoundingClientRect().top-16);window.scrollTo(0,y);return {found:true,target_y:y};"""
CREDIT="""const el=document.querySelector(arguments[0]);if(!el)return {found:false};const r=el.getBoundingClientRect(),s=getComputedStyle(el);return {found:true,text:el.innerText,visible:s.display!=='none'&&s.visibility!=='hidden'&&Number(s.opacity)>0&&r.width>0&&r.height>0&&r.left>=0&&r.top>=0&&r.right<=innerWidth&&r.bottom<=innerHeight,width:innerWidth,height:innerHeight,font_size:s.fontSize};"""
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--plan',required=True,type=pathlib.Path);ap.add_argument('--plan-sha256',required=True);ap.add_argument('--output',required=True,type=pathlib.Path);a=ap.parse_args()
 if sha(a.plan)!=a.plan_sha256:raise ValueError('Capture plan hash mismatch')
 plan=json.loads(a.plan.read_text());duration=float(plan.get('duration_seconds',36));credit=plan['credit_selector'];shots=list(plan['shots'])
 if not 30<=duration<=45 or not isinstance(credit,str) or not credit:raise ValueError('Require30–45seconds and a credit selector')
 if not 1<=len(shots)<=5 or not all(isinstance(x,str) and x for x in shots):raise ValueError('Require1–5 shot selectors')
 if credit not in shots:shots.append(credit)
 assets=[plan['page']]+plan.get('assets',[])
 if not assets or len(assets)>32:raise ValueError('Invalid asset pin list')
 for path,pin in PINS.items():
  if sha(path)!=pin:raise ValueError('Pinned Chromium/driver hash mismatch')
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);started=time.monotonic();result={'status':'ERROR','plan_sha256':a.plan_sha256,'adapter_sha256':sha(__file__),'method':'Public-results still-frame walkthrough rendered by pinned Chromium; not Lumen UI footage or a performance benchmark.','viewport':[1024,768],'duration_seconds':duration,'frames':[],'required_visible_credits':CREDITS,'capture_executed':True,'encoder_executed':False}
 driver=None;identity=None;mon=None;sid=None;owned=[];profile_owner=None;profile=None;cgroup=None;driver_log=None;cgroup_log=None;endpoint=None
 try:
  cgroup_log=(out/'cgroup.log').open('wb');cgroup=subprocess.Popen([sys.executable,str(ROOT/'agent/progress/cgroup_monitor.py'),'--output',str(out/'cgroup.csv'),'--duration','300'],stdout=cgroup_log,stderr=cgroup_log)
  result['assets_before']=check_assets(assets)
  tmp=ROOT/'agent/t';tmp.mkdir(exist_ok=True);profile_owner=tempfile.TemporaryDirectory(prefix='public-results-cft156-',dir=tmp);profile=profile_owner.name
  sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close();endpoint='http://127.0.0.1:'+str(port)
  driver_log=(out/'driver.log').open('wb');driver=subprocess.Popen([str(DRIVER),'--port='+str(port),'--allowed-ips=127.0.0.1'],stdout=driver_log,stderr=driver_log,stdin=subprocess.DEVNULL);identity={'pid':driver.pid,'start_ticks':start_identity(driver.pid)};mon=monitor(driver.pid,out/'owned-resources','isolated public-results capture')
  for _ in range(100):
   try:request(endpoint,'/status');break
   except Exception:time.sleep(.05)
  else:raise RuntimeError('ChromeDriver did not start')
  value=request(endpoint,'/session',{'capabilities':{'alwaysMatch':{'browserName':'chrome','pageLoadStrategy':'none','goog:chromeOptions':{'binary':str(CHROME),'args':['--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-component-update','--disable-sync','--no-proxy-server','--window-size=1024,768','--user-data-dir='+profile]}}}})
  sid=value['sessionId'];prefix='/session/'+sid;result['browser_version']=value['capabilities'].get('browserVersion')
  request(endpoint,prefix+'/goog/cdp/execute',{'cmd':'Emulation.setDeviceMetricsOverride','params':{'width':1024,'height':768,'deviceScaleFactor':1,'mobile':False}})
  request(endpoint,prefix+'/url',{'url':plan['page']['url']});deadline=time.monotonic()+45;ready=plan.get('ready_selector','body')
  while time.monotonic()<deadline:
   if matched_process(identity):owned.extend(processes(driver.pid))
   state=request(endpoint,prefix+'/execute/sync',{'script':"return document.readyState==='complete'&&(!document.fonts||document.fonts.status==='loaded')&&!!document.querySelector(arguments[0]);",'args':[ready]})
   if state:break
   time.sleep(.1)
  else:raise RuntimeError('Public report readiness timeout')
  viewport=request(endpoint,prefix+'/execute/sync',{'script':'return [innerWidth,innerHeight,devicePixelRatio];','args':[]})
  if viewport!=[1024,768,1]:raise RuntimeError('Viewport is not exact1024x768 at1x')
  for index,selector in enumerate(shots,1):
   if time.monotonic()-started>150:raise RuntimeError('Capture deadline exceeded')
   q=request(endpoint,prefix+'/execute/sync',{'script':SCROLL,'args':[selector]})
   if not q.get('found'):raise RuntimeError('Shot selector not found')
   time.sleep(.7)
   if matched_process(identity):owned.extend(processes(driver.pid))
   if selector==credit:
    proof=request(endpoint,prefix+'/execute/sync',{'script':CREDIT,'args':[credit]})
    if not proof.get('visible') or not all(normalized(x) in normalized(proof.get('text','')) for x in CREDITS):raise RuntimeError('Required credit text is not fully visible in the viewport')
    result['credit_proof']=proof
   encoded=request(endpoint,prefix+'/screenshot');png=base64.b64decode(encoded,validate=True);del encoded
   if len(png)<24 or png[:8]!=b'\x89PNG\r\n\x1a\n' or png[12:16]!=b'IHDR' or struct.unpack('>II',png[16:24])!=(1024,768):raise RuntimeError('Screenshot is not a1024x768 PNG')
   name='frame-%02d.png'%index;frame=out/name
   with frame.open('xb') as f:f.write(png)
   result['frames'].append({'file':name,'selector':selector,'sha256':hashlib.sha256(png).hexdigest(),'bytes':len(png),'hold_seconds':duration/len(shots)})
   del png
  result['assets_after']=check_assets(assets)
  if result['assets_before']!=result['assets_after']:raise RuntimeError('Public content changed during capture')
  if sha(a.plan)!=a.plan_sha256:raise RuntimeError('Capture plan changed')
  result['status']='PASS'
 except Exception as error:
  result['error_type']=type(error).__name__;result['error']=str(error)[:500]
 finally:
  if driver and identity and matched_process(identity):owned.extend(processes(driver.pid))
  if sid:
   try:request(endpoint,'/session/'+sid,method='DELETE')
   except Exception as error:result['session_cleanup_error']=type(error).__name__
  if driver:stop(driver);result['driver_actual_exit']=driver.returncode
  if mon:endmonitor(mon)
  if driver_log:driver_log.close()
  if profile:
   for proc in pathlib.Path('/proc').iterdir():
    if not proc.name.isdigit():continue
    try:args=(proc/'cmdline').read_bytes().split(b'\0')
    except OSError:continue
    if ('--user-data-dir='+profile).encode() in args:owned.extend(processes(int(proc.name)))
  result['owned_cleanup']=cleanup_owned(owned)
  if profile_owner:profile_owner.cleanup()
  result['profile_removed']=not profile or not pathlib.Path(profile).exists()
  if cgroup:
   cgroup.terminate()
   try:cgroup.wait(5)
   except subprocess.TimeoutExpired:cgroup.kill();cgroup.wait()
   result['cgroup_monitor_exit']=cgroup.returncode
  if cgroup_log:cgroup_log.close()
  result['elapsed_seconds']=time.monotonic()-started;result['browser_pins_unchanged']=all(sha(p)==h for p,h in PINS.items())
  result['cleanup_complete']=not result['owned_cleanup']['remaining_same_identity'] and result['profile_removed'] and result.get('cgroup_monitor_exit')==0
  if not result['cleanup_complete'] or not result['browser_pins_unchanged']:result['status']='ERROR'
  if result['status']=='PASS':
   lines=['ffconcat version 1.0']
   for frame in result['frames']:lines+=['file '+frame['file'],'duration %.9f'%frame['hold_seconds']]
   lines+=['file '+result['frames'][-1]['file']];(out/'frames.ffconcat').write_text('\n'.join(lines)+'\n')
   argv=['ffmpeg','-hide_banner','-loglevel','error','-nostdin','-n','-f','concat','-safe','1','-i',str(out/'frames.ffconcat'),'-vf','fps=30,format=yuv420p,setsar=1','-c:v','libx264','-threads','2','-preset','medium','-crf','18','-t',str(duration),'-an','-movflags','+faststart',str(out/'lumen-results-walkthrough.mp4')]
   (out/'encode-command.json').write_text(json.dumps(argv,indent=2)+'\n')
  (out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps({'status':result['status'],'frames':len(result['frames']),'viewport':result['viewport'],'duration_seconds':duration,'cleanup_complete':result['cleanup_complete'],'encoder_executed':False}))
 return 0 if result['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
