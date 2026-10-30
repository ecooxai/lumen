#!/usr/bin/env python3
"""Bounded normal-network retry of the observed unavailable detector endpoint."""
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import time
import urllib.parse
import urllib.request

out=Path('out/verification');url=None
for line in (out/'diagnose-html5-native.log').read_text(errors='replace').splitlines():
    if 'LUME_HTML5_ERROR ' not in line:continue
    try:candidate=json.loads(line.split('LUME_HTML5_ERROR ',1)[1]).get('resource','')
    except ValueError:continue
    parsed=urllib.parse.urlsplit(candidate)
    if parsed.scheme=='https' and parsed.netloc=='api.whichbrowser.net' and parsed.path=='/rel/detect.js':url=candidate;break
if not url:raise SystemExit('No observed detector URL available')
start=time.monotonic()
with (out/'detector-health.jsonl').open('a',buffering=1) as log:
    for attempt in range(1,21):
        if attempt>1:time.sleep(max(0,start+(attempt-1)*30-time.monotonic()))
        entry={'attempt':attempt,'maxAttempts':20,'time':dt.datetime.now(dt.timezone.utc).isoformat(),'normalNetworkAndTLS':True}
        try:
            request=urllib.request.Request(url,headers={'User-Agent':'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36','Referer':'https://html5test.co/'})
            with urllib.request.urlopen(request,timeout=8) as response:
                body=response.read(300000);status=response.status;content_type=response.headers.get('Content-Type','')
            recovered=status==200 and bool(re.search(rb'\b(?:function\s+WhichBrowser|WhichBrowser\s*=)',body))
            entry.update(status=status,bytes=len(body),contentType=content_type,detectorSourceReturned=recovered)
            if recovered:
                (out/'whichbrowser-recovered.js').write_bytes(body)
                entry['sha256']=hashlib.sha256(body).hexdigest()
                (out/'detector-recovered.json').write_text(json.dumps(entry,indent=2)+'\n')
        except Exception as error:
            entry.update(detectorSourceReturned=False,errorType=type(error).__name__,message=str(error)[:180])
        log.write(json.dumps(entry)+'\n')
        if entry['detectorSourceReturned']:break
