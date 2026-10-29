#!/usr/bin/env python3
"""Observe HTML default-encoding policy with unchanged parent-policy fixtures.

No expectations are rewritten. This records current Chrome/Lume behavior before
considering a product default change. All pages are owned loopback responses.
"""
from datetime import datetime, timezone
import hashlib, importlib.util, json, os, secrets, subprocess, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path.cwd().resolve()
OUT = ROOT / 'out/lume-20261029-cpu30/html-default-policy-1'
BUILD = ROOT / 'out/lume-20261029-cpu30/native-codec-1/summary.json'
NATIVE_SHA = '9137de2fb8cba151b7a4ca4f21aa02771061068cd41aa4327de4e3e20d709553'
CHROME = ROOT / 'output/session-102900614/chrome-official-reference-v2/Google Chrome.app/Contents/MacOS/Google Chrome'
CHROME_SHA = '0a8cb6eba2bee365c01fb0612f64f1d15855fd499dd54625ced1fe50215e5fb4'
FRAME_FIXTURE = ROOT / 'tests/test_frame_encoding_fallback.py'
FRAME_SHA = 'e0dac5f09136cb13de02b7467fa5d8c8fdb0700e7210ef70e05b6b3224a2db40'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
utc = lambda: datetime.now(timezone.utc).isoformat()

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec); sys.modules[name] = m
    spec.loader.exec_module(m); return m

def stop_owned(process):
    mon = module('default_policy_owned_monitor', ROOT / 'tools/monitor_process.py')
    receipt = mon.cleanup_owned_group(process, {}, grace=5)
    until = time.monotonic()+2
    while mon.owned_group_members(process.pid) and time.monotonic()<until: time.sleep(.2)
    if receipt.get('error') or mon.owned_group_members(process.pid):
        raise RuntimeError('Owned process cleanup incomplete')
    return process.poll()

# These child responses are loaded from an explicit Windows-1251 carrier. JSON
# and XML defaults therefore cannot pass accidentally by inheriting UTF-8.
MIMES = [
    ('HTML inherited parent', 'text/html', b'<p id=payload>\xc3\xa9</p>', False),
    ('HTML explicit UTF8', 'text/html;charset=utf-8', b'<p id=payload>\xc3\xa9</p>', False),
    ('HTML UTF8 meta', 'text/html', b'<meta charset=utf-8><p id=payload>\xc3\xa9</p>', False),
    ('HTML UTF8 BOM', 'text/html', b'\xef\xbb\xbf<p id=payload>\xc3\xa9</p>', False),
    ('JSON UTF8 default', 'application/json', b'"\xc3\xa9"', True),
    ('text JSON UTF8 default', 'text/json', b'"\xc3\xa9"', True),
    ('suffix JSON UTF8 default', 'application/problem+json', b'"\xc3\xa9"', True),
    ('XML UTF8 default', 'application/xml', b'<root>\xc3\xa9</root>', True),
    ('text XML UTF8 default', 'text/xml', b'<root>\xc3\xa9</root>', True),
    ('SVG UTF8 default', 'image/svg+xml', b'<svg xmlns="http://www.w3.org/2000/svg"><text>\xc3\xa9</text></svg>', True),
    ('plain inherited/default policy', 'text/plain', b'\xc3\xa9', True),
]

