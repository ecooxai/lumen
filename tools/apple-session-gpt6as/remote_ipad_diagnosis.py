"""Bounded, read-only diagnosis of exactly the existing task iPad and project artifacts."""
import hashlib,json,os,pathlib,plistlib,re,subprocess
R=pathlib.Path.cwd().resolve();D=pathlib.Path('/Applications/Xcode-27.0-RC.app/Contents/Developer');B='/Library/Developer/PrivateFrameworks/CoreSimulator.framework/Versions/A/Resources/bin/simctl';U='3DD498EB-4464-4ED7-9CD3-7667889DD1F6';N='GPT6AstraPro-ChatGPT-devinmac3d-Lume-iPad';env=dict(os.environ,DEVELOPER_DIR=str(D));out={}
def command(args,timeout=10,limit=1600):
 try:
  p=subprocess.run(args,env=env,capture_output=True,timeout=timeout);return {'code':p.returncode,'stdout':p.stdout.decode(errors='replace')[:limit],'stderr':p.stderr.decode(errors='replace')[-limit:]}
 except subprocess.TimeoutExpired as e:
  text=e.stdout or b'';text=text.decode(errors='replace')if isinstance(text,bytes)else text
  return {'timeout':timeout,'stdout':text[-limit:]}
def digest(p):
 h=hashlib.sha256()
 with p.open('rb')as f:
  for part in iter(lambda:f.read(1048576),b''):h.update(part)
 return h.hexdigest()
p=subprocess.run([B,'list','devices','available','--json'],env=env,capture_output=True,text=True,timeout=10);data=json.loads(p.stdout);matches=[(rid,d)for rid,ds in data['devices'].items()for d in ds if d.get('udid')==U]
if len(matches)!=1 or matches[0][1].get('name')!=N:raise RuntimeError('Task iPad identity changed')
rid,device=matches[0];out['device']={k:device.get(k)for k in ['udid','name','state','isAvailable']}
p=subprocess.run([B,'list','runtimes','--json'],env=env,capture_output=True,text=True,timeout=10);runtime=next(r for r in json.loads(p.stdout)['runtimes']if r['identifier']==rid);root=pathlib.Path(runtime['runtimeRoot'])
out['spawnHelpVariants']={'runtimePATH':command([B,'spawn',U,'launchctl','help']),'absoluteRuntimeRoot':command([B,'spawn',U,str(root/'bin/launchctl'),'help'])}
out['bootStatus']=command([B,'bootstatus',U],timeout=8,limit=2400)
out['runtimeLaunchctl']={'file':command(['file',str(root/'bin/launchctl')]),'codeSignature':command(['codesign','--verify','--strict',str(root/'bin/launchctl')])}
for name,path in [('runtime',pathlib.Path(runtime['bundlePath'])/'Contents/Info.plist'),('coreSimulator',pathlib.Path('/Library/Developer/PrivateFrameworks/CoreSimulator.framework/Versions/A/Resources/Info.plist')),('xcode',D.parent/'Info.plist')]:
 try:
  value=plistlib.loads(path.read_bytes());out[name+'Version']={k:value[k]for k in ['CFBundleIdentifier','CFBundleVersion','CFBundleShortVersionString','DTXcode','DTXcodeBuild','MinimumCoreSimulatorVersion','SupportedArchitectures']if k in value}
 except Exception as e:out[name+'Version']={'error':type(e).__name__+': '+str(e)}
def app_info(app):
 try:
  v=plistlib.loads((app/'Info.plist').read_bytes());exe=(app/v['CFBundleExecutable']).resolve();exe.relative_to(app.resolve());r={'app':str(app),'bundle':{k:v.get(k)for k in ['CFBundleIdentifier','CFBundleExecutable','CFBundleSupportedPlatforms','DTPlatformName','UIDeviceFamily','MinimumOSVersion']},'binaryBytes':exe.stat().st_size,'binarySHA256':digest(exe),'file':command(['file',str(exe)])}
  result=command(['otool','-l',str(exe)],limit=100000);lines=result.get('stdout','').splitlines();r['platformLoadCommands']=['\n'.join(lines[i:i+6])for i,line in enumerate(lines)if 'LC_BUILD_VERSION'in line or 'LC_VERSION_MIN'in line];return r
 except Exception as e:return {'app':str(app),'error':type(e).__name__+': '+str(e)}
data_path=pathlib.Path(device['dataPath']);
if U not in data_path.parts:raise RuntimeError('Device data root escaped identity')
apps=[];container_root=data_path/'Containers/Bundle/Application'
if container_root.is_dir():
 containers=list(container_root.iterdir());out['bundleContainersObserved']=len(containers);out['bundleContainerScanTruncated']=len(containers)>300
 for container in containers[:300]:
  if not container.is_dir()or container.is_symlink():continue
  for app in container.glob('*.app'):
   try:v=plistlib.loads((app/'Info.plist').read_bytes())
   except Exception:continue
   if any('lume'in str(v.get(k,'')).lower()for k in ['CFBundleIdentifier','CFBundleName','CFBundleDisplayName']):apps.append(app_info(app))
out['installedTaskLumenAppBundles']=apps
artifacts=[];walked=0
for base in ['out','build','build-apple-dom-20261029','artifacts','dist','ios','build-deps','packages']:
 start=R/base
 if not start.is_dir():continue
 for current,dirs,files in os.walk(start):
  walked+=1
  if walked>2000:dirs[:]=[];break
  path=pathlib.Path(current);depth=len(path.relative_to(start).parts)
  dirs[:]=[n for n in dirs if n not in ['.git','node_modules','target','registry','profile','profiles','src','include']and depth<5]
  for name in list(dirs):
   p=path/name
   if name.endswith('.app'):
    artifacts.append(app_info(p));dirs.remove(name)
   elif name.endswith('.framework')and'v8'in name.lower():artifacts.append({'framework':str(p)});dirs.remove(name)
  for name in files:
   if name.endswith('.ipa')or(name.startswith('libv8')and name.endswith(('.a','.dylib'))):
    p=path/name
    if p.is_symlink():continue
    artifacts.append({'path':str(p),'bytes':p.stat().st_size,'sha256':digest(p),'file':command(['file',str(p)])})
  if len(artifacts)>30:dirs[:]=[];break
out['projectAppleArtifacts']=artifacts[:30];out['boundedDirectoriesVisited']=walked
logroot=pathlib.Path(device['logPath']);out['deviceStartupLogs']=[]
if U not in logroot.parts:raise RuntimeError('Device log root escaped identity')
if logroot.is_dir():
 for p in sorted(logroot.rglob('*'))[:40]:
  if not p.is_file()or p.is_symlink():continue
  row={'path':str(p),'bytes':p.stat().st_size}
  if p.suffix in ['.log','.txt']or'log'in p.name.lower():
   with p.open('rb')as f:f.seek(max(0,p.stat().st_size-16384));tail=f.read().decode(errors='replace')
   lines=[line for line in tail.splitlines()if re.search(r'error|fail|missing|dyld|launchd|CoreSimulator|assert|boot|Program|exec',line,re.I)]
   row['filteredTail']=['[sensitive-looking diagnostic line omitted]'if re.search(r'password|secret|authorization|cookie|token',line,re.I)else line for line in lines[-35:]]
  out['deviceStartupLogs'].append(row)
print(json.dumps(out,indent=2))
