"""Four bounded native graphics runs: presented frames and separate cleanup."""
import hashlib,json,os,pathlib,shutil,signal,subprocess,sys,tarfile,urllib.request
from datetime import datetime,timezone
ROOT=pathlib.Path.cwd().resolve();OUT=ROOT/'output/session-20261029/native-gui-kit-v3-install';KIT=OUT/'kit'
URL='https://f6c8d1446c510279.j6oksipj7wog.opentunnel.xyz/gpt6-astra-pro_web-demos_builtin-subagents_native-gui-kit-v3.tar.gz';ARCHIVE_SHA='4debce2b3edd5264906f165adf46023af3cbf62d1b909e0936594dc3cc03ba4f'
BINARY=ROOT/'build-apple-load-20261029/lumen-load-2cceed5f71132bd0';BINARY_SHA='2cceed5f71132bd093b5e14501be284499b500dfa343274e76ab49f02dedcebd'
EXPECTED_FILES={'scripts/qa-apple-gpt6/telemetry.py': '8b03ab0aab566080ee56b6ec66b1a964c5fd62b6b822a4be7a16e12c7aad9b07', 'tools/apple-session-gpt6as/probe_existing_release.py': 'c785f2bc0842d3c85ae577d3c49dcc2338c958d56074773ff9c33635e4160e9a', 'tools/apple-web-lab-gpt6as/monitor.mjs': 'b6f16a59fbb2268f4fc9c27fa6ac398868d38055b9b632495aba8f299645c105', 'tools/apple-web-lab-gpt6as/public/auth-bundle.js': 'd22f1a76a4183fcf64d74448ec6e51e3922c146000aac3659fe3486b75a9dd19', 'tools/apple-web-lab-gpt6as/public/auth.html': '7c7fb9dea16441aff5f8701ffe4c4a5a454f695e8961d1de492da87b8155b687', 'tools/apple-web-lab-gpt6as/public/auth.js': '4be39c9509d82a6ecc2b605acc817621dcf5b508035e890c408dddb77db9e41c', 'tools/apple-web-lab-gpt6as/public/controller.js': '5b7efbc50170a5d5e1b6309cacd16b8b6ca24adffdae646cfe31cdf8f2c37c3a', 'tools/apple-web-lab-gpt6as/public/demos.js': '7c6b9f5354b95b88fdaa9b008644850f2b884633b693d0d8b2d583b8576a5778', 'tools/apple-web-lab-gpt6as/public/fixture.json': 'ee433f34ca898bcd3f0c3d6a27ecfb228d03ef57688fc13df54d777be9f3f227', 'tools/apple-web-lab-gpt6as/public/frame-bundle.js': 'c7b31647c59cf7f14adb2882c9c8f339ac17d28ad3d826ca22540a8204c43b35', 'tools/apple-web-lab-gpt6as/public/frame.html': '31651b5d998239441689706c16cf9ba92ac0d3cbc4f23920a02087b663450504', 'tools/apple-web-lab-gpt6as/public/frame.js': 'd44976d1784545ec93a6f0ffcf442b28c818af768bcfb4c3ae93ada183aefd7d', 'tools/apple-web-lab-gpt6as/public/index.html': '6ea6a4f5610fc244a213c70f8bb9516c73efde8c099dfe9e23f4e1d29f66b9c3', 'tools/apple-web-lab-gpt6as/public/iteration.json': '8c507d3f296babb57fde29210f61019227ecd0d62f85a1cf72bb785d558fc5cd', 'tools/apple-web-lab-gpt6as/public/lab.js': '8208791712e20be532d035bc201cea0a033edc6b51683f73c881838dc31f0cc0', 'tools/apple-web-lab-gpt6as/public/model.js': '3d32438bf6b97df6a96e135944bd60a3c7eabfaeba3f04f7c31a780c1c961359', 'tools/apple-web-lab-gpt6as/public/monitor.js': '7bc01182cea2cbb1cf4242159c8c2ade719592bd9b03839b103cad11e34894af', 'tools/apple-web-lab-gpt6as/public/rtc-core.js': 'e17643b53a5e59e5ba32f601dea23b46ea84d99942bbc5645472c9907de5e8ab', 'tools/apple-web-lab-gpt6as/public/rtc-peer-bundle.js': '4dd71d4eb4a381a7f37c15a3d86f534191e6ac5cdf4fc33ff495d4f135d9d7b7', 'tools/apple-web-lab-gpt6as/public/rtc-peer.css': 'f5f4ed1c77cca07ecb55c25e3295990012d18bf9eaec91930f23329d5a13e156', 'tools/apple-web-lab-gpt6as/public/rtc-peer.html': '98842c3a0d22c2d86db2eb6c5d1cac883db799ded141e4dc036563e6ddf9c02d', 'tools/apple-web-lab-gpt6as/public/rtc-peer.js': 'a69eff6d58b85973e288ee32fe12dcac1ffdde15d5f975866ea28b416f79f671', 'tools/apple-web-lab-gpt6as/public/rtc-room.html': 'ba2a6c57401b3419df02e9e8b31543e1aa00e241cd0ea5b8a746d74705c9434d', 'tools/apple-web-lab-gpt6as/public/rtc-room.js': '0ca64484cc1648f6309a27858005b5f77cc8edac7d1d9ab23e3663290f58cf46', 'tools/apple-web-lab-gpt6as/public/style.css': 'd198787e0f4201592d0148d4b9ee112197a348dfdf4dae2ba9f467f91c328af0', 'tools/apple-web-lab-gpt6as/public/view.js': 'aa459e4b3e07a59228fedfa6801cfab6024e92ba5d5034fe0c1eddd78fa89345', 'tools/apple-web-lab-gpt6as/public/worker.js': '7e44f0f411497750db4b626b2eeeb69a3b1ddd2fbe4044e08117ad5bff38bf3c', 'tools/apple-web-lab-gpt6as/rtc-broker.mjs': 'a36785f2b9adc8b0193d9eae4ce6d9d2e9da452226adfc4643d9af5023e9229d', 'tools/apple-web-lab-gpt6as/server.mjs': 'f312be0ea4835a47c0225ce770779c091bea7d95692a594be533d6533d5cdff6'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def utc():return datetime.now(timezone.utc).isoformat()
def save(p,d):tmp=p.with_suffix('.tmp');tmp.write_text(json.dumps(d,indent=2)+'\n');tmp.replace(p)
if '--worker'not in sys.argv:
 if sha(BINARY)!=BINARY_SHA:raise RuntimeError('Selected immutable lifecycle browser binary changed')
 OUT.mkdir(parents=True,exist_ok=False);archive=OUT/'reviewed-kit.tar.gz'
 with urllib.request.urlopen(URL,timeout=25)as response:data=response.read(1024*1024)
 if len(data)!=83933 or hashlib.sha256(data).hexdigest()!=ARCHIVE_SHA:raise RuntimeError('Reviewed GUI kit download mismatch')
 archive.write_bytes(data);KIT.mkdir()
 with tarfile.open(archive)as t:
  members=t.getmembers();names=[m.name for m in members]
  if len(names)!=len(set(names))or set(names)!=set(EXPECTED_FILES):raise RuntimeError('GUI kit file inventory differs')
  for m in members:
   n=pathlib.PurePosixPath(m.name)
   if not m.isfile()or n.is_absolute()or'..'in n.parts:raise RuntimeError('Unsafe GUI kit member')
   body=t.extractfile(m).read()
   if hashlib.sha256(body).hexdigest()!=EXPECTED_FILES[m.name]:raise RuntimeError('GUI kit source checksum mismatch')
   dest=KIT/m.name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(body)
 copied=KIT/'build/lumen-lifecycle';copied.parent.mkdir();shutil.copy2(BINARY,copied);copied.chmod(0o555)
 if sha(copied)!=BINARY_SHA:raise RuntimeError('Immutable GUI binary copy mismatch')
 with(OUT/'job.log').open('x')as log:child=subprocess.Popen([sys.executable,str(pathlib.Path(__file__).resolve()),'--worker'],stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL,start_new_session=True)
 print(json.dumps({'workerPID':child.pid,'output':str(OUT.relative_to(ROOT)),'kitSHA256':ARCHIVE_SHA,'nativeBinarySHA256':BINARY_SHA,'runs':'Explicit WebGL/WebGPU: one retained480x640frame and one separate cleanup run each,15seconds per run'}));raise SystemExit(0)
receipt={'startedUTC':utc(),'binarySHA256':BINARY_SHA,'kitSHA256':ARCHIVE_SHA,'scope':'Actual immutable lifecycle/transport-enabled MacLumen app; explicit GPU backend and480x640presentation, separate cleanup. RTC1/WT1 enabled; individual API results retained; not allAPI/IPA/iPad proof.','runs':[],'issues':[]}
for backend in ['webgl','webgpu']:
 for retain in [True,False]:
  label=backend+('-presented'if retain else'-cleanup');output='output/'+label
  args=[sys.executable,'tools/apple-session-gpt6as/probe_existing_release.py','--binary','build/lumen-lifecycle','--expected-sha256',BINARY_SHA,'--output',output,'--seconds','15','--backend',backend,'--focus-graphics','--graphics-size','480x640']
  if retain:args.append('--show-graphics')
  row={'label':label,'startedUTC':utc(),'command':args};receipt['runs'].append(row);save(OUT/'combined-receipt.json',receipt)
  try:
   with(OUT/(label+'.log')).open('x')as log:
    proc=subprocess.Popen(args,cwd=KIT,stdout=log,stderr=subprocess.STDOUT,stdin=subprocess.DEVNULL,start_new_session=True)
    try:row['runnerExitCode']=proc.wait(timeout=55)
    except subprocess.TimeoutExpired:
     row['runnerTimedOut']=True;os.killpg(proc.pid,signal.SIGTERM)
     try:row['runnerExitCode']=proc.wait(timeout=15)
     except subprocess.TimeoutExpired:
      os.killpg(proc.pid,signal.SIGKILL);row['runnerExitCode']=proc.wait(timeout=3);row['forcedRunnerKill']=True;receipt['issues'].append(label+': runner failed graceful cleanup; inspect ownedtargets')
   file=KIT/output/'probe-receipt.json';value=json.loads(file.read_text());row.update(receipt=str(file.relative_to(ROOT)),receiptSHA256=sha(file),functionalPass=value.get('functionalPass'),functionalChecks=value.get('functionalChecks'),issues=value.get('issues'),screenshot=value.get('screenshot'),binarySHA256=value.get('sha256'))
  except Exception as error:row['error']=type(error).__name__+': '+str(error);receipt['issues'].append(label+': '+row['error'])
  row['finishedUTC']=utc();save(OUT/'combined-receipt.json',receipt)
receipt['finishedUTC']=utc();receipt['binarySHA256After']=sha(BINARY);save(OUT/'combined-receipt.json',receipt);print(json.dumps(receipt))
