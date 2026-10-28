"""Independent fresh about:blank URL/anchor sanity probe on the frozen Chrome app."""
import argparse,datetime,hashlib,importlib.util,json,pathlib,re,subprocess,sys,time,urllib.request
ROOT=pathlib.Path.cwd();parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--chromium',type=pathlib.Path,required=True)
parser.add_argument('--expected-sha256',default='4f71e9dbe725fec1110977b6a6dd0708fb32fbde9c905b4b461cef880ac08288')
parser.add_argument('--output',type=pathlib.Path,default=ROOT/'output/session-10290244/chrome-native-url-sanity')
args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=False)
EXPECTED=args.expected_sha256
if not re.fullmatch('[a-f0-9]{64}',EXPECTED):raise RuntimeError('Expected reference SHA256 must be explicit64hex')
def sha(path):return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
def utc():return datetime.datetime.now(datetime.timezone.utc).isoformat()
if sha(args.chromium)!=EXPECTED:raise RuntimeError('Frozen Chrome executable hash differs')
spec=importlib.util.spec_from_file_location('url_probe_cdp',ROOT/'tests/wpt/full_runner.py');runner=importlib.util.module_from_spec(spec);sys.modules[spec.name]=runner;spec.loader.exec_module(runner)
receipt={'schema':1,'startedUTC':utc(),'binarySHA256':EXPECTED,'binaryPath':str(args.chromium),'probeSourceSHA256':sha(pathlib.Path(__file__)),'runnerSHA256':sha(ROOT/'tests/wpt/full_runner.py'),'scope':'Fresh about:blank; no WPT fixture, URL shim, reporter, extension, account or user profile. Native URL and HTMLAnchorElement sanity only.','probeCompleted':False,'nativeSanityPassed':False}
browser=None;page=None;context=None;began=time.monotonic()
try:
 browser=runner.Chromium(args.chromium,args.output/'chrome',[],headless=True)
 receipt['browserVersion']=browser.version
 receipt['ownedPID']=browser.process.pid
 receipt['ownedProcessIdentity']=subprocess.check_output(['ps','-p',str(browser.process.pid),'-o','pid=,ppid=,lstart=,command='],text=True).strip()
 receipt['launchArguments']=browser.process.args
 context=browser.browser.call('Target.createBrowserContext')['browserContextId'];target=browser.browser.call('Target.createTarget',{'url':'about:blank','browserContextId':context})['targetId']
 with urllib.request.urlopen(f'http://127.0.0.1:{browser.port}/json/list',timeout=10)as response:targets=json.load(response)
 info=next(value for value in targets if value['id']==target);receipt['target']={key:info[key]for key in ('type','url','title')if key in info}
 page=runner.CDP(info['webSocketDebuggerUrl']);page.call('Runtime.enable');page.call('Page.enable')
 expression=r'''(()=>{
 const nativeSource=fn=>Function.prototype.toString.call(fn),out={location:location.href,userAgent:navigator.userAgent,platform:navigator.platform,functions:{URL:nativeSource(URL),URLSearchParams:nativeSource(URLSearchParams),createElement:nativeSource(Document.prototype.createElement),anchorHrefGetter:nativeSource(Object.getOwnPropertyDescriptor(HTMLAnchorElement.prototype,'href').get)},checks:[]};
 const record=(name,expected,operation)=>{let actual,error;try{actual=operation();}catch(value){error={name:value.name,message:value.message};actual='throws:'+value.name;}out.checks.push({name,expected,actual,error,passed:actual===expected});};
 record('URL rejects a space in hostname','throws:TypeError',()=>new URL('http://example example.com','http://other.com/').href);
 record('URL canonicalizes localhost file host','file:///',()=>new URL('file://localhost/').href);
 record('file URL ignores port mutation','file:///',()=>{const value=new URL('file://localhost/');value.port='12';return value.href;});
 record('file URL rejects host with port mutation','file://y/',()=>{const value=new URL('file://y/');value.host='x:123';return value.href;});
 record('URL replaces and encodes lone surrogate path','http://example.com/%EF%BF%BD',()=>new URL('http://example.com/'+String.fromCharCode(0xD800)).href);
 const anchor=()=>document.createElement('a');
 record('anchor rejects spaced hostname as a parsed host','',()=>{const a=anchor();a.href='http://example example.com';return a.hostname;});
 record('anchor retains invalid literal href','http://example example.com',()=>{const a=anchor();a.href='http://example example.com';return a.href;});
 record('anchor canonicalizes localhost file host','file:///',()=>{const a=anchor();a.href='file://localhost/';return a.href;});
 record('anchor file ignores port mutation','file:///',()=>{const a=anchor();a.href='file://localhost/';a.port='12';return a.href;});
 record('anchor file rejects host with port mutation','file://y/',()=>{const a=anchor();a.href='file://y/';a.host='x:123';return a.href;});
 const odd=String.fromCharCode(0xD800,0xD801,0xDFFE,0xDFFF,0xFDD0,0xFDCF,0xFDEF,0xFDF0,0xFFFE,0xFFFF),source='http://example.com/'+odd+'?'+odd;
 const repaired=String.fromCodePoint(0xFFFD,0x107FE,0xFFFD,0xFDD0,0xFDCF,0xFDEF,0xFDF0,0xFFFE,0xFFFF);
 const encoded='http://example.com/'+encodeURIComponent(repaired)+'?'+encodeURIComponent(repaired);
 record('URL exact upstream surrogate/noncharacter case',encoded,()=>new URL(source,'about:blank').href);
 record('anchor exact upstream surrogate/noncharacter case',encoded,()=>{const a=anchor();a.href=source;return a.href;});
 out.surrogateInputCodeUnits=Array.from({length:odd.length},(_,index)=>odd.charCodeAt(index).toString(16));
 return out;
})()'''
 evaluation=page.call('Runtime.evaluate',{'expression':expression,'returnByValue':True},timeout=15)
 if evaluation.get('exceptionDetails'):raise RuntimeError('Sanity probe script raised a JavaScript exception')
 receipt['result']=evaluation['result']['value'];receipt['probeCompleted']=True
 receipt['nativeSanityPassed']=all(value['passed']for value in receipt['result']['checks'])
 receipt['binarySHA256After']=sha(args.chromium)
 if receipt['binarySHA256After']!=EXPECTED:raise RuntimeError('Frozen executable changed during probe')
except Exception as error:receipt['error']=type(error).__name__+': '+str(error)
finally:
 if page:page.close()
 if browser:
  if context:browser.browser.call('Target.disposeBrowserContext',{'browserContextId':context})
  receipt['cleanup']=browser.close()
  if receipt['cleanup'].get('errors'):receipt['probeCompleted']=False
 receipt['finishedUTC']=utc();receipt['wallSeconds']=round(time.monotonic()-began,3)
 (args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
raise SystemExit(0 if receipt['probeCompleted']and receipt['nativeSanityPassed']else 1)
