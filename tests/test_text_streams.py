#!/usr/bin/env python3
"""Encoding stream identity, incremental state, errors and backpressure."""
import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading

CORE=r"""
async function runTextStreamChecks() {
    const rows=[];
    const check=async(name,fn)=>{
        let timer;
        try { const value=await Promise.race([Promise.resolve().then(fn),new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error('check timeout')),300);})]);rows.push({name,passed:value===true}); }
        catch(e){rows.push({name,passed:false,error:e.name+': '+e.message});}
        finally{clearTimeout(timer);}
    };
    const throws=(kind,fn)=>{try{fn();return false;}catch(e){return e.name===kind;}};
    const reject=async(p)=>{try{await p;return null;}catch(e){return e;}};
    async function transform(stream,chunks) {
        const reader=stream.readable.getReader(),writer=stream.writable.getWriter(),out=[];
        const read=(async()=>{for(;;){const row=await reader.read();if(row.done)return;out.push(row.value);}})();
        const readFailure=reject(read);
        const writeFailure=reject((async()=>{for(const chunk of chunks)await writer.write(chunk);await writer.close();})());
        const [writeError,readError]=await Promise.all([writeFailure,readFailure]);
        return{out,writeError,readError};
    }
    const decode=async(chunks,label='utf-8',options)=>{
        const result=await transform(new TextDecoderStream(label,options),chunks);
        if(result.writeError)throw result.writeError;if(result.readError)throw result.readError;
        return result.out.join('');
    };
    const encode=async(chunks)=>{
        const result=await transform(new TextEncoderStream(),chunks);
        if(result.writeError)throw result.writeError;if(result.readError)throw result.readError;
        return result.out.flatMap(chunk=>Array.from(chunk)).join(',');
    };
    const u8=a=>new Uint8Array(a);
    await check('standalone encoder interface inheritance',()=>Object.getPrototypeOf(TextEncoderStream)===Function.prototype&&Object.getPrototypeOf(TextEncoderStream.prototype)===Object.prototype);
    await check('standalone decoder interface inheritance',()=>Object.getPrototypeOf(TextDecoderStream)===Function.prototype&&Object.getPrototypeOf(TextDecoderStream.prototype)===Object.prototype);
    await check('interface names lengths and tags',()=>TextEncoderStream.name==='TextEncoderStream'&&TextEncoderStream.length===0&&TextDecoderStream.name==='TextDecoderStream'&&TextDecoderStream.length===0&&Object.prototype.toString.call(new TextEncoderStream())==='[object TextEncoderStream]'&&Object.prototype.toString.call(new TextDecoderStream())==='[object TextDecoderStream]');
    await check('decoder options canonical encoding and fatal single-byte chunks',async()=>{const d=new TextDecoderStream('latin1',{fatal:1,ignoreBOM:1});const r=await transform(d,[u8([0x80]),u8([0xA0])]);return d.encoding==='windows-1252'&&d.fatal===true&&d.ignoreBOM===true&&!r.writeError&&!r.readError&&r.out.join('')==='€\u00A0';});
    await check('encoder encoding and separate streams',()=>{const e=new TextEncoderStream();return e.encoding==='utf-8'&&e.readable instanceof ReadableStream&&e.writable instanceof WritableStream&&!(e instanceof TransformStream);});
    await check('decoder readonly enumerable attributes',()=>{const p=TextDecoderStream.prototype;return ['encoding','fatal','ignoreBOM','readable','writable'].every(k=>{const d=Object.getOwnPropertyDescriptor(p,k);return d&&typeof d.get==='function'&&d.set===undefined&&d.enumerable;});});
    await check('encoder attributes reject forged receivers',()=>['encoding','readable','writable'].every(k=>throws('TypeError',()=>Object.getOwnPropertyDescriptor(TextEncoderStream.prototype,k).get.call(Object.create(TextEncoderStream.prototype)))));
    await check('decoder attributes reject forged receivers',()=>['encoding','fatal','ignoreBOM','readable','writable'].every(k=>throws('TypeError',()=>Object.getOwnPropertyDescriptor(TextDecoderStream.prototype,k).get.call({}))));
    await check('encoder brand rejects serialization after public prototype tampering',()=>{const s=new TextEncoderStream();Object.setPrototypeOf(s,null);Object.defineProperty(s,Symbol.toStringTag,{value:'Object'});return throws('DataCloneError',()=>structuredClone(s));});
    await check('nested decoder brand rejects serialization after public prototype tampering',()=>{const s=new TextDecoderStream();Object.setPrototypeOf(s,{});Object.defineProperty(s,Symbol.toStringTag,{value:'Object'});return throws('DataCloneError',()=>structuredClone({stream:s}));});
    await check('public decoder properties do not replace private state',async()=>{const d=new TextDecoderStream('utf-8',{fatal:true});Object.defineProperties(d,{fatal:{value:false},encoding:{value:'windows-1252'}});const r=await transform(d,[u8([255])]);return r.writeError&&r.writeError.name==='TypeError'&&r.readError&&r.readError.name==='TypeError';});
    await check('decoder rejects invalid label',()=>throws('RangeError',()=>new TextDecoderStream('replacement')));
    await check('decoder rejects primitive dictionary',()=>throws('TypeError',()=>new TextDecoderStream('utf-8',1)));
    await check('decoder accepts null dictionary',()=>new TextDecoderStream('utf-8',null).fatal===false);
    await check('UTF8 scalar survives every byte boundary',async()=>await decode([u8([240]),u8([159]),u8([152]),u8([128])])==='😀');
    await check('UTF8 BOM survives every byte boundary',async()=>await decode([u8([239]),u8([187]),u8([191,65])])==='A');
    await check('ignoreBOM preserves split BOM',async()=>await decode([u8([239]),u8([187,191]),u8([65])],'utf-8',{ignoreBOM:true})==='\uFEFFA');
    await check('UTF16 scalar spans bytes and chunks',async()=>await decode([u8([0x3D]),u8([0xD8,0]),u8([0xDE])],'utf-16le')==='😀');
    await check('final incomplete UTF8 emits one replacement',async()=>await decode([u8([0xF0,0x90]),u8([0x80])])==='\uFFFD');
    await check('fatal incomplete flush errors both sides',async()=>{const r=await transform(new TextDecoderStream('utf-8',{fatal:true}),[u8([0xF0,0x90])]);return r.writeError&&r.writeError.name==='TypeError'&&r.readError&&r.readError.name==='TypeError';});
    await check('invalid chunk type errors both sides',async()=>{const r=await transform(new TextDecoderStream(),['abc']);return r.writeError&&r.writeError.name==='TypeError'&&r.readError&&r.readError.name==='TypeError';});
    await check('DataView uses only selected bytes',async()=>await decode([new DataView(u8([65,66,67]).buffer,1,1)])==='B');
    await check('empty decoder chunks do not emit empty strings',async()=>{const r=await transform(new TextDecoderStream(),[u8([]),u8([65]),u8([])]);return !r.writeError&&!r.readError&&r.out.length===1&&r.out[0]==='A';});
    await check('decoder streaming reconsumes invalid continuation',async()=>await decode([u8([0xE2]),u8([65])])==='\uFFFDA');
    await check('encoder combines surrogate pair across chunks',async()=>await encode(['\uD83D','\uDE00'])==='240,159,152,128');
    await check('encoder retains high surrogate across empty chunks',async()=>await encode(['\uD83D','','','\uDE00'])==='240,159,152,128');
    await check('encoder flushes dangling high surrogate',async()=>await encode(['\uD800'])==='239,191,189');
    await check('encoder replaces unpaired low surrogate',async()=>await encode(['\uDC00A'])==='239,191,189,65');
    await check('encoder reconsumes nonlow after pending high',async()=>await encode(['\uD800','A'])==='239,191,189,65');
    await check('encoder converts undefined and null as DOMString',async()=>await encode([undefined,null])==='117,110,100,101,102,105,110,101,100,110,117,108,108');
    await check('encoder Symbol input errors both sides',async()=>{const r=await transform(new TextEncoderStream(),[Symbol('x')]);return r.writeError&&r.writeError.name==='TypeError'&&r.readError&&r.readError.name==='TypeError';});
    await check('empty encoder input emits no empty byte chunks',async()=>{const r=await transform(new TextEncoderStream(),['','A','']);return !r.writeError&&!r.readError&&r.out.length===1&&r.out[0].length===1&&r.out[0][0]===65;});
    await check('codec writes wait for readable demand',async()=>{
        const s=new TextEncoderStream(),writer=s.writable.getWriter();let settled=false;
        const write=writer.write('A').then(()=>{settled=true;});
        await Promise.resolve();await Promise.resolve();await Promise.resolve();const held=!settled;
        const reader=s.readable.getReader(),row=await reader.read();await write;await writer.close();
        return held&&!row.done&&row.value[0]===65&&(await reader.read()).done;
    });
    await check('readable cancellation rejects pending codec write',async()=>{
        const s=new TextEncoderStream(),writer=s.writable.getWriter(),reason=new Error('cancel-marker');
        const pending=reject(writer.write('A'));await s.readable.cancel(reason);
        return (await pending)===reason&&(await reject(writer.closed))===reason;
    });
    await check('writable abort errors readable with original reason',async()=>{
        const s=new TextDecoderStream(),reader=s.readable.getReader(),writer=s.writable.getWriter(),reason=new Error('abort-marker');
        const pending=reject(reader.read());await writer.abort(reason);return(await pending)===reason;
    });
    return rows;
}
"""
CORE_COUNT=35
HTML=r"""<!doctype html><meta charset="utf-8"><title>Encoding stream regression</title><iframe></iframe><script>
'use strict';
__CORE__
const rows=[];
function append(scope,row){const out={i:rows.length,scope,name:row.name,ok:row.passed===true,error:row.error||null};rows.push(out);console.log('TEXT_STREAM_CHECK '+JSON.stringify(out));}
function finish(issue){if(window.__textStreamDone)return;window.__textStreamDone=true;const passed=rows.filter(r=>r.ok).length;console.log('TEXT_STREAM_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed,issue:issue||null}));document.documentElement.setAttribute('data-text-stream-done','1');}
(async()=>{
    for(const row of await runTextStreamChecks())append('window',row);
    const w=document.querySelector('iframe').contentWindow;
    const cross=(name,fn)=>{try{append('cross-realm',{name,passed:fn()===true});}catch(e){append('cross-realm',{name,passed:false,error:e.name+': '+e.message});}};
    cross('parent getter reads child decoder options',()=>Object.getOwnPropertyDescriptor(TextDecoderStream.prototype,'fatal').get.call(new w.TextDecoderStream('utf-8',{fatal:true}))===true);
    cross('child getter reads parent decoder encoding',()=>Object.getOwnPropertyDescriptor(w.TextDecoderStream.prototype,'encoding').get.call(new TextDecoderStream('latin1'))==='windows-1252');
    cross('parent getter retains child readable realm',()=>Object.getOwnPropertyDescriptor(TextEncoderStream.prototype,'readable').get.call(new w.TextEncoderStream()) instanceof w.ReadableStream);
    cross('child getter retains parent writable realm',()=>Object.getOwnPropertyDescriptor(w.TextEncoderStream.prototype,'writable').get.call(new TextEncoderStream()) instanceof WritableStream);
    const worker=new Worker('/worker.js');const timer=setTimeout(()=>{worker.terminate();finish('worker timeout');},15000);
    worker.onmessage=e=>{clearTimeout(timer);worker.terminate();if(!Array.isArray(e.data)){finish('invalid worker payload');return;}for(const row of e.data)append('worker',row);finish(null);};
    worker.onerror=e=>{clearTimeout(timer);worker.terminate();e.preventDefault();finish(String(e.message||'worker error'));};
})().catch(e=>finish(e.name+': '+e.message));
</script>""".replace('__CORE__',CORE)
WORKER="'use strict';\n"+CORE+'\nrunTextStreamChecks().then(rows=>postMessage(rows));\n'
EXPECTED=CORE_COUNT*2+4

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        worker=self.path.split('?',1)[0]=='/worker.js';data=(WORKER if worker else HTML).encode()
        self.send_response(200);self.send_header('Content-Type','application/javascript; charset=utf-8' if worker else 'text/html; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    def log_message(self,*_):pass

def main():
    p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,required=True);args=p.parse_args();binary=args.binary.resolve()
    if not binary.is_file():p.error('binary does not exist')
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='lume-text-streams-') as profile:
            env={k:v for k,v in os.environ.items() if not k.startswith('LUMEN_')};env.update(LUMEN_PROFILE_DIR=profile,LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-text-stream-done')
            run=subprocess.run([str(binary),'http://127.0.0.1:%d/'%server.server_port,'30000'],capture_output=True,text=True,env=env,timeout=35)
    except subprocess.TimeoutExpired as e:print('TEXT_STREAM_INFRA '+json.dumps({'error':'timeout','seconds':e.timeout}));return 2
    finally:server.shutdown();server.server_close();thread.join(timeout=2)
    print(run.stdout,end='');print(run.stderr,end='');rows=[];summaries=[]
    for line in (run.stdout+'\n'+run.stderr).splitlines():
        for marker,target in [('TEXT_STREAM_CHECK ',rows),('TEXT_STREAM_RESULTS ',summaries)]:
            at=line.find(marker)
            if at>=0:
                try:target.append(json.loads(line[at+len(marker):]))
                except json.JSONDecodeError:print('TEXT_STREAM_INFRA malformed JSON');return 2
    if run.returncode!=0 or len(rows)!=EXPECTED or len(summaries)!=1 or [r.get('i') for r in rows]!=list(range(EXPECTED)):
        print('TEXT_STREAM_INFRA '+json.dumps({'exit':run.returncode,'checks':len(rows),'expected':EXPECTED,'summaries':summaries}));return 2
    passed=sum(r.get('ok') is True for r in rows);s=summaries[0]
    if s.get('total')!=EXPECTED or s.get('passed')!=passed or s.get('failed')!=EXPECTED-passed or s.get('issue') is not None:print('TEXT_STREAM_INFRA inconsistent summary');return 2
    print('TEXT_STREAM_RECEIPT '+json.dumps({'binary':str(binary),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'fixture_sha256':hashlib.sha256(HTML.encode()).hexdigest(),'worker_sha256':hashlib.sha256(WORKER.encode()).hexdigest(),'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed,'scopes':{scope:{'total':sum(r['scope']==scope for r in rows),'passed':sum(r['scope']==scope and r['ok'] for r in rows)}for scope in ['window','worker','cross-realm']}}))
    print('RESULTS text-streams %s (%d/%d checks)'%('PASS' if passed==EXPECTED else 'FAIL',passed,EXPECTED));return 0 if passed==EXPECTED else 1

if __name__=='__main__':raise SystemExit(main())
