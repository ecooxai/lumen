"""Run the unchanged body handler fixture in the qualified official Chrome reference."""
from pathlib import Path
import datetime, hashlib, http.server, importlib.util, json, socket, sys, threading, time, urllib.request
ROOT=Path.cwd();OUT=ROOT/'output/session-102900614/body-window-handlers-chrome-1';OUT.mkdir(parents=True,exist_ok=False)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);sys.modules[name]=m;spec.loader.exec_module(m);return m
fixture_path=ROOT/'tests/test_body_window_handlers.py'
if sha(fixture_path)!='04bbd4c2d5044b0d402f4c6354d98e6fab1cc70f94ccfa0d7c4963da4f8948e9':raise RuntimeError('Frozen45 fixture differs')
fixture=module('body_alias_fixture',fixture_path);runner=module('body_alias_reference_runner',ROOT/'tests/wpt/full_runner.py')
trust=json.loads((ROOT/'output/session-102900614/chrome-official-reference-v2/receipt.json').read_text());binary=Path(trust['binary'])
if not trust.get('readyForSanityProbe') or not trust.get('payloadUnchanged') or not trust.get('quarantineAndOtherAttributesUnchanged') or sha(binary)!='0a8cb6eba2bee365c01fb0612f64f1d15855fd499dd54625ced1fe50215e5fb4':raise RuntimeError('Qualified reference provenance differs')
for step in ('copied-deep-strict-final','copied-publisher-final','copied-gatekeeper-final'):
    if next(row for row in trust['steps']if row['name']==step)['exitCode']!=0:raise RuntimeError('Reference trust check failed')
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),fixture.Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
started=time.monotonic();receipt={'startedUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),'fixtureSHA256':sha(fixture_path),'binarySHA256':sha(binary),'runnerSHA256':sha(ROOT/'tests/wpt/full_runner.py'),'expectedChecks':fixture.EXPECTED,'rows':[],'console':[],'passed':False,'primarySource':'https://html.spec.whatwg.org/multipage/webappapis.html#event-handler-content-attributes','scope':'Unchanged HTTP fixture; no browser APIs, expectations, CSP or test code modified.'}
browser=None;page=None;context=None
try:
    browser=runner.Chromium(str(binary),OUT/'reference',[],headless=False);receipt['browserVersion']=browser.version
    context=browser.browser.call('Target.createBrowserContext')['browserContextId'];target=browser.browser.call('Target.createTarget',{'url':'about:blank','browserContextId':context})['targetId']
    with urllib.request.urlopen('http://127.0.0.1:'+str(browser.port)+'/json/list',timeout=8) as response:targets=json.load(response)
    info=next(row for row in targets if row['id']==target);page=runner.CDP(info['webSocketDebuggerUrl']);page.call('Page.enable');page.call('Runtime.enable')
    page.call('Page.navigate',{'url':'http://127.0.0.1:%d/'%server.server_port});deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        try:event=page.event(min(1,max(.01,deadline-time.monotonic())))
        except socket.timeout:continue
        if event.get('method')!='Runtime.consoleAPICalled':continue
        value=' '.join(str(item.get('value',item.get('description',''))) for item in event.get('params',{}).get('args',[]));receipt['console'].append(value)
        if value.startswith('BODY_WINDOW_CHECK '):receipt['rows'].append(json.loads(value.split(' ',1)[1]))
        if value.startswith('BODY_WINDOW_RESULTS '):receipt['results']=json.loads(value.split(' ',1)[1]);break
    rows=receipt['rows'];passed=sum(row.get('ok')is True for row in rows)
    receipt['passed']=len(rows)==fixture.EXPECTED and [row['i']for row in rows]==list(range(fixture.EXPECTED)) and receipt.get('results')=={'total':fixture.EXPECTED,'passed':passed,'failed':fixture.EXPECTED-passed} and passed==fixture.EXPECTED
except Exception as exc:receipt['error']=type(exc).__name__+': '+str(exc)
finally:
    if page:page.close()
    if browser and context:
        try:browser.browser.call('Target.disposeBrowserContext',{'browserContextId':context})
        except Exception as exc:receipt['contextCleanupError']=type(exc).__name__+': '+str(exc)
    if browser:receipt['cleanup']=browser.close()
    server.shutdown();server.server_close();thread.join(timeout=2)
receipt['durationSeconds']=round(time.monotonic()-started,3);receipt['finishedUTC']=datetime.datetime.now(datetime.timezone.utc).isoformat()
(OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({'passed':receipt['passed'],'rows':len(receipt['rows']),'failures':[row for row in receipt['rows']if not row['ok']],'error':receipt.get('error'),'cleanup':receipt.get('cleanup'),'durationSeconds':receipt['durationSeconds'],'receipt':str((OUT/'receipt.json').relative_to(ROOT))},indent=2))
raise SystemExit(0 if receipt['passed'] and not receipt.get('error') and not receipt.get('contextCleanupError') and not receipt.get('cleanup',{}).get('errors') else 1)