def observe(label, binary, digest, chrome, frame):
    if sha(binary)!=digest: raise RuntimeError('Executable identity changed')
    directory=OUT/label; directory.mkdir(); profile=directory/'profile'; profile.mkdir()
    nonce=secrets.token_hex(16); event=threading.Event(); reports=[]; requests=[]
    public=[{'name':r['name']} for r in frame.CASES]+[{'name':r[0]} for r in MIMES]
    core=r'''const cases=CASES, rows=[];
const cp=s=>Array.from(s,c=>c.codePointAt(0));
rows.push({name:'Top-level unlabelled HTML',encoding:document.characterSet,text:document.getElementById('payload').textContent,language:navigator.language});
(async()=>{for(let i=0;i<cases.length;i++){
 const f=document.createElement('iframe');let timer,listener;
 try {const result=new Promise((resolve,reject)=>{listener=e=>{if(e.source===f.contentWindow&&e.data&&(e.data.kind==='parent-result'||e.data.kind==='mime-result'))resolve(e.data);};window.addEventListener('message',listener);timer=setTimeout(()=>reject(Error('report timeout')),1800);});
 f.src=i<FRAMECOUNT?'/parent/'+i:'/mime-parent/'+(i-FRAMECOUNT);document.body.appendChild(f);
 const value=await result;rows.push({name:cases[i].name,parentEncoding:value.parentEncoding,encoding:value.encoding,text:value.text,error:value.error||null});
 }catch(e){rows.push({name:cases[i].name,error:e.name+': '+e.message});}
 finally{clearTimeout(timer);window.removeEventListener('message',listener);f.remove();}}
 fetch('/report',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({nonce:NONCE,rows,userAgent:navigator.userAgent})});
})().catch(e=>fetch('/report',{method:'POST',body:JSON.stringify({nonce:NONCE,error:e.name+': '+e.message,rows})}));'''.replace('CASES',json.dumps(public)).replace('FRAMECOUNT',str(len(frame.CASES))).replace('NONCE',json.dumps(nonce))
    page=b'<!doctype html><title>Owned HTML default policy diagnostic</title><p id=payload>\xc3\xa9</p><script>'+core.encode()+b'</script>'

    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*_): pass
        def do_GET(self):
            path=urlsplit(self.path).path; csp=False
            if path.startswith('/parent/'):
                i=int(path.rsplit('/',1)[1]); ct='text/html;charset='+frame.CASES[i]['parent']; data=frame.parent_bytes(i,servers[1].server_port)
            elif path.startswith('/child/'):
                i=int(path.rsplit('/',1)[1]); row=frame.CASES[i]; ct='text/html'+(';charset='+row['header'] if row.get('header') else ''); data=frame.child_bytes(i); csp=row.get('csp',False)
            elif path.startswith('/mime-parent/'):
                i=int(path.rsplit('/',1)[1]); ct='text/html;charset=windows-1251'
                selector="d.getElementsByTagName('root')[0].textContent" if i in (7,8) else "d.getElementsByTagName('text')[0].textContent" if i==9 else "(d.body?d.body.textContent:d.documentElement.textContent)" if MIMES[i][3] else "d.getElementById('payload').textContent"
                script=r'''const f=document.createElement('iframe');f.onload=()=>{try{const d=f.contentDocument;parent.postMessage({kind:'mime-result',parentEncoding:document.characterSet,encoding:d.characterSet,text:TEXTVALUE},'*');}catch(e){parent.postMessage({kind:'mime-result',error:e.name+': '+e.message},'*');}};f.src=URL;document.body.appendChild(f);'''.replace('TEXTVALUE',selector).replace('URL',json.dumps('/mime-child/'+str(i)))
                data=('<!doctype html><body><script>'+script+'</script>').encode()
            elif path.startswith('/mime-child/'):
                i=int(path.rsplit('/',1)[1]); _,ct,data,_=MIMES[i]
            else: ct,data='text/html',page
            requests.append({'path':path,'contentType':ct,'sha256':hashlib.sha256(data).hexdigest()})
            self.send_response(200); self.send_header('Content-Type',ct); self.send_header('Content-Length',str(len(data))); self.send_header('Cache-Control','no-store')
            if csp: self.send_header('Content-Security-Policy','sandbox allow-scripts')
            self.end_headers(); self.wfile.write(data)
        def do_POST(self):
            size=int(self.headers.get('Content-Length','0'))
            if urlsplit(self.path).path!='/report' or not 0<size<100000: self.send_error(400); return
            value=json.loads(self.rfile.read(size))
            if value.pop('nonce',None)!=nonce: self.send_error(403); return
            reports.append(value);self.send_response(204);self.end_headers();event.set()
    servers=[ThreadingHTTPServer(('127.0.0.1',0),Handler) for _ in range(2)]; threads=[]
    for server in servers:
        t=threading.Thread(target=server.serve_forever,daemon=True);t.start();threads.append(t)
    env={k:v for k,v in os.environ.items() if not k.startswith('LUMEN_')};env.update(LUMEN_PROFILE_DIR=str(profile),LUMEN_CONSOLE='1',LUMEN_WINDOW='900x700')
    url='http://127.0.0.1:'+str(servers[0].server_port)+'/'
    command=[str(binary),url]
    if chrome:command=[str(binary),'--headless=new','--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-component-update','--disable-sync','--user-data-dir='+str(profile),url]
    began=time.monotonic();log=(directory/'browser.log').open('x');mlog=(directory/'monitor.log').open('x');app=monitor=None
    row={'browser':label,'binarySHA256':digest,'startedUTC':utc(),'frameFixtureSHA256':sha(FRAME_FIXTURE),'complete':False}
    try:
        app=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL,start_new_session=True)
        prefix=directory/'resources';monitor=subprocess.Popen([sys.executable,'tools/monitor_process.py','--pid',str(app.pid),'--duration','70','--settle','0','--output',str(prefix)],stdout=mlog,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL,start_new_session=True)
        if not event.wait(58):raise RuntimeError('Fresh observation page did not report')
        time.sleep(max(0,1.3-(time.monotonic()-began)))
        if len(reports)!=1 or len(reports[0].get('rows',[]))!=1+len(public) or reports[0].get('error'):raise RuntimeError('Observation report incomplete')
        row['actual']=reports[0];row['complete']=True
    except Exception as error:row['error']=type(error).__name__+': '+str(error)
    finally:
        if app is not None:
            try: row['ownedBrowserExitCode']=stop_owned(app);row['ownedProcessGroupGone']=True
            except Exception as error:row['ownedCleanupError']=str(error);row['complete']=False
        if monitor is not None:
            try:row['monitorExitCode']=monitor.wait(timeout=6)
            except subprocess.TimeoutExpired:row['monitorForcedStop']=True;stop_owned(monitor)
        log.close();mlog.close()
        for server in servers:server.shutdown();server.server_close()
        for thread in threads:thread.join(timeout=2)
        (directory/'requests.json').write_text(json.dumps(requests,indent=2)+'\n')
        p=directory/'resources.json'
        if p.is_file():
            meta=json.loads(p.read_text());row['resources']={'path':str(p.relative_to(ROOT)),'sha256':sha(p),'samples':meta.get('samples'),'collectorError':meta.get('collector_error')}
            if meta.get('collector_error') or not meta.get('samples'):row['complete']=False
        else:row['complete']=False;row['resourceError']='Missing one-second samples'
        row.update(finishedUTC=utc(),wallSeconds=round(time.monotonic()-began,3),binarySHA256After=sha(binary))
        row['complete']=row['complete'] and row['binarySHA256After']==digest
        (directory/'receipt.json').write_text(json.dumps(row,indent=2)+'\n')
    return row

