#!/usr/bin/env python3
"""Legacy decoder chunk boundaries, finite state, fatal recovery and stream delivery."""
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
async function runLegacyCodecChecks() {
    const rows = [];
    const eq = (actual, expected) => {
        if (actual !== expected) throw new Error('expected ' + JSON.stringify(expected) + ', got ' + JSON.stringify(actual));
    };
    const u8 = values => new Uint8Array(values);
    const typeError = fn => { try { fn(); } catch (error) { if (error.name === 'TypeError') return; throw error; } throw new Error('Expected TypeError'); };
    const check = async (name, fn) => {
        let timer;
        try {
            await Promise.race([Promise.resolve().then(fn), new Promise((_, reject) => {
                timer = setTimeout(() => reject(new Error('check timeout')), 500);
            })]);
            rows.push({ name, passed: true });
        } catch (error) { rows.push({ name, passed: false, error: error.name + ': ' + error.message }); }
        finally { clearTimeout(timer); }
    };
    const examples = [
        ['gbk', [0xCB, 0xAE], '水'],
        ['gb18030', [0x94, 0x39, 0xFC, 0x36], '😀'],
        ['big5', [0xA4, 0xF4], '水'],
        ['euc-jp', [0xBF, 0xE5], '水'],
        ['iso-2022-jp', [0x1B, 0x24, 0x42, 0x3F, 0x65, 0x1B, 0x28, 0x42], '水'],
        ['shift_jis', [0x90, 0x85], '水'],
        ['euc-kr', [0xE2, 0xA9], '水'],
    ];
    for (const [label, bytes, expected] of examples) {
        await check(label + ' existing one-shot output', () => eq(new TextDecoder(label).decode(u8(bytes)), expected));
        for (const fatal of [false, true]) {
            await check(label + ' every two-chunk boundary fatal=' + fatal, () => {
                for (let split = 0; split <= bytes.length; ++split) {
                    const decoder = new TextDecoder(label, { fatal });
                    const output = decoder.decode(u8(bytes.slice(0, split)), { stream: true }) + decoder.decode(u8(bytes.slice(split)));
                    eq(output, expected);
                    eq(decoder.decode(u8([65])), 'A');
                }
            });
            await check(label + ' bytewise with empty chunks fatal=' + fatal, () => {
                const decoder = new TextDecoder(label, { fatal });
                let output = decoder.decode(u8([]), { stream: true });
                for (const byte of bytes) {
                    output += decoder.decode(u8([byte]), { stream: true });
                    output += decoder.decode(u8([]), { stream: true });
                }
                eq(output + decoder.decode(), expected);
            });
        }
        await check(label + ' final chunk resets state for subsequent decode', () => {
            const decoder = new TextDecoder(label);
            eq(decoder.decode(u8(bytes), { stream: true }) + decoder.decode(), expected);
            eq(decoder.decode(u8([0x21])), '!');
        });
        await check(label + ' TextDecoderStream bytewise', async () => {
            const stream = new TextDecoderStream(label), reader = stream.readable.getReader(), writer = stream.writable.getWriter();
            let output = '';
            const reading = (async () => { for (;;) { const row = await reader.read(); if (row.done) return; output += row.value; } })();
            const writing = (async () => { for (const byte of bytes) await writer.write(u8([byte])); await writer.close(); })();
            const outcomes = await Promise.allSettled([reading, writing]);
            for (const outcome of outcomes) if (outcome.status === 'rejected') throw outcome.reason;
            eq(output, expected);
        });
    }
    for (const [label, lead] of [['gbk', 0x81], ['gb18030', 0x81], ['big5', 0x81], ['euc-jp', 0xA1], ['shift_jis', 0x81], ['euc-kr', 0x81]]) {
        await check(label + ' incomplete final input emits one replacement', () => {
            const decoder = new TextDecoder(label);
            eq(decoder.decode(u8([lead]), { stream: true }), '');
            eq(decoder.decode(), '\uFFFD');
            eq(decoder.decode(), '');
        });
        await check(label + ' fatal incomplete flush then reset', () => {
            const decoder = new TextDecoder(label, { fatal: true });
            eq(decoder.decode(u8([lead]), { stream: true }), '');
            typeError(() => decoder.decode());
            eq(decoder.decode(u8([65])), 'A');
        });
        await check(label + ' invalid trail reconsumes ASCII', () => {
            const decoder = new TextDecoder(label);
            eq(decoder.decode(u8([lead]), { stream: true }), '');
            eq(decoder.decode(u8([0x20, 65])), '\uFFFD A');
        });
        await check(label + ' fatal streaming retains unconsumed bytes', () => {
            const decoder = new TextDecoder(label, { fatal: true });
            eq(decoder.decode(u8([lead]), { stream: true }), '');
            typeError(() => decoder.decode(u8([0x20, 65]), { stream: true }));
            eq(decoder.decode(u8([66])), ' AB');
        });
    }
    await check('ISO2022 incomplete escape EOF reconsumes leading byte', () => {
        const decoder = new TextDecoder('iso-2022-jp');
        eq(decoder.decode(u8([0x1B]), { stream: true }), '');
        eq(decoder.decode(u8([0x24]), { stream: true }), '');
        eq(decoder.decode(), '\uFFFD$');
    });
    await check('ISO2022 invalid escape reconsumes both following bytes', () => {
        const decoder = new TextDecoder('iso-2022-jp');
        eq(decoder.decode(u8([0x1B, 0x24]), { stream: true }), '');
        eq(decoder.decode(u8([0x50])), '\uFFFD$P');
    });
    await check('ISO2022 consecutive escapes retain output flag', () => {
        const decoder = new TextDecoder('iso-2022-jp');
        eq(decoder.decode(u8([0x1B, 0x28, 0x42]), { stream: true }), '');
        eq(decoder.decode(u8([0x1B, 0x28, 0x42, 65])), '\uFFFDA');
    });
    await check('ISO2022 fatal consecutive escape resumes in output state', () => {
        const decoder = new TextDecoder('iso-2022-jp', { fatal: true });
        eq(decoder.decode(u8([0x1B, 0x28, 0x42]), { stream: true }), '');
        typeError(() => decoder.decode(u8([0x1B, 0x28, 0x42]), { stream: true }));
        eq(decoder.decode(u8([65])), 'A');
    });
    await check('ISO2022 split Katakana escape and final reset', () => {
        const decoder = new TextDecoder('iso-2022-jp');
        eq(decoder.decode(u8([0x1B, 0x28]), { stream: true }), '');
        eq(decoder.decode(u8([0x49, 0x21])), '\uFF61');
        eq(decoder.decode(u8([0x21])), '!');
    });
    await check('ISO2022 empty flush resets Katakana state', () => {
        const decoder = new TextDecoder('iso-2022-jp');
        eq(decoder.decode(u8([0x1B, 0x28, 0x49]), { stream: true }), '');
        eq(decoder.decode(), '');
        eq(decoder.decode(u8([0x21])), '!');
    });
    await check('ISO2022 fatal invalid escape preserves reconsume queue', () => {
        const decoder = new TextDecoder('iso-2022-jp', { fatal: true });
        eq(decoder.decode(u8([0x1B, 0x28]), { stream: true }), '');
        typeError(() => decoder.decode(u8([0x50]), { stream: true }));
        eq(decoder.decode(u8([65])), '(PA');
    });
    await check('ISO2022 Roman state and explicit final flush', () => {
        const decoder = new TextDecoder('iso-2022-jp');
        eq(decoder.decode(u8([0x1B, 0x28, 0x4A]), { stream: true }), '');
        eq(decoder.decode(u8([0x5C]), { stream: true }), '\u00A5');
        eq(decoder.decode(), '');
        eq(decoder.decode(u8([0x5C])), '\\');
    });
    await check('GB18030 invalid third byte restores digit and byte', () => {
        const decoder = new TextDecoder('gb18030');
        eq(decoder.decode(u8([0x81, 0x30]), { stream: true }), '');
        eq(decoder.decode(u8([0x20])), '\uFFFD0 ');
    });
    await check('GB18030 invalid fourth byte restores three bytes', () => {
        const decoder = new TextDecoder('gb18030');
        eq(decoder.decode(u8([0x81, 0x30, 0x81]), { stream: true }), '');
        eq(decoder.decode(u8([0x20])), '\uFFFD0\uFFFD ');
    });
    await check('GB18030 EOF emits a single error for unfinished four-byte scalar', () => {
        const decoder = new TextDecoder('gb18030');
        eq(decoder.decode(u8([0x81, 0x30, 0x81]), { stream: true }), '');
        eq(decoder.decode(), '\uFFFD');
    });
    await check('single-byte fatal streaming accepts valid bytes', () => {
        const decoder = new TextDecoder('windows-1252', { fatal: true });
        eq(decoder.decode(u8([0x80]), { stream: true }), '€');
        eq(decoder.decode(), '');
    });
    await check('single-byte fatal streaming retains bytes after error', () => {
        const decoder = new TextDecoder('iso-8859-3', { fatal: true });
        typeError(() => decoder.decode(u8([0xA5, 65]), { stream: true }));
        eq(decoder.decode(u8([66])), 'AB');
    });
    await check('input mutation cannot change retained leading byte', () => {
        const decoder = new TextDecoder('shift_jis'), input = u8([0x90]);
        eq(decoder.decode(input, { stream: true }), ''); input[0] = 65;
        eq(decoder.decode(u8([0x85])), '水');
    });
    await check('decoder instances keep independent ISO2022 state', () => {
        const first = new TextDecoder('iso-2022-jp'), second = new TextDecoder('iso-2022-jp');
        eq(first.decode(u8([0x1B, 0x28, 0x49]), { stream: true }), '');
        eq(second.decode(u8([0x21])), '!');
        eq(first.decode(u8([0x21])), '\uFF61');
    });
    return rows;
}
"""
CORE_COUNT=88
HTML=r"""<!doctype html><meta charset="utf-8"><title>Legacy codec regression</title><iframe></iframe><script>
'use strict';
__CORE__
const rows=[];
function append(scope,row){const out={i:rows.length,scope,name:row.name,ok:row.passed===true,error:row.error||null};rows.push(out);console.log('LEGACY_CODEC_CHECK '+JSON.stringify(out));}
function finish(issue){if(window.__legacyCodecDone)return;window.__legacyCodecDone=true;const passed=rows.filter(r=>r.ok).length;console.log('LEGACY_CODEC_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed,issue:issue||null}));document.documentElement.setAttribute('data-legacy-codec-done','1');}
(async()=>{
    for(const row of await runLegacyCodecChecks())append('window',row);
    const w=document.querySelector('iframe').contentWindow;
    const cross=(name,fn)=>{try{append('cross-realm',{name,passed:fn()===true});}catch(e){append('cross-realm',{name,passed:false,error:e.name+': '+e.message});}};
    cross('parent decode retains child legacy state',()=>{const decoder=new w.TextDecoder('shift_jis');return TextDecoder.prototype.decode.call(decoder,new Uint8Array([0x90]),{stream:true})===''&&TextDecoder.prototype.decode.call(decoder,new Uint8Array([0x85]))==='水';});
    cross('child decode retains parent legacy state',()=>{const decoder=new TextDecoder('iso-2022-jp');return w.TextDecoder.prototype.decode.call(decoder,new Uint8Array([0x1b,0x28]),{stream:true})===''&&w.TextDecoder.prototype.decode.call(decoder,new Uint8Array([0x49,0x21]))==='｡';});
    const worker=new Worker('/worker.js');const timer=setTimeout(()=>{worker.terminate();finish('worker timeout');},15000);
    worker.onmessage=e=>{clearTimeout(timer);worker.terminate();if(!Array.isArray(e.data)){finish('invalid worker payload');return;}for(const row of e.data)append('worker',row);finish(null);};
    worker.onerror=e=>{clearTimeout(timer);worker.terminate();e.preventDefault();finish(String(e.message||'worker error'));};
})().catch(e=>finish(e.name+': '+e.message));
</script>""".replace('__CORE__',CORE)
WORKER="'use strict';\n"+CORE+'\nrunLegacyCodecChecks().then(rows=>postMessage(rows));\n'
EXPECTED=CORE_COUNT*2+2

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
        with tempfile.TemporaryDirectory(prefix='lume-legacy-codecs-') as profile:
            env={k:v for k,v in os.environ.items() if not k.startswith('LUMEN_')};env.update(LUMEN_PROFILE_DIR=profile,LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-legacy-codec-done')
            run=subprocess.run([str(binary),'http://127.0.0.1:%d/'%server.server_port,'30000'],capture_output=True,text=True,env=env,timeout=35)
    except subprocess.TimeoutExpired as e:print('LEGACY_CODEC_INFRA '+json.dumps({'error':'timeout','seconds':e.timeout}));return 2
    finally:server.shutdown();server.server_close();thread.join(timeout=2)
    print(run.stdout,end='');print(run.stderr,end='');rows=[];summaries=[]
    for line in (run.stdout+'\n'+run.stderr).splitlines():
        for marker,target in [('LEGACY_CODEC_CHECK ',rows),('LEGACY_CODEC_RESULTS ',summaries)]:
            at=line.find(marker)
            if at>=0:
                try:target.append(json.loads(line[at+len(marker):]))
                except json.JSONDecodeError:print('LEGACY_CODEC_INFRA malformed JSON');return 2
    if run.returncode!=0 or len(rows)!=EXPECTED or len(summaries)!=1 or [r.get('i') for r in rows]!=list(range(EXPECTED)):
        print('LEGACY_CODEC_INFRA '+json.dumps({'exit':run.returncode,'checks':len(rows),'expected':EXPECTED,'summaries':summaries}));return 2
    passed=sum(r.get('ok') is True for r in rows);s=summaries[0]
    if s.get('total')!=EXPECTED or s.get('passed')!=passed or s.get('failed')!=EXPECTED-passed or s.get('issue') is not None:print('LEGACY_CODEC_INFRA inconsistent summary');return 2
    print('LEGACY_CODEC_RECEIPT '+json.dumps({'binary':str(binary),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'fixture_sha256':hashlib.sha256(HTML.encode()).hexdigest(),'worker_sha256':hashlib.sha256(WORKER.encode()).hexdigest(),'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed,'scopes':{scope:{'total':sum(r['scope']==scope for r in rows),'passed':sum(r['scope']==scope and r['ok'] for r in rows)}for scope in ['window','worker','cross-realm']}}))
    print('RESULTS legacy-codecs %s (%d/%d checks)'%('PASS' if passed==EXPECTED else 'FAIL',passed,EXPECTED));return 0 if passed==EXPECTED else 1

if __name__=='__main__':raise SystemExit(main())
