#!/usr/bin/env python3
"""Exact codec fixture in a scoped Chrome diagnostic context; no general reference claim."""
import argparse, datetime, hashlib, http.server, importlib.util, json, signal, sys, threading, time, urllib.request
from pathlib import Path
ROOT=Path('/Users/devin/repos/lumen')
FIXTURE_SHA='5ff54ee9929df32e9922d3af45224ce568fe72345451aefc527c05244a187050'
CHROME_SHA='4f71e9dbe725fec1110977b6a6dd0708fb32fbde9c905b4b461cef880ac08288'
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path); obj=importlib.util.module_from_spec(spec); sys.modules[name]=obj; spec.loader.exec_module(obj); return obj

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--label',default='text-codec-chrome-diagnostic'); args=parser.parse_args()
    out=ROOT/'output/session-10290244'/args.label; out.mkdir(exist_ok=False,parents=True)
    fixture_path=ROOT/'tests/test_text_codec.py'; assert hashlib.sha256(fixture_path.read_bytes()).hexdigest()==FIXTURE_SHA
    fixture=module('text_codec_diagnostic_fixture',fixture_path); runner=module('text_codec_diagnostic_runner',ROOT/'tests/wpt/full_runner.py')
    binary=ROOT/'output/wpt-10290244/reference/chrome-4f71e9dbe725fec1-mac-arm64-10290244/Google Chrome.app/Contents/MacOS/Google Chrome'
    assert hashlib.sha256(binary.read_bytes()).hexdigest()==CHROME_SHA
    receipt={'started_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'reference_role':'narrow_diagnostic_only_general_reference_quarantined','binary':str(binary),'binary_sha256':CHROME_SHA,'fixture_source_sha256':FIXTURE_SHA,'fixture_sha256':hashlib.sha256(fixture.HTML.encode()).hexdigest(),'worker_sha256':hashlib.sha256(fixture.WORKER.encode()).hexdigest(),'issues':[]}
    begin=time.monotonic(); server=http.server.ThreadingHTTPServer(('127.0.0.1',0),fixture.Handler); thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
    browser=page=context=None
    try:
        browser=runner.Chromium(binary,out/'chrome',[],headless=True)
        receipt.update(version=browser.version,pid=browser.process.pid,argv=browser.process.args)
        context=browser.browser.call('Target.createBrowserContext')['browserContextId']
        target=browser.browser.call('Target.createTarget',{'url':'about:blank','browserContextId':context})['targetId']
        with urllib.request.urlopen('http://127.0.0.1:%d/json/list'%browser.port,timeout=5) as response: tabs=json.load(response)
        info=next(t for t in tabs if t['id']==target)
        page=runner.CDP(info['webSocketDebuggerUrl']); page.call('Runtime.enable'); page.call('Page.enable')
        url='http://127.0.0.1:%d/'%server.server_port; page.call('Page.navigate',{'url':url})
        deadline=time.monotonic()+14; result=None
        while time.monotonic()<deadline:
            raw=page.call('Runtime.evaluate',{'expression':'window.__textCodecResult || null','returnByValue':True},timeout=5)
            if raw.get('exceptionDetails'): raise RuntimeError('fixture extraction raised: '+str(raw['exceptionDetails']))
            result=raw.get('result',{}).get('value')
            if result is not None: break
            time.sleep(.05)
        receipt['fixture_result']=result
        if result is None: receipt['issues'].append('fixture timeout')
        else:
            rows=result.get('rows',[]); summary=result.get('result',{}); passed=sum(r.get('ok') is True for r in rows)
            complete=len(rows)==170 and [r.get('i') for r in rows]==list(range(170)) and summary.get('total')==170 and summary.get('passed')==passed and summary.get('workerIssue') is None
            receipt.update(complete=complete,total=len(rows),passed=passed,failed=len(rows)-passed)
            if not complete: receipt['issues'].append('incomplete or inconsistent fixture results')
        expr=r'''(()=>{const rows=[];for(const [encoding,first,next] of [['utf-8',[255,65],[66]],['utf-8',[226,65],[66]],['utf-16le',[0,220,65,0],[66,0]]]){const d=new TextDecoder(encoding,{fatal:true});let initial=null,later=null;try{initial={value:d.decode(new Uint8Array(first),{stream:true})};}catch(e){initial={error:e.name};}try{later={value:d.decode(new Uint8Array(next))};}catch(e){later={error:e.name};}rows.push({encoding,first,next,initial,later});}return {crossOriginIsolated,sharedArrayBuffer:typeof SharedArrayBuffer,rows};})()'''
        receipt['fatal_stream_diagnostic']=page.call('Runtime.evaluate',{'expression':expr,'returnByValue':True},timeout=5)
    except BaseException as exc:
        receipt['issues'].append(type(exc).__name__+': '+str(exc))
    finally:
        if page:
            try: page.close()
            except Exception as exc: receipt['issues'].append('page cleanup: '+str(exc))
        if browser and context:
            try: browser.browser.call('Target.disposeBrowserContext',{'browserContextId':context})
            except Exception as exc: receipt['issues'].append('context cleanup: '+str(exc))
        if browser:
            try:
                cleanup=browser.close(); receipt['browser_cleanup']=cleanup
                if cleanup.get('errors'): receipt['issues'].extend('browser cleanup: '+str(e) for e in cleanup['errors'])
            except Exception as exc: receipt['issues'].append('browser cleanup: '+str(exc))
        server.shutdown(); server.server_close(); thread.join(timeout=2)
    receipt['ended_at']=datetime.datetime.now(datetime.timezone.utc).isoformat(); receipt['duration_seconds']=time.monotonic()-begin
    receipt['fixture_assertions_passed']=receipt.get('complete',False) and receipt.get('passed')==170 and not receipt['issues']
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('TEXT_CODEC_CHROME_RECEIPT '+json.dumps({k:receipt.get(k) for k in ['started_at','ended_at','duration_seconds','total','passed','failed','issues','fixture_assertions_passed']}))
    return 0 if receipt['fixture_assertions_passed'] else 1

def terminate(_signum,_frame): raise KeyboardInterrupt('termination requested')
if __name__=='__main__':
    signal.signal(signal.SIGTERM,terminate)
    raise SystemExit(main())
