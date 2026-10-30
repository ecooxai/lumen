#!/usr/bin/env python3
"""Reuse the strict external peer gate for Chrome versus actual iOS Simulator."""
import argparse,hashlib,json,os,subprocess,uuid
from pathlib import Path
from rtc_interop_case import assess,final_peer
from simulator_idle_case import DEVICES

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--device',choices=DEVICES,required=True);args=ap.parse_args()
    out=Path('out/verification');port=(out/'lab-server-port.txt').read_text().strip()
    if not port.isdecimal() or not 1024<=int(port)<=65535:raise SystemExit('Invalid task-owned lab port')
    room='lume-sim-'+args.device+'-'+uuid.uuid4().hex;tag=room[-12:];base='http://127.0.0.1:'+port+'/rtc-peer.html?room='+room+'&autostart=1&role='
    names={'chrome':'rtc-'+args.device+'-chrome-'+tag,'lumen':'rtc-'+args.device+'-lumen-'+tag}
    commands={'chrome':['python3','verification/native_case.py','--engine','chrome','--binary','/Applications/Google Chrome.app/Contents/MacOS/Google Chrome','--name',names['chrome'],'--kind','rtc','--url',base+'offer','--duration','35','--settle','23'],
        'lumen':['python3','verification/simulator_browser_case.py','--device',args.device,'--name',names['lumen'],'--url',base+'answer','--duration','35']}
    env={key:value for key,value in os.environ.items() if key!='LUMEN_EXPERIMENTAL_RTC_LOOPBACK'};jobs={};handles=[]
    for engine,command in commands.items():
        handle=(out/(names[engine]+'.driver.log')).open('w');handles.append(handle);jobs[engine]=subprocess.Popen(command,stdout=handle,stderr=subprocess.STDOUT,env=env)
    exits={engine:job.wait(timeout=50) for engine,job in jobs.items()}
    for handle in handles:handle.close()
    peers={};processes={}
    for engine,name in names.items():
        peers[engine]=final_peer(out/(name+'.log'))
        try:processes[engine]=json.loads((out/(name+'.json')).read_text())
        except (OSError,ValueError):processes[engine]={}
    assessment=assess(peers,processes,room)
    if processes.get('lumen',{}).get('installedMatchesCurrentBuildSim') is not True:
        assessment['failedGates'].append('simulator-installed-binary-not-verified');assessment['passed']=False;assessment['crossBrowserInteroperability']=False
    result={'schemaVersion':1,'device':DEVICES[args.device][1],'room':room,'assessment':assessment,'driverExitCodes':exits,'peerPageSha256':hashlib.sha256(Path('tools/web-capability-lab/rtc-peer.mjs').read_bytes()).hexdigest(),'peerCoreSha256':hashlib.sha256(Path('tools/web-capability-lab/core.mjs').read_bytes()).hexdigest(),'peers':peers,'processes':{engine:{key:value for key,value in data.items() if key!='samples'} for engine,data in processes.items()},'physicalDeviceVerified':False,'sameIPAOnMacVerified':False}
    target=out/('rtc-simulator-'+args.device+'-'+tag+'.json');target.write_text(json.dumps(result,indent=2));(out/('rtc-simulator-'+args.device+'-latest.json')).write_text(json.dumps({'result':str(target)},indent=2));print(json.dumps(result,indent=2))
    return 0 if assessment['passed'] and all(value==0 for value in exits.values()) else 1

if __name__=='__main__':raise SystemExit(main())
