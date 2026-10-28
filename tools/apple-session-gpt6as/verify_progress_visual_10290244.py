"""Verify the actual public progress view in isolated native Chrome via existing CDP."""
import argparse, base64, datetime, hashlib, importlib.util, json, pathlib, sys, time, urllib.request

ROOT=pathlib.Path.cwd();BASE=ROOT/'output/session-10290244'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--chromium',type=pathlib.Path,required=True)
parser.add_argument('--origin',help='Defaults to the verified task live origin')
parser.add_argument('--output',type=pathlib.Path,default=BASE/'progress-visual-qa')
args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
def sha(path):return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
def read(path):return json.loads(pathlib.Path(path).read_text())
def need(value,message):
    if not value:raise RuntimeError(message)
def utc():return datetime.datetime.now(datetime.timezone.utc).isoformat()
origin=args.origin or read(BASE/'progress-state/server-receipt.json')['publicURL']
need(origin.startswith('https://')and '#'not in origin,'Use the public HTTPS base origin only')
spec=importlib.util.spec_from_file_location('lume_visual_cdp',ROOT/'tests/wpt/full_runner.py')
runner=importlib.util.module_from_spec(spec);sys.modules[spec.name]=runner;spec.loader.exec_module(runner)
receipt={'schema':1,'startedUTC':utc(),'publicOrigin':origin,'method':'Native Chrome and existing full_runner CDP; fresh isolated profile, no extensions or user accounts.',
         'chromiumSHA256':sha(args.chromium),'runnerSHA256':sha(ROOT/'tests/wpt/full_runner.py'),'passed':False,'views':[]}
