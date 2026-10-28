"""Run an unchanged authored fixture in isolated installed Chromium."""
import argparse,functools,hashlib,http.server,json,os,pathlib,socket,subprocess,sys,tempfile,threading,time
ROOT=pathlib.Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'agent/validation'))
from paired_wpt import request,stop,monitor,endmonitor
from performance.compare import processes,start_identity,matched_process,cleanup_owned

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--fixture',required=True,type=pathlib.Path);ap.add_argument('--marker',required=True);ap.add_argument('--output',required=True,type=pathlib.Path);ap.add_argument('--timeout',type=float,default=35);a=ap.parse_args()
    fixture=a.fixture.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);before=hashlib.sha256(fixture.read_bytes()).hexdigest();binary=pathlib.Path('/home/a/.local/archpkg/pacman/usr/bin/chromium').resolve();driver='/home/a/.local/archpkg/pacman/usr/bin/chromedriver'
    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self,*args):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(fixture.parent)));thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start();url='http://127.0.0.1:%d/%s'%(server.server_port,fixture.name)
    tmp=ROOT/'agent/t';tmp.mkdir(exist_ok=True);profile_owner=tempfile.TemporaryDirectory(prefix='chromium-fixture-',dir=tmp);profile=profile_owner.name
    sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close();endpoint='http://127.0.0.1:'+str(port);log=(out/'driver.log').open('wb');p=subprocess.Popen([driver,'--port='+str(port),'--allowed-ips=127.0.0.1'],stdout=log,stderr=log,stdin=subprocess.DEVNULL);identity={'pid':p.pid,'start_ticks':start_identity(p.pid)};mon=monitor(p.pid,out/'resources','owned Chromium authored-fixture tree');sid=None;owned=[];started=time.monotonic();result={'status':'HARNESS_ERROR','fixture':str(fixture),'fixture_sha256':before,'binary':str(binary),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'url':url}
    try:
        for _ in range(100):
            try:request(endpoint,'/status');break
            except Exception:time.sleep(.05)
        value=request(endpoint,'/session',{'capabilities':{'alwaysMatch':{'browserName':'chrome','pageLoadStrategy':'none','goog:chromeOptions':{'binary':str(binary),'args':['--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-component-update','--disable-sync','--no-proxy-server','--window-size=800,640','--user-data-dir='+profile]}}}});sid=value['sessionId'];prefix='/session/'+sid;result['capabilities']=value['capabilities']
        injection='''(()=>{const messages=[];Object.defineProperty(window,"__astraFixtureLog",{value:messages});const original=console.log;console.log=function(){let line="";for(let i=0;i<arguments.length;i++)line+=(i?" ":"")+String(arguments[i]);messages.push(line);return original.apply(console,arguments);};})();'''
        request(endpoint,prefix+'/goog/cdp/execute',{'cmd':'Page.addScriptToEvaluateOnNewDocument','params':{'source':injection}});request(endpoint,prefix+'/goog/cdp/execute',{'cmd':'Emulation.setDeviceMetricsOverride','params':{'width':748,'height':596,'deviceScaleFactor':1,'mobile':False}});request(endpoint,prefix+'/url',{'url':url})
        while time.monotonic()-started<a.timeout:
            if matched_process(identity):owned.extend(processes(p.pid))
            logs=request(endpoint,prefix+'/execute/sync',{'script':'return window.__astraFixtureLog || []','args':[]});markers=[s for s in logs if s.startswith('RESULTS '+a.marker+':')]
            if markers:
                result['console']=logs;result['marker']=markers[-1];result['status']='PASS' if markers[-1].startswith('RESULTS '+a.marker+':PASS') else 'FAIL';break
            time.sleep(.1)
        else:result['status']='TIMEOUT'
        if time.monotonic()-started<1.2:time.sleep(1.2-(time.monotonic()-started))
    except Exception as error:result['error']=repr(error)
    finally:
        if matched_process(identity):owned.extend(processes(p.pid))
        if sid:
            try:request(endpoint,'/session/'+sid,method='DELETE')
            except Exception as error:result['driver_cleanup_error']=repr(error)
        stop(p);endmonitor(mon);log.close();result['owned_process_cleanup']=cleanup_owned(owned);profile_owner.cleanup();result['profile_removed']=not pathlib.Path(profile).exists();server.shutdown();server.server_close();thread.join(2);result['elapsed_s']=time.monotonic()-started;result['fixture_unchanged']=before==hashlib.sha256(fixture.read_bytes()).hexdigest()
        if result['owned_process_cleanup']['remaining_same_identity'] or not result['profile_removed'] or not result['fixture_unchanged']:result['status']='HARNESS_ERROR'
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result.get(k) for k in ['status','marker','elapsed_s','profile_removed','fixture_unchanged']}));return 0 if result['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
