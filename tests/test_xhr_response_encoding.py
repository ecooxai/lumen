#!/usr/bin/env python3
"""Observable XHR response decoding and data-URL byte preservation checks."""
import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading

RESOURCES = {
    'utf8': ('text/plain; charset=utf-8', bytes.fromhex('c3a9')),
    'latin': ('text/plain; charset=windows-1252', b'\x80'),
    'quoted': ('text/plain; Charset="windows-1252"', b'\x80'),
    'byte80': ('text/plain; charset=utf-8', b'\x80'),
    'duplicate-first-invalid': ('text/plain; charset=bogus; charset=windows-1252', b'\x80'),
    'duplicate-first-valid': ('text/plain; charset=windows-1252; charset=utf-8', b'\x80'),
    'bom-utf8': ('text/plain; charset=windows-1252', bytes.fromhex('efbbbf41')),
    'bom-utf8-double': ('text/plain; charset=utf-8', bytes.fromhex('efbbbfefbbbf41')),
    'bom-utf16le': ('text/plain; charset=windows-1252', bytes.fromhex('fffe4100ac20')),
    'bom-utf16be': ('text/plain; charset=utf-8', bytes.fromhex('feff0041')),
    'utf16le': ('text/plain; charset=utf-16le', bytes.fromhex('4100')),
    'utf16be': ('text/plain; charset=utf-16be', bytes.fromhex('0041')),
    'unsupported': ('text/plain; charset=utf-32', bytes.fromhex('f09f9880')),
    'replacement': ('text/plain; charset=iso-2022-kr', b'AB'),
    'empty-replacement': ('text/plain; charset=iso-2022-kr', b''),
    'iso2022': ('text/plain; charset=iso-2022-jp', bytes.fromhex('1b24423f651b2842')),
    'json-utf8': ('application/json; charset=windows-1252', '{"value":"é"}'.encode()),
    'json-utf16': ('application/json; charset=utf-16le', b'\xff\xfe'+'{"value":1}'.encode('utf-16le')),
}

