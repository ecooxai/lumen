#!/usr/bin/env python3
"""Observable ArrayBuffer transfer semantics in windows and dedicated workers.

Run this fixture through the native jsrun binary with tools/monitor_process.py.
Source: HTML StructuredSerializeWithTransfer and Web IDL sequence conversion.
"""
import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading

CORE = r"""
async function runTransferChecks() {
    const rows = [];
    const check = async (name, fn) => {
        try { rows.push({ name, passed: (await fn()) === true }); }
        catch (e) { rows.push({ name, passed: false, error: e.name + ': ' + e.message }); }
    };
    const throws = (name, fn) => { try { fn(); return false; } catch (e) { return e.name === name; } };
    const bytes = b => Array.from(new Uint8Array(b)).join(',');
    const channel = async (value, options, inspect) => {
        const c = new MessageChannel();
        let timer;
        try {
            return await new Promise((resolve, reject) => {
                timer = setTimeout(() => reject(new Error('channel delivery timeout')), 1000);
                c.port2.onmessage = e => resolve({ data: e.data, before, ports: e.ports });
                c.port1.postMessage(value, options);
                const before = inspect ? inspect() : true;
            });
        } finally { clearTimeout(timer); c.port1.close(); c.port2.close(); }
    };
    const withPort = fn => {
        const c = new MessageChannel();
        try { return fn(c.port1); } finally { c.port1.close(); c.port2.close(); }
    };
    await check('legacy transfer detaches sender and its views before return', async () => {
        const a = new Uint8Array([1, 2, 255]), b = a.buffer, view = new DataView(b);
        const r = await channel(b, [b], () => b.byteLength === 0 && a.byteLength === 0 && throws('TypeError', () => view.byteLength));
        return r.before && bytes(r.data) === '1,2,255';
    });
    await check('dictionary transfer overload', async () => {
        const b = new Uint8Array([8, 9]).buffer;
        const r = await channel(b, { transfer: [b] }, () => b.byteLength === 0);
        return r.before && bytes(r.data) === '8,9';
    });
    await check('iterable transfer overload', async () => {
        const b = new Uint8Array([7]).buffer;
        const r = await channel(b, new Set([b]), () => b.byteLength === 0);
        return r.before && bytes(r.data) === '7';
    });
    await check('listed buffer absent from payload is detached', async () => {
        const b = new ArrayBuffer(4);
        const r = await channel({ value: 42 }, [b], () => b.byteLength === 0);
        return r.before && r.data.value === 42;
    });
    await check('transferred graph preserves cycles aliases map and set', async () => {
        const b = new Uint8Array([2, 4, 6, 8]).buffer, v = new Uint8Array(b, 1, 2);
        const p = { b, again: b, v, twice: v, map: new Map([[b, v]]), set: new Set([b]) }; p.self = p;
        const r = await channel(p, [b]);
        return r.data.self === r.data && r.data.b === r.data.again && r.data.v === r.data.twice && r.data.v.buffer === r.data.b && r.data.v.byteOffset === 1 && r.data.v.length === 2 && r.data.map.get(r.data.b) === r.data.v && r.data.set.has(r.data.b) && bytes(r.data.b) === '2,4,6,8';
    });
    await check('overlapping views retain offsets and recipient storage', async () => {
        const b = new Uint8Array([1,2,3,4,5,6,7,8]).buffer;
        const r = await channel({ b, u: new Uint16Array(b, 2, 2), d: new DataView(b, 1, 5) }, [b]);
        return r.data.u.buffer === r.data.b && r.data.d.buffer === r.data.b && r.data.u.byteOffset === 2 && r.data.u.length === 2 && r.data.d.byteOffset === 1 && r.data.d.byteLength === 5 && r.data.d.getUint8(0) === 2;
    });
    await check('resizable ArrayBuffer preserves maximum length', async () => {
        const b = new ArrayBuffer(3, { maxByteLength: 9 }); new Uint8Array(b).set([3,4,5]);
        const r = await channel(b, [b]);
        return b.byteLength === 0 && r.data.resizable && r.data.maxByteLength === 9 && bytes(r.data) === '3,4,5';
    });
    await check('no transfer copies and leaves sender usable', async () => {
        const b = new Uint8Array([3,5]).buffer;
        const r = await channel(b); new Uint8Array(b)[0] = 9;
        return b.byteLength === 2 && r.data !== b && bytes(r.data) === '3,5';
    });
    await check('duplicate transfer rejects before getter without detachment', () => withPort(p => {
        const b = new ArrayBuffer(2); let calls = 0;
        return throws('DataCloneError', () => p.postMessage({ get x() { calls++; return 1; } }, [b,b])) && calls === 0 && b.byteLength === 2;
    }));
    await check('nontransferable object rejects before getter', () => withPort(p => {
        const b = new ArrayBuffer(2); let calls = 0;
        return throws('DataCloneError', () => p.postMessage({ get x() { calls++; return 1; } }, [b,{}])) && calls === 0 && b.byteLength === 2;
    }));
    await check('SharedArrayBuffer is not transferable', () => withPort(p => {
        const b = new ArrayBuffer(2), s = new SharedArrayBuffer(2); let calls = 0;
        return throws('DataCloneError', () => p.postMessage({ get x() { calls++; return 1; } }, [b,s])) && calls === 0 && b.byteLength === 2;
    }));
    await check('typed arrays are not transferable', () => withPort(p => {
        const b = new Uint8Array(2);
        return throws('DataCloneError', () => p.postMessage(b, [b])) && b.byteLength === 2;
    }));
    for (const [name, bad] of [['number', 1], ['null', null], ['undefined', undefined], ['string', 'x']]) {
        await check('transfer sequence rejects primitive ' + name, () => withPort(p => throws('TypeError', () => p.postMessage(1, [bad]))));
    }
    await check('options transfer must be iterable', () => withPort(p => throws('TypeError', () => p.postMessage(1, { transfer: {} }))));
    await check('serialization function failure leaves transfer intact', () => withPort(p => {
        const b = new ArrayBuffer(2);
        return throws('DataCloneError', () => p.postMessage({ b, f() {} }, [b])) && b.byteLength === 2;
    }));
    await check('getter exception precedes all detachment', () => withPort(p => {
        const b = new ArrayBuffer(2), marker = new Error('marker');
        try { p.postMessage({ get x() { throw marker; } }, [b]); } catch (e) { return e === marker && b.byteLength === 2; }
        return false;
    }));
    await check('detached transfer is checked after payload serialization', () => withPort(p => {
        const b = new ArrayBuffer(2); b.transfer(); let calls = 0;
        return throws('DataCloneError', () => p.postMessage({ get x() { calls++; return 1; } }, [b])) && calls === 1;
    }));
    await check('later detached item preserves earlier transfer side effect', () => withPort(p => {
        const a = new ArrayBuffer(2), b = new ArrayBuffer(2); b.transfer();
        return throws('DataCloneError', () => p.postMessage(null, [a,b])) && a.byteLength === 0;
    }));
    await check('getter-detached listed buffer rejects after getter', () => withPort(p => {
        const b = new ArrayBuffer(2); let calls = 0;
        return throws('DataCloneError', () => p.postMessage({ get x() { calls++; b.transfer(); return 1; } }, [b])) && calls === 1 && b.byteLength === 0;
    }));
    await check('closed channel still performs serialization transfer', () => {
        const c = new MessageChannel(), b = new ArrayBuffer(2); c.port1.close(); c.port2.close();
        c.port1.postMessage(null, [b]); return b.byteLength === 0;
    });
    await check('MessagePort postMessage requires its payload', () => withPort(p => throws('TypeError', () => p.postMessage())));
    await check('structuredClone transfers and retains graph identity', () => {
        const b = new Uint8Array([6,7]).buffer, x = { b, v: new Uint8Array(b) }; x.self = x;
        const y = structuredClone(x, { transfer: [b] });
        return b.byteLength === 0 && y.self === y && y.v.buffer === y.b && bytes(y.b) === '6,7';
    });
    await check('structuredClone supports iterable dictionary transfer', () => {
        const b = new Uint8Array([1]).buffer, y = structuredClone(b, { transfer: new Set([b]) });
        return b.byteLength === 0 && bytes(y) === '1';
    });
    await check('structuredClone duplicate rejects without detaching', () => {
        const b = new ArrayBuffer(2);
        return throws('DataCloneError', () => structuredClone(b, { transfer: [b,b] })) && b.byteLength === 2;
    });
    await check('structuredClone invalid transfer sequence rejects', () => throws('TypeError', () => structuredClone(null, { transfer: [1] })));
    await check('structuredClone requires one argument', () => throws('TypeError', () => structuredClone()));
    await check('structuredClone null options are empty', () => structuredClone({ answer: 42 }, null).answer === 42);
    await check('transfer options getter is evaluated once', async () => {
        const b = new Uint8Array([3]).buffer; let calls = 0;
        const r = await channel(b, { get transfer() { calls++; return [b]; } });
        return calls === 1 && b.byteLength === 0 && bytes(r.data) === '3';
    });
    return rows;
}
"""
CORE_COUNT = 31
HTML = r"""<!doctype html><meta charset="utf-8"><title>Native message transfer regression</title><script>
'use strict';
__CORE__
const rows=[];
function append(scope,row) { const result={i:rows.length,scope,name:row.name,ok:row.passed===true,error:row.error||null}; rows.push(result); console.log('MESSAGE_TRANSFER_CHECK '+JSON.stringify(result)); }
function finish(issue) { if (window.__transferFinished) return; window.__transferFinished=true; const passed=rows.filter(x=>x.ok).length; console.log('MESSAGE_TRANSFER_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed,issue:issue||null})); document.documentElement.setAttribute('data-message-transfer-done','1'); }
(async()=>{
    for(const row of await runTransferChecks()) append('window',row);
    const worker = new Worker('/worker.js'), pending=new Map();
    worker.onmessage=e=>{const fn=pending.get(e.data.type);if(fn){pending.delete(e.data.type);fn(e.data);}};
    worker.onerror=e=>{e.preventDefault();finish(String(e.message||'worker error'));};
    const wait=kind=>new Promise(resolve=>pending.set(kind,resolve));
    const timeout=setTimeout(()=>{worker.terminate();finish('worker timeout');},12000);
    const ready=wait('checks');worker.postMessage({type:'checks'});
    const result=await ready;for(const row of result.rows)append('worker',row);
    const b=new Uint8Array([11,22,33]).buffer, echo=wait('echo'), detached=wait('detached');
    worker.postMessage({type:'echo',b,v:new Uint8Array(b)},[b]);
    append('cross-worker',{name:'parent detaches before worker postMessage returns',passed:b.byteLength===0});
    const answer=await echo, state=await detached;
    append('cross-worker',{name:'worker receives aliases and bytes',passed:answer.aliased===true&&answer.bytes==='11,22,33'});
    append('cross-worker',{name:'returned transfer remains intact in main realm',passed:answer.b instanceof ArrayBuffer&&Array.from(new Uint8Array(answer.b)).join(',')==='11,22,33'});
    append('cross-worker',{name:'worker detaches before returning from reply',passed:state.ok===true});
    const c=new MessageChannel(), pbuf=new Uint8Array([44,55]).buffer;
    const viaPort=new Promise(resolve=>{c.port1.onmessage=e=>resolve(e.data);});
    worker.postMessage({type:'port',p:c.port2,b:pbuf},[c.port2,pbuf]);
    append('cross-worker',{name:'buffer alongside a transferred port detaches',passed:pbuf.byteLength===0});
    const reply=await viaPort;
    append('cross-worker',{name:'transferred endpoint carries transferred buffer back',passed:reply.b instanceof ArrayBuffer&&Array.from(new Uint8Array(reply.b)).join(',')==='44,55'});
    c.port1.close();c.port2.close();clearTimeout(timeout);worker.terminate();finish(null);
})().catch(e=>finish(e.name+': '+e.message));
</script>""".replace('__CORE__', CORE)
WORKER = "'use strict';\n" + CORE + r"""
onmessage=async e=>{
    const d=e.data;
    if(d.type==='checks') postMessage({type:'checks',rows:await runTransferChecks()});
    else if(d.type==='echo') {
        const b=d.b, aliased=d.v.buffer===b, bytes=Array.from(new Uint8Array(b)).join(',');
        postMessage({type:'echo',b,aliased,bytes},{transfer:[b]});
        postMessage({type:'detached',ok:b.byteLength===0&&d.v.byteLength===0});
    } else if(d.type==='port') { d.p.postMessage({b:d.b},[d.b]); d.p.close(); }
};
"""
EXPECTED = CORE_COUNT * 2 + 6

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        is_worker = self.path.split('?', 1)[0] == '/worker.js'
        data = (WORKER if is_worker else HTML).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/javascript; charset=utf-8' if is_worker else 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.end_headers()
        self.wfile.write(data)
    def log_message(self, *_):
        pass

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, required=True)
    args=parser.parse_args()
    binary=args.binary.resolve()
    if not binary.is_file(): parser.error('binary does not exist')
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='lume-message-transfer-') as profile:
            env={k:v for k,v in os.environ.items() if not k.startswith('LUMEN_')}
            env.update(LUMEN_PROFILE_DIR=profile,LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-message-transfer-done')
            run=subprocess.run([str(binary),'http://127.0.0.1:%d/'%server.server_port,'20000'],capture_output=True,text=True,env=env,timeout=25)
    except subprocess.TimeoutExpired as exc:
        print('MESSAGE_TRANSFER_INFRA '+json.dumps({'error':'timeout','seconds':exc.timeout}));return 2
    finally:
        server.shutdown();server.server_close();thread.join(timeout=2)
    print(run.stdout,end='');print(run.stderr,end='')
    rows=[];summaries=[]
    for line in (run.stdout+'\n'+run.stderr).splitlines():
        for marker,target in [('MESSAGE_TRANSFER_CHECK ',rows),('MESSAGE_TRANSFER_RESULTS ',summaries)]:
            at=line.find(marker)
            if at>=0:
                try:target.append(json.loads(line[at+len(marker):]))
                except json.JSONDecodeError:print('MESSAGE_TRANSFER_INFRA malformed JSON');return 2
    if run.returncode!=0 or len(rows)!=EXPECTED or len(summaries)!=1 or [r.get('i') for r in rows]!=list(range(EXPECTED)):
        print('MESSAGE_TRANSFER_INFRA '+json.dumps({'exit':run.returncode,'checks':len(rows),'expected':EXPECTED,'summaries':summaries}));return 2
    passed=sum(r.get('ok') is True for r in rows);summary=summaries[0]
    if summary.get('total')!=EXPECTED or summary.get('passed')!=passed or summary.get('failed')!=EXPECTED-passed or summary.get('issue') is not None:
        print('MESSAGE_TRANSFER_INFRA inconsistent or incomplete summary');return 2
    print('MESSAGE_TRANSFER_RECEIPT '+json.dumps({'binary':str(binary),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'fixture_sha256':hashlib.sha256(HTML.encode()).hexdigest(),'worker_sha256':hashlib.sha256(WORKER.encode()).hexdigest(),'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed,'scopes':{s:{'total':sum(r['scope']==s for r in rows),'passed':sum(r['scope']==s and r['ok'] for r in rows)} for s in ['window','worker','cross-worker']}}))
    print('RESULTS message-transfer %s (%d/%d checks)'%('PASS' if passed==EXPECTED else 'FAIL',passed,EXPECTED))
    return 0 if passed==EXPECTED else 1

if __name__=='__main__':raise SystemExit(main())
