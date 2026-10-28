#!/usr/bin/env python3
import argparse,json,os,pathlib,subprocess,sys,time
from paired_wpt import monitor,endmonitor
p=argparse.ArgumentParser();p.add_argument('--urls-file',required=True);p.add_argument('--output',required=True);p.add_argument('--jobs',default='4');a=p.parse_args()
root=pathlib.Path('/mnt/p3/project/browser/wpt');out=pathlib.Path(a.output).resolve();out.mkdir(exist_ok=True,parents=True);mirror=out/'wpt-root';mirror.mkdir(exist_ok=True)
# Read-only links to exact same checked-out upstream corpus; our own config avoids disrupting existing server.
for item in root.iterdir():
 if item.name in ('config.json','.git','_venv3'):continue
 dst=mirror/item.name
 if not dst.exists() and not dst.is_symlink():dst.symlink_to(item,target_is_directory=item.is_dir())
ports={'http':[18000,18001],'http-local':[18002],'http-public':[18003],'https':[18443,18444],'https-local':[18445],'https-public':[18446],'ws':[18888],'wss':[18889],'h2':[19000],'webtransport-h3':[19001],'dns':[18053]}
json.dump({'ports':ports,'browser_host':'web-platform.test','server_host':'127.0.0.1','bind_address':False},open(mirror/'config.json','w'),indent=2)
urls=json.load(open(a.urls_file));include=out/'include.txt';include.write_text('\n'.join(urls)+'\n')
cmd=[str(root/'_venv3/bin/python'),str(root/'wpt'),'run','--yes','--headless','--no-install-fonts','--no-manifest-update','--manifest','/mnt/p3/project/browser/wpt-data/MANIFEST.json','--tests',str(mirror),'--test-types','testharness','--binary','/home/a/.local/archpkg/pacman/usr/bin/chromium','--webdriver-binary','/home/a/.local/archpkg/pacman/usr/bin/chromedriver','--processes',a.jobs,'--no-pause-after-test','--no-restart-on-unexpected','--retry-unexpected','0','--include-file',str(include),'--log-wptreport',str(out/'report.json'),'--log-raw',str(out/'raw.jsonl'),'--binary-arg=--no-proxy-server','chrome']
meta={'started_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'command':cmd,'urls':urls,'ports':ports};json.dump(meta,open(out/'metadata.json','w'),indent=2)
with (out/'run.log').open('wb') as f:
 q=subprocess.Popen(cmd,cwd=root,stdout=f,stderr=f);m=monitor(q.pid,out/'resources','official Chromium WPT process tree');code=q.wait();endmonitor(m)
meta['exit_code']=code;meta['finished_utc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime());json.dump(meta,open(out/'metadata.json','w'),indent=2);print(json.dumps({'exit':code,'output':str(out)}),flush=True)
