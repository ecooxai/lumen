#!/usr/bin/env python3
"""Real HTTP bytes exercise HTML metadata prescanning and encoding precedence."""
import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading

CASES=[]
def add(name,prefix,body,encoding,text,content_type='text/html',bom=b'',query=None):
    page=bom+prefix.encode()+b'<p id="payload">'+body+b'</p><a id="query" href="?&#27700;">x</a>'
    CASES.append({'name':name,'bytes':page,'contentType':content_type,'encoding':encoding,'text':text,'query':query})

add('UTF8 meta baseline','<meta charset=utf-8>','é'.encode(),'UTF-8','é')
add('Windows1252 meta','<meta charset=windows-1252>',b'\x80','windows-1252','€')
add('Big5 metadata controls bytes and hyperlink query','<meta charset=big5>',b'\xa4\xf4','Big5','水',query='?%A4%F4')
add('GBK metadata controls bytes and hyperlink query','<meta charset=gbk>',b'\xcb\xae','GBK','水',query='?%CB%AE')
add('ISO2022JP metadata','<meta charset=iso-2022-jp>',bytes.fromhex('1b24423f651b2842'),'ISO-2022-JP','水')
add('pragma before content','<meta http-equiv=content-type content="text/html;charset=windows-1252">',b'\x80','windows-1252','€')
add('content before pragma','<meta content="charset=windows-1252" http-equiv=content-type>',b'\x80','windows-1252','€')
add('content without pragma does not select encoding','<meta content="text/html;charset=windows-1252">','é'.encode(),'UTF-8','é')
add('charset attribute overrides earlier content','<meta content="charset=big5" http-equiv=content-type charset=windows-1252>',b'\x80','windows-1252','€')
add('unknown charset prevents content fallback in same meta','<meta charset=bogus content="charset=windows-1252" http-equiv=content-type>','é'.encode(),'UTF-8','é')
add('first duplicate charset wins','<meta charset=windows-1252 charset=utf-8>',b'\x80','windows-1252','€')
add('duplicate content attribute is ignored','<meta content=bad content="charset=windows-1252" http-equiv=content-type>','é'.encode(),'UTF-8','é')
add('ASCII mixed-case metadata','<MeTa ChArSeT=WiNdOwS-1252>',b'\x80','windows-1252','€')
add('slash after meta tag name','<meta/charset=windows-1252>',b'\x80','windows-1252','€')
add('unknown meta allows later valid declaration','<meta charset=bogus><meta charset=windows-1252>',b'\x80','windows-1252','€')
add('comment metadata is skipped','<!-- <meta charset=big5> --><meta charset=windows-1252>',b'\x80','windows-1252','€')
add('metadata text inside another attribute is skipped','<div title="<meta charset=big5>"></div><meta charset=windows-1252>',b'\x80','windows-1252','€')
add('UTF16 meta maps to UTF8','<meta charset=utf-16le>','é'.encode(),'UTF-8','é')
add('x-user-defined meta maps to Windows1252','<meta charset=x-user-defined>',b'\x80','windows-1252','€')
add('transport charset overrides metadata','<meta charset=big5>',b'\x80','windows-1252','€','text/html;charset=windows-1252')
add('BOM overrides transport and metadata','<meta charset=big5>','é'.encode(),'UTF-8','é','text/html;charset=windows-1252',bom=b'\xef\xbb\xbf')
add('single-quoted charset in content extraction','<meta content="garbage; charset=\'windows-1252\'" http-equiv=content-type>',b'\x80','windows-1252','€')
add('content extraction resumes after charset without equals','<meta content="charset nope charset = windows-1252" http-equiv=content-type>',b'\x80','windows-1252','€')
add('XML encoding fallback in text/html','<?xml version="1.0" encoding="windows-1252"?>',b'\x80','windows-1252','€')
add('XML declaration is case sensitive','<?XML version="1.0" encoding="windows-1252"?>','é'.encode(),'UTF-8','é')
add('later HTML meta precedes XML encoding fallback','<?xml version="1.0" encoding="big5"?><meta charset=windows-1252>',b'\x80','windows-1252','€')
for label,codec in [('UTF-16LE','utf-16le'),('UTF-16BE','utf-16be')]:
    page='<?xml version="1.0"?><p id="payload">é</p>'
    CASES.append({'name':label+' XML prefix without BOM','bytes':page.encode(codec),'contentType':'text/html','encoding':label,'text':'é','query':None})