HTML = r'''<!doctype html><meta charset=utf-8><title>XHR response encoding</title><script>
'use strict';
const rows=[];
const eq=(actual,expected)=>{if(actual!==expected)throw new Error('expected '+JSON.stringify(expected)+', got '+JSON.stringify(actual));};
const bytes=value=>Array.from(new Uint8Array(value)).join(',');
async function check(name,fn){let timer;try{await Promise.race([Promise.resolve().then(fn),new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error('check timeout')),1200);})]);rows.push({i:rows.length,name,ok:true});}catch(e){rows.push({i:rows.length,name,ok:false,error:e.name+': '+e.message});}finally{clearTimeout(timer);}console.log('XHR_ENCODING_CHECK '+JSON.stringify(rows[rows.length-1]));}
function request(url,options={}){return new Promise((resolve,reject)=>{const xhr=options.xhr||new XMLHttpRequest();try{if(options.before!==undefined)xhr.overrideMimeType(options.before);xhr.open('GET',url);if(options.type!==undefined)xhr.responseType=options.type;if(options.override!==undefined)xhr.overrideMimeType(options.override);xhr.timeout=1000;xhr.onload=()=>resolve(xhr);xhr.onerror=()=>reject(new Error('XHR network error'));xhr.ontimeout=()=>reject(new Error('XHR request timeout'));xhr.send();}catch(e){reject(e);}});}
const text=async(name,options)=> (await request('/bytes/'+name,options)).responseText;
(async()=>{
 await check('HTTP UTF8 baseline',async()=>eq(await text('utf8'),'é'));
 await check('HTTP Windows1252 charset',async()=>eq(await text('latin'),'€'));
 await check('HTTP quoted mixed-case charset parameter',async()=>eq(await text('quoted'),'€'));
 await check('override explicit charset wins',async()=>eq(await text('byte80',{override:'text/plain;charset=windows-1252'}),'€'));
 await check('override without charset retains response charset',async()=>eq(await text('latin',{override:'application/octet-stream'}),'€'));
 await check('override unknown charset falls back to UTF8',async()=>eq(await text('latin',{override:'text/plain;charset=bogus'}),'�'));
 await check('override empty quoted charset falls back to UTF8',async()=>eq(await text('latin',{override:'text/plain;charset=""'}),'�'));
 await check('invalid override MIME retains response charset',async()=>eq(await text('latin',{override:'bad mime;charset=utf-8'}),'€'));
 await check('first unknown charset blocks later duplicate',async()=>eq(await text('duplicate-first-invalid'),'�'));
 await check('first valid charset blocks later duplicate',async()=>eq(await text('duplicate-first-valid'),'€'));
 await check('UTF8 BOM overrides legacy charset',async()=>eq(await text('bom-utf8'),'A'));
 await check('only one leading UTF8 BOM is removed',async()=>eq(await text('bom-utf8-double'),'\uFEFFA'));
 await check('UTF16LE BOM overrides legacy charset',async()=>eq(await text('bom-utf16le'),'A€'));
 await check('UTF16BE BOM overrides UTF8 charset',async()=>eq(await text('bom-utf16be'),'A'));
 await check('UTF16LE charset without BOM',async()=>eq(await text('utf16le'),'A'));
 await check('UTF16BE charset without BOM',async()=>eq(await text('utf16be'),'A'));
 await check('unsupported UTF32 charset defaults to UTF8',async()=>eq(await text('unsupported'),'😀'));
 await check('replacement encoding emits one replacement for nonempty body',async()=>eq(await text('replacement'),'�'));
 await check('replacement encoding preserves empty body',async()=>eq(await text('empty-replacement'),''));
 await check('ISO2022JP response charset',async()=>eq(await text('iso2022'),'水'));
 await check('JSON response decoding stays UTF8 despite legacy MIME',async()=>eq((await request('/bytes/json-utf8',{type:'json'})).response.value,'é'));
 await check('JSON response does not accept UTF16 BOM',async()=>eq((await request('/bytes/json-utf16',{type:'json'})).response,null));
 await check('overrideMimeType before open remains effective',async()=>eq(await text('byte80',{before:'text/plain;charset=windows-1252'}),'€'));
 await check('overrideMimeType persists through another open',async()=>{const xhr=await request('/bytes/byte80',{override:'text/plain;charset=windows-1252'});eq((await request('/bytes/byte80',{xhr})).responseText,'€');});
 await check('data URL UTF8 percent bytes',async()=>eq((await request('data:text/plain;charset=utf-8,%C3%A9')).responseText,'é'));
 await check('data URL invalid UTF8 byte is replacement',async()=>eq((await request('data:text/plain,%FF%41')).responseText,'�A'));
 await check('data URL legacy bytes retain charset',async()=>eq((await request('data:text/plain;charset=windows-1252,%FF')).responseText,'ÿ'));
 await check('data URL UTF16 BOM preserved through percent decoding',async()=>eq((await request('data:text/plain,%FF%FE%41%00')).responseText,'A'));
 await check('data URL ArrayBuffer preserves arbitrary bytes',async()=>eq(bytes((await request('data:application/octet-stream,%00%80%FF',{type:'arraybuffer'})).response),'0,128,255'));
 await check('fetch data URL ArrayBuffer preserves arbitrary bytes',async()=>eq(bytes(await (await fetch('data:application/octet-stream,%00%80%FF')).arrayBuffer()),'0,128,255'));
 await check('fetch text uses UTF8 independently of MIME charset',async()=>eq(await (await fetch('data:text/plain;charset=windows-1252,%80')).text(),'�'));
 await check('literal Unicode data URL is UTF8 encoded',async()=>eq((await request('data:;charset=utf-8,é')).responseText,'é'));
 await check('invalid percent escape sequences remain literal',async()=>eq((await request('data:text/plain,%G1%2%')).responseText,'%G1%2%'));
 await check('data URL fragment is excluded from body',async()=>eq((await request('data:text/plain,hello#fragment')).responseText,'hello'));
 await check('data URL base64 preserves non-UTF8 bytes',async()=>eq(bytes((await request('data:application/octet-stream;base64,/0E=',{type:'arraybuffer'})).response),'255,65'));
 await check('data URL base64 marker is case insensitive',async()=>eq((await request('data:text/plain;BASE64,QQ==')).responseText,'A'));
 await check('data URL base64 marker permits preceding space',async()=>eq((await request('data:text/plain; base64,QQ==')).responseText,'A'));
 await check('data URL base64 forgiving ASCII whitespace',async()=>eq((await request('data:text/plain;base64,Q%20Q%3D%3D')).responseText,'A'));
 await check('invalid base64 fetch rejects with TypeError',async()=>{try{await fetch('data:text/plain;base64,@@@');throw new Error('fetch unexpectedly succeeded');}catch(e){eq(e.name,'TypeError');}});
 await check('invalid base64 XHR dispatches error without synchronous throw',async()=>{await new Promise((resolve,reject)=>{const xhr=new XMLHttpRequest();xhr.open('GET','data:text/plain;base64,@@@');xhr.onerror=resolve;xhr.onload=()=>reject(new Error('unexpected load'));xhr.send();});});
 await check('missing data URL comma is a network error',async()=>{try{await fetch('data:text/plain');throw new Error('fetch unexpectedly succeeded');}catch(e){eq(e.name,'TypeError');}});
 await check('data URL default MIME type contains text/plain',async()=>{const xhr=await request('data:;charset=windows-1252,%80');eq(xhr.getResponseHeader('content-type').toLowerCase(),'text/plain;charset=windows-1252');eq(xhr.responseText,'€');});
 const passed=rows.filter(r=>r.ok).length;console.log('XHR_ENCODING_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed}));document.documentElement.setAttribute('data-xhr-encoding-done','1');
})().catch(e=>console.log('XHR_ENCODING_INFRA '+e.name+': '+e.message));
</script>'''
EXPECTED = 42

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        name=self.path.split('?',1)[0].removeprefix('/bytes/')
        content_type,data=RESOURCES.get(name,('text/html; charset=utf-8',HTML.encode()))
        self.send_response(200);self.send_header('Content-Type',content_type);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    def log_message(self,*_):pass

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--binary',type=Path,required=True);args=parser.parse_args();binary=args.binary.resolve()
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='lume-xhr-encoding-') as profile:
            env={k:v for k,v in os.environ.items() if not k.startswith('LUMEN_')};env.update(LUMEN_PROFILE_DIR=profile,LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-xhr-encoding-done')
            run=subprocess.run([str(binary),'http://127.0.0.1:%d/'%server.server_port,'30000'],capture_output=True,text=True,env=env,timeout=35)
    except subprocess.TimeoutExpired as e:print('XHR_ENCODING_INFRA '+json.dumps({'error':'timeout','seconds':e.timeout}));return 2
    finally:server.shutdown();server.server_close();thread.join(timeout=2)
    print(run.stdout,end='');print(run.stderr,end='');rows=[];results=[]
    for line in (run.stdout+'\n'+run.stderr).splitlines():
        for marker,destination in [('XHR_ENCODING_CHECK ',rows),('XHR_ENCODING_RESULTS ',results)]:
            index=line.find(marker)
            if index>=0:destination.append(json.loads(line[index+len(marker):]))
    if run.returncode or len(rows)!=EXPECTED or len(results)!=1 or [row['i'] for row in rows]!=list(range(EXPECTED)):
        print('XHR_ENCODING_INFRA '+json.dumps({'exit':run.returncode,'checks':len(rows),'results':results}));return 2
    passed=sum(row['ok'] is True for row in rows)
    if results[0]!={'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed}:raise RuntimeError('Inconsistent fixture result')
    print('XHR_ENCODING_RECEIPT '+json.dumps({'binarySHA256':hashlib.sha256(binary.read_bytes()).hexdigest(),'fixtureSHA256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'htmlSHA256':hashlib.sha256(HTML.encode()).hexdigest(),'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed}))
    print('RESULTS xhr-response-encoding %s (%d/%d checks)'%('PASS' if passed==EXPECTED else 'FAIL',passed,EXPECTED));return 0 if passed==EXPECTED else 1

if __name__=='__main__':raise SystemExit(main())
