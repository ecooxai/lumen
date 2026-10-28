"""Independently probe signed Chrome via dump-DOM, without the WPT/CDP runner."""
from pathlib import Path
from html.parser import HTMLParser
import datetime as dt, hashlib, json, os, signal, subprocess, sys, time
ROOT=Path.cwd();OUT=ROOT/'output/session-102900614/chrome-independent-diagnostic-1';OUT.mkdir(exist_ok=False)
NATIVE='0a8cb6eba2bee365c01fb0612f64f1d15855fd499dd54625ced1fe50215e5fb4'
binary=ROOT/'output/session-102900614/chrome-official-reference-v2/Google Chrome.app/Contents/MacOS/Google Chrome'
if hashlib.sha256(binary.read_bytes()).hexdigest()!=NATIVE:raise RuntimeError('Frozen trusted binary changed')
fixture=OUT/'url-native.html'
fixture.write_text('''<!doctype html><meta charset="utf-8"><title>Native URL independent probe</title><pre id="results"></pre><script>
(()=>{const checks=[];const check=(name,expected,fn)=>{let actual;try{actual=fn()}catch(e){actual='throws:'+e.name}checks.push({name,expected,actual,passed:actual===expected})};
check('space hostname','throws:TypeError',()=>new URL('http://example example.com','http://other.com/').href);
check('file localhost','file:///',()=>new URL('file://localhost/').href);
check('file ignored port','file:///',()=>{let x=new URL('file:///');x.port='12';return x.href});
check('file rejected host port','file://y/',()=>{let x=new URL('file://y/');x.host='x:123';return x.href});
check('invalid anchor literal','http://example example.com',()=>{let a=document.createElement('a');a.href='http://example example.com';return a.href});
check('anchor file localhost','file:///',()=>{let a=document.createElement('a');a.href='file://localhost/';return a.href});
document.getElementById('results').textContent=JSON.stringify({location:location.href,userAgent:navigator.userAgent,nativeURL:Function.prototype.toString.call(URL),checks});})();
</script>''')
class Extract(HTMLParser):
    def __init__(self):super().__init__(convert_charrefs=True);self.inside=False;self.parts=[]
    def handle_starttag(self,tag,attrs):
        if tag=='pre'and dict(attrs).get('id')=='results':self.inside=True
    def handle_endtag(self,tag):
        if tag=='pre':self.inside=False
    def handle_data(self,data):
        if self.inside:self.parts.append(data)
receipt={'startedUTC':dt.datetime.now(dt.timezone.utc).isoformat(),'binarySHA256':NATIVE,'fixtureSHA256':hashlib.sha256(fixture.read_bytes()).hexdigest(),'scope':'Independent direct signed Chrome dump-DOM transport; no CDP/WPT module or URL shim. Fresh task profiles. This does not earn conformance/parity.','environmentVariableNamesRelevantToLoaderOrChrome':sorted(name for name in os.environ if name.startswith(('DYLD_','LD_','CHROME_','CHROMIUM_'))),'runs':[]}
# Names only: do not read private profile preferences or disclose policy values.
try:
    import plistlib
    policy=Path('/Library/Managed Preferences/com.google.Chrome.plist')
    if policy.is_file()and not policy.is_symlink():
        with policy.open('rb')as handle:managed=plistlib.load(handle)
        receipt['managedChromePolicyNames']=sorted(managed)if isinstance(managed,dict)else[]
    else:receipt['managedChromePolicyNames']=[]
except Exception as error:receipt['managedPolicyReadError']=type(error).__name__
for mode in ('inherited-environment','minimal-environment'):
    profile=OUT/(mode+'-profile');profile.mkdir()
    command=[str(binary),'--headless=new','--no-first-run','--no-default-browser-check','--disable-background-networking','--user-data-dir='+str(profile),'--dump-dom','--timeout=5000',fixture.as_uri()]
    environment=dict(os.environ)
    if mode=='minimal-environment':environment={name:value for name,value in os.environ.items()if name in ('PATH','HOME','USER','LOGNAME','TMPDIR','LANG','SHELL','__CF_USER_TEXT_ENCODING')or name.startswith('LC_')}
    began=time.monotonic();started=dt.datetime.now(dt.timezone.utc).isoformat();logpath=OUT/(mode+'.stderr.log')
    with logpath.open('wb')as log:
        process=subprocess.Popen(command,cwd=ROOT,env=environment,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=log,start_new_session=True)
        forced=False
        try:stdout,_=process.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            forced=True;os.killpg(process.pid,signal.SIGTERM)
            try:stdout,_=process.communicate(timeout=4)
            except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);stdout,_=process.communicate(timeout=3)
    def group_alive():
        try:os.killpg(process.pid,0);return True
        except ProcessLookupError:return False
    drain_end=time.monotonic()+5
    while group_alive()and time.monotonic()<drain_end:time.sleep(.1)
    group_gone=not group_alive()
    if not group_gone:
        forced=True;os.killpg(process.pid,signal.SIGTERM)
        cleanup_end=time.monotonic()+2
        while group_alive()and time.monotonic()<cleanup_end:time.sleep(.1)
        if group_alive():os.killpg(process.pid,signal.SIGKILL)
        group_gone=not group_alive()
    raw=OUT/(mode+'.dom.html');raw.write_bytes(stdout)
    parser=Extract();parser.feed(stdout.decode(errors='replace'))
    result=None;error=None
    try:result=json.loads(''.join(parser.parts))
    except Exception as caught:error=type(caught).__name__
    receipt['runs'].append({'mode':mode,'startedUTC':started,'finishedUTC':dt.datetime.now(dt.timezone.utc).isoformat(),'durationSeconds':round(time.monotonic()-began,3),'exitCode':process.returncode,'forced':forced,'ownedPID':process.pid,'ownedProcessGroupGone':group_gone,'environmentVariableNames':sorted(environment),'result':result,'parseError':error,'domSHA256':hashlib.sha256(stdout).hexdigest(),'stderrSHA256':hashlib.sha256(logpath.read_bytes()).hexdigest()})
    (OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
receipt['finishedUTC']=dt.datetime.now(dt.timezone.utc).isoformat();receipt['binarySHA256After']=hashlib.sha256(binary.read_bytes()).hexdigest()
receipt['bothNativeSanityPassed']=all(row['exitCode']==0 and not row['forced']and row['ownedProcessGroupGone']and row['result']and all(check['passed']for check in row['result']['checks'])for row in receipt['runs'])
(OUT/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2));raise SystemExit(0 if receipt['bothNativeSanityPassed']else 1)
