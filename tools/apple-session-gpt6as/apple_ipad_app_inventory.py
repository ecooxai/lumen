"""Read only the task iPad's Lume-named installed app bundles."""
from pathlib import Path
import hashlib,json,plistlib,subprocess,time
from datetime import datetime,timezone
root=Path('/Users/devin/repos/lumen')
if Path.cwd().resolve()!=root:raise RuntimeError('Unexpected workspace')
out=root/'output/session-10290244/apple-ipad-app-filesystem-inventory';out.mkdir(exist_ok=False)
device=Path('/Users/devin/Library/Developer/CoreSimulator/Devices/3DD498EB-4464-4ED7-9CD3-7667889DD1F6')
base=device/'data/Containers/Bundle/Application'
receipt={'startedUTC':datetime.now(timezone.utc).isoformat(),'deviceUUID':device.name,'scope':'Lume/Lumen-named bundles in the explicitly task-owned simulator only','bundleContainerExists':base.exists(),'matches':[],'errors':[]}
if base.exists():
 for container in sorted(base.iterdir()):
  if not container.is_dir():continue
  for app in sorted(container.iterdir()):
   if not app.is_dir() or app.suffix.lower()!='.app' or not any(x in app.stem.lower() for x in ('lume','lumen')):continue
   info=app/'Info.plist'
   try:
    data=plistlib.loads(info.read_bytes());bundle=data.get('CFBundleIdentifier','')
    row={'path':str(app),'infoSHA256':hashlib.sha256(info.read_bytes()).hexdigest(),'bundleIdentifier':bundle,'displayName':data.get('CFBundleDisplayName'),'name':data.get('CFBundleName'),'executable':data.get('CFBundleExecutable'),'minimumOSVersion':data.get('MinimumOSVersion'),'supportedPlatforms':data.get('CFBundleSupportedPlatforms'),'deviceFamily':data.get('UIDeviceFamily')}
    executable=app/(data.get('CFBundleExecutable') or '')
    if executable.is_file() and executable.parent.resolve()==app.resolve():
     with executable.open('rb') as f:row['executableSHA256']=hashlib.file_digest(f,'sha256').hexdigest()
     row['executableBytes']=executable.stat().st_size
     p=subprocess.run(['/usr/bin/file',str(executable)],capture_output=True,text=True,timeout=8);row['fileOutput']=p.stdout.strip();row['fileExitCode']=p.returncode
     tool=Path('/Library/Developer/CommandLineTools/usr/bin/otool')
     if tool.exists():
      p=subprocess.run([str(tool),'-l',str(executable)],capture_output=True,text=True,timeout=8)
      (out/(bundle.replace('/','_')+'-load-commands.txt')).write_text(p.stdout+p.stderr)
      row['loadCommandsExitCode']=p.returncode
    receipt['matches'].append(row)
   except Exception as e:receipt['errors'].append({'appPath':str(app),'error':str(e)})
receipt['finishedUTC']=datetime.now(timezone.utc).isoformat()
(out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