began=time.monotonic();browser=None
try:
    browser=runner.Chromium(args.chromium,args.output/'chrome',[],headless=True)
    receipt['chromeVersion']=browser.version
    for label,width,height,mobile in [('desktop',1280,1100,False),('phone',390,844,True)]:
        context=browser.browser.call('Target.createBrowserContext')['browserContextId'];page=None
        try:
            target=browser.browser.call('Target.createTarget',{'url':'about:blank','browserContextId':context})['targetId']
            with urllib.request.urlopen(f'http://127.0.0.1:{browser.port}/json/list',timeout=10)as response:targets=json.load(response)
            page=runner.CDP(next(value['webSocketDebuggerUrl']for value in targets if value['id']==target))
            page.call('Page.enable');page.call('Runtime.enable');page.call('Log.enable')
            page.call('Emulation.setDeviceMetricsOverride',{'width':width,'height':height,'deviceScaleFactor':1,'mobile':mobile})
            page.call('Emulation.setTouchEmulationEnabled',{'enabled':mobile})
            navigation=page.call('Page.navigate',{'url':origin})
            need(not navigation.get('errorText'),'Public page navigation failed')
            def evaluate(expression):
                value=page.call('Runtime.evaluate',{'expression':expression,'returnByValue':True,'awaitPromise':True},timeout=20)
                need(not value.get('exceptionDetails'),'Progress browser evaluation failed')
                return value['result'].get('value')
            deadline=time.monotonic()+25
            while True:
                ready=evaluate("({ready:document.readyState,live:document.getElementById('connection')?.textContent,cpu:document.getElementById('cpu')?.textContent,latency:document.getElementById('latency')?.textContent,rows:document.querySelectorAll('#expanded-rows tr').length})")
                if ready.get('live','').startswith('Live')and ready.get('cpu')not in(None,'—','—%')and ready.get('latency')not in(None,'—')and ready['rows']==7:break
                need(time.monotonic()<deadline,'Progress view did not become live with seven category rows')
                time.sleep(.2)
            inspected=evaluate("""(async()=>{
              const text=id=>document.getElementById(id).textContent;
              const first=text('updated');await new Promise(resolve=>setTimeout(resolve,2600));
              const second=text('updated'),snapshot=await fetch((await fetch('/config.json').then(r=>r.json())).liveBase+'/api/progress').then(r=>r.json());
              const score=document.getElementById('score').getBoundingClientRect();
              return {title:document.title,bodyText:document.body.innerText,wptRate:text('wpt-rate'),wptCount:text('wpt-count'),scope:text('wpt-scope'),phase:text('phase'),score:text('score'),artifact:text('artifact'),firstHostSample:first,secondHostSample:second,cpu:text('cpu'),ram:text('ram'),latency:text('latency'),categoryRows:[...document.querySelectorAll('#expanded-rows tr')].map(row=>[...row.children].map(cell=>cell.textContent)),expandedSummary:text('expanded-summary'),compatibility:[...document.querySelectorAll('#compatibility .fact')].map(row=>row.textContent),scoreBox:{left:score.left,top:score.top,right:score.right,bottom:score.bottom},viewport:{width:innerWidth,height:innerHeight},documentWidth:document.documentElement.scrollWidth,snapshot:{wpt:snapshot.status.wpt,expanded:snapshot.status.expandedWpt,iteration:snapshot.status.iteration,score:snapshot.scorecard.score}};
            })()""")
            need(inspected['wptRate']=='100.00%'and '1,850 / 1,850'in inspected['wptCount'],'Original subset counts are stale or absent')
            need(inspected['snapshot']['wpt']['binarySha256'][:20]in inspected['artifact'],'Visible native identity differs from live status')
            need(f"Iteration {inspected['snapshot']['iteration']}"in inspected['score']and f"{inspected['snapshot']['score']:.2f}/100"in inspected['score'],'Iteration or task score does not match the scorecard')
            need('NaN'not in inspected['bodyText']and 'undefined'not in inspected['bodyText'],'Unrendered or nonfinite UI data')
            need(inspected['firstHostSample']!=inspected['secondHostSample'],'Visible live host sample did not advance')
            need(inspected['latency'].endswith(' ms')and inspected['cpu'].endswith('%')and inspected['ram'].endswith(' GB'),'Live metric units are absent')
            need('full official WPT'in inspected['scope']or 'Full official WPT'in inspected['scope'],'Selected-suite limit is not visible')
            need(inspected['documentWidth']<=width+1,'Page overflows horizontally beyond the viewport')
            box=inspected['scoreBox'];need(0<=box['left']<box['right']<=width and 0<=box['top']<box['bottom']<=height,'Fixed iteration badge is outside the viewport')
            need(width-box['right']<=25 and height-box['bottom']<=25,'Iteration badge is not at the bottom right')
            order=['dom','events','url','headers','microtasks','storage','encoding']
            for row,name in zip(inspected['categoryRows'],order):
                data=inspected['snapshot']['expanded']['categories'][name]
                need(row[1]==str(data['scheduled'])and row[2]==f"{data['lumenRecorded']} / {data['lumenFullyPassing']}"and row[3]==f"{data['chromeRecorded']} / {data['chromeFullyPassing']}",'Visible category denominator or file outcome differs: '+name)
            if inspected['snapshot']['expanded']['isCurrentAcceptedNativeArtifact']is False:need('earlier native artifact'in inspected['expandedSummary'],'Historical cohort lacks a visible artifact label')
            exceptions=[item for item in page.events if item.get('method')=='Runtime.exceptionThrown']
            need(not exceptions,'Page JavaScript raised an exception')
            size=page.call('Page.getLayoutMetrics')['cssContentSize']
            prefix=args.output/('gpt6-astra-pro_release-integrator_builtin-subagents_devinmac3d_lume-progress-'+label)
            for kind,params in [('viewport',{}),('full',{'captureBeyondViewport':True,'clip':{'x':0,'y':0,'width':width,'height':min(size['height'],7000),'scale':1}})]:
                encoded=page.call('Page.captureScreenshot',{'format':'png',**params})['data']
                image=pathlib.Path(str(prefix)+'-'+kind+'.png');image.write_bytes(base64.b64decode(encoded))
                receipt.setdefault('screenshots',[]).append({'path':str(image.relative_to(ROOT)),'sha256':sha(image),'bytes':image.stat().st_size})
            inspected.pop('bodyText');receipt['views'].append({'label':label,'passed':True,**inspected})
        finally:
            if page:page.close()
            browser.browser.call('Target.disposeBrowserContext',{'browserContextId':context})
    receipt['passed']=True
except Exception as error:receipt['error']=type(error).__name__+': '+str(error)
finally:
    if browser:
        receipt['cleanup']=browser.close()
        if receipt['cleanup'].get('errors')or receipt['cleanup'].get('process',{}).get('error'):receipt['passed']=False
    receipt['finishedUTC']=utc();receipt['wallSeconds']=round(time.monotonic()-began,3)
    (args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({'passed':receipt['passed'],'error':receipt.get('error'),'wallSeconds':receipt['wallSeconds'],'receipt':str(args.output/'receipt.json'),'screenshots':receipt.get('screenshots',[])},indent=2))
raise SystemExit(0 if receipt['passed']else 1)