CASES.append({'name':'text/plain does not inspect HTML metadata','bytes':b'<meta charset=windows-1252>\xc3\xa9','contentType':'text/plain','encoding':'UTF-8','text':'<meta charset=windows-1252>é','query':None,'plain':True})
PUBLIC=[{key:value for key,value in case.items() if key not in ('bytes','contentType')} for case in CASES]
EXPECTED=len(CASES)
HTML=r'''<!doctype html><meta charset=utf-8><title>HTML metadata encoding</title><body><script>
'use strict';
const cases=__CASES__,rows=[];
const eq=(actual,expected)=>{if(actual!==expected)throw new Error('expected '+JSON.stringify(expected)+', got '+JSON.stringify(actual));};
(async()=>{for(let index=0;index<cases.length;++index){const row=cases[index],frame=document.createElement('iframe');let timer;
 try{await new Promise((resolve,reject)=>{timer=setTimeout(()=>reject(new Error('frame timeout')),1000);frame.onload=resolve;frame.onerror=()=>reject(new Error('frame load error'));frame.src='/case/'+index;document.body.appendChild(frame);});
 const doc=frame.contentDocument;eq(doc.characterSet,row.encoding);eq(row.plain?doc.body.textContent:doc.getElementById('payload').textContent,row.text);if(row.query!==null)eq(doc.getElementById('query').search,row.query);rows.push({i:index,name:row.name,ok:true});}
 catch(error){rows.push({i:index,name:row.name,ok:false,error:error.name+': '+error.message});}finally{clearTimeout(timer);frame.remove();}
 console.log('HTML_META_CHECK '+JSON.stringify(rows[rows.length-1]));}
 const passed=rows.filter(row=>row.ok).length;console.log('HTML_META_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed}));document.documentElement.setAttribute('data-html-meta-done','1');
})().catch(error=>console.log('HTML_META_INFRA '+error.name+': '+error.message));
</script>'''.replace('__CASES__',json.dumps(PUBLIC,ensure_ascii=True))

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        path=self.path.split('?',1)[0]
        if path.startswith('/case/'):
            index=int(path.removeprefix('/case/'));case=CASES[index];content_type,data=case['contentType'],case['bytes']
        else:content_type,data='text/html;charset=utf-8',HTML.encode()
        self.send_response(200);self.send_header('Content-Type',content_type);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    def log_message(self,*_):pass

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--binary',type=Path,required=True);args=parser.parse_args();binary=args.binary.resolve()
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='lume-html-meta-') as profile:
            env={k:v for k,v in os.environ.items() if not k.startswith('LUMEN_')};env.update(LUMEN_PROFILE_DIR=profile,LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-html-meta-done')
            run=subprocess.run([str(binary),'http://127.0.0.1:%d/'%server.server_port,'35000'],capture_output=True,text=True,env=env,timeout=40)
    except subprocess.TimeoutExpired as e:print('HTML_META_INFRA '+json.dumps({'error':'timeout','seconds':e.timeout}));return 2
    finally:server.shutdown();server.server_close();thread.join(timeout=2)
    print(run.stdout,end='');print(run.stderr,end='');rows=[];results=[]
    for line in (run.stdout+'\n'+run.stderr).splitlines():
        for marker,destination in [('HTML_META_CHECK ',rows),('HTML_META_RESULTS ',results)]:
            index=line.find(marker)
            if index>=0:destination.append(json.loads(line[index+len(marker):]))
    if run.returncode or len(rows)!=EXPECTED or len(results)!=1 or [row['i'] for row in rows]!=list(range(EXPECTED)):
        print('HTML_META_INFRA '+json.dumps({'exit':run.returncode,'checks':len(rows),'results':results}));return 2
    passed=sum(row['ok'] is True for row in rows)
    if results[0]!={'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed}:raise RuntimeError('Inconsistent fixture result')
    print('HTML_META_RECEIPT '+json.dumps({'binarySHA256':hashlib.sha256(binary.read_bytes()).hexdigest(),'fixtureSHA256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'htmlSHA256':hashlib.sha256(HTML.encode()).hexdigest(),'caseSHA256':[hashlib.sha256(case['bytes']).hexdigest() for case in CASES],'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed}))
    print('RESULTS html-meta-encoding %s (%d/%d checks)'%('PASS' if passed==EXPECTED else 'FAIL',passed,EXPECTED));return 0 if passed==EXPECTED else 1

if __name__=='__main__':raise SystemExit(main())