def main():
    if sha(FRAME_FIXTURE)!=FRAME_SHA:raise RuntimeError('Frozen frame fixture changed')
    if sha(ROOT/'tools/monitor_process.py')!='e31aa4fc949b149ac9c0e8c6170ce72ec3def971dbab4d6b026635f262d9cd79':raise RuntimeError('Reviewed process monitor changed')
    build=json.loads(BUILD.read_text());native=build['artifacts']['lumen']
    if not build.get('passed') or native['sha256']!=NATIVE_SHA:raise RuntimeError('Frozen native artifact changed')
    OUT.mkdir(parents=True,exist_ok=False);frame=module('frozen_frame_policy_fixture',FRAME_FIXTURE)
    browsers=[observe('lume',ROOT/native['path'],NATIVE_SHA,False,frame),observe('qualified-chrome',CHROME,CHROME_SHA,True,frame)]
    complete=all(r['complete'] for r in browsers);comparison=[]
    if complete:
        for a,b in zip(browsers[0]['actual']['rows'],browsers[1]['actual']['rows']):
            if a['name']!=b['name']:raise RuntimeError('Observation inputs differ')
            comparison.append({'name':a['name'],'same':a==b,'lume':a,'chrome':b})
    receipt={'complete':complete,'browsers':browsers,'comparisons':comparison,'scope':'Observed default-encoding policy only. Unchanged frozen frame assertions remain separately recorded; this probe has no conformance score.','sources':['https://html.spec.whatwg.org/multipage/parsing.html#determining-the-character-encoding']}
    (OUT/'summary.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'complete':complete,'receipt':str((OUT/'summary.json').relative_to(ROOT)),'same':sum(r['same'] for r in comparison),'total':len(comparison),'comparisons':comparison},indent=2));return 0 if complete else 1

if __name__=='__main__':raise SystemExit(main())
