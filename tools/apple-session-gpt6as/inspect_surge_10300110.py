"""One bounded existing Surge CLI/account/domain check; no install or login."""
from datetime import datetime, timezone
from pathlib import Path
import json
import os
import re
import shutil
import subprocess
import time
import urllib.request


def main():
    root=Path.cwd().resolve()
    if str(root)!='/Users/devin/repos/lumen':raise SystemExit('Use the observed canonical Mac repository')
    started=time.monotonic()
    out=root/'out/10300110-apple/surge-availability';out.mkdir(parents=True,exist_ok=False);out.chmod(0o700)
    state=root/'output/session-20261029-cpu30/progress-state'
    server=json.loads((state/'server-receipt.json').read_text())
    domains=[value for value in [server.get('surgeURL'),*server.get('allowedOrigins',[])]
             if isinstance(value,str) and re.fullmatch(r'https://[a-z0-9-]+\.surge\.sh',value)]
    if not domains:domains=['https://lume-102900614-gpt6-astra-pro.surge.sh']
    domain=domains[0]
    candidates=[root/f'output/session-{task}/surge-cli/node_modules/.bin/surge'
                for task in ('20261029-cpu30','102900614','10290244')]
    if shutil.which('surge'):candidates.append(Path(shutil.which('surge')))
    cli=next((path for path in candidates if path.is_file() and os.access(path,os.X_OK)),None)
    report={'startedUTC':datetime.now(timezone.utc).isoformat(),'scope':'Existing CLI/authentication/domain metadata only',
            'domain':domain,'existingCLI':str(cli) if cli else None,'installedOrLoggedIn':False,
            'credentialValuesOmitted':True,'existingTokenPresent':bool(os.environ.get('SURGE_TOKEN')),
            'existingAuthenticationVerified':False,'domainHTTPSReachable':False}
    if cli:
        for name,args,limit in [('version',['--version'],5),('authentication',['whoami'],15)]:
            try:
                result=subprocess.run([str(cli),*args],stdin=subprocess.DEVNULL,capture_output=True,text=True,timeout=limit)
                raw=result.stdout+result.stderr
                private=out/(name+'-private.log');private.write_text(raw);private.chmod(0o600)
                report[name+'ExitCode']=result.returncode
                if name=='version':
                    found=re.search(r'(?<![\w.])\d+\.\d+\.\d+(?:[-+][\w.-]+)?(?![\w.])',raw)
                    report['surgeVersion']=found.group(0) if found else None
                else:
                    authenticated=bool(re.search(r'[^\s@]+@[^\s@]+\.[^\s@]+',raw))
                    negative=bool(re.search(r'not logged|not authenticated|unauthorized|login required',raw,re.I))
                    report['existingAuthenticationVerified']=result.returncode==0 and authenticated and not negative
            except subprocess.TimeoutExpired:report[name+'Error']='Bounded '+str(limit)+' second command timeout'
            except OSError as error:report[name+'Error']=type(error).__name__
    try:
        request=urllib.request.Request(domain+'/',method='HEAD')
        with urllib.request.urlopen(request,timeout=8) as response:
            report['domainHTTPStatus']=response.status;report['domainHTTPSReachable']=response.status==200
        with urllib.request.urlopen(domain+'/config.json',timeout=8) as response:
            config=json.loads(response.read(65536))
        report['publishedLiveBase']=config.get('liveBase')
        report['publishedMessageSessionId']=config.get('messageSessionId')
        report['publishedLiveBaseMatches']=config.get('liveBase')==server['publicURL']
    except Exception as error:
        report['domainError']=type(error).__name__+': '+str(error)[:200]
    report.update(canReuseWithoutInstall=bool(cli) and report['existingAuthenticationVerified'],
                  finishedUTC=datetime.now(timezone.utc).isoformat(),durationSeconds=round(time.monotonic()-started,3))
    (out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
