#!/usr/bin/env python3
"""Native HTML declarative shadow parsing and detached document writer controls."""
import argparse,hashlib,http.server,json,os,subprocess,tempfile,threading
from pathlib import Path

HTML=r'''<!doctype html><meta charset=utf-8><title>Declarative shadow document regression</title><body>
<div id="network-open"><template shadowrootmode="open"><b>alpha</b></template><i>light</i></div>
<div id="network-closed"><template shadowrootmode="closed"><b>secret</b></template></div>
<div id="network-invalid"><template shadowrootmode="invalid"><b>ordinary</b></template></div>
<a id="invalid-host"><template shadowrootmode="open">ordinary</template></a>
<div id="duplicate"><template shadowrootmode="open">first</template><template shadowrootmode="closed">second</template></div>
<div id="nested"><template shadowrootmode="open"><section><template shadowrootmode="open"><b>nested</b></template></section></template></div>
<div id="uppercase"><template shadowrootmode="OPEN">upper</template></div>
<div id="spaced"><template shadowrootmode=" open ">ordinary</template></div>
<template id="ordinary"><b>inert</b></template>
<div id="options"><template shadowrootmode="open" shadowrootdelegatesfocus shadowrootclonable shadowrootserializable><b>copy</b></template></div>
<script>
'use strict';
const rows=[];
const eq=(actual,expected)=>{if(actual!==expected)throw new Error('expected '+JSON.stringify(expected)+', got '+JSON.stringify(actual));};
const ok=value=>{if(!value)throw new Error('expected truthy value');};
const throws=(name,fn)=>{try{fn();}catch(e){eq(e.name,name);return;}throw new Error('expected '+name);};
const check=(name,fn)=>{let row={i:rows.length,name,ok:true};try{fn();}catch(e){row.ok=false;row.error=e.name+': '+e.message;}rows.push(row);console.log('DSD_CHECK '+JSON.stringify(row));};
const host=document.getElementById('network-open');
check('network open root content and native ShadowRoot brand',()=>{ok(host.shadowRoot instanceof ShadowRoot);eq(host.shadowRoot.textContent,'alpha');eq(host.shadowRoot.host,host);eq(host.shadowRoot.mode,'open');});
check('declarative template is absent from light DOM',()=>{eq(host.querySelector('template'),null);eq(host.textContent,'light');});
check('shadow descendants retain owner document and connectivity',()=>{eq(host.shadowRoot.firstChild.ownerDocument,document);eq(host.shadowRoot.firstChild.isConnected,true);});
check('closed root rejects a conflicting imperative mode',()=>{const h=document.getElementById('network-closed');eq(h.shadowRoot,null);throws('NotSupportedError',()=>h.attachShadow({mode:'open'}));});
check('invalid mode remains ordinary inert template',()=>{const h=document.getElementById('network-invalid');eq(h.shadowRoot,null);eq(h.firstChild.content.textContent,'ordinary');});
check('invalid host remains ordinary inert template',()=>{const h=document.getElementById('invalid-host');eq(h.shadowRoot,null);eq(h.firstChild.content.textContent,'ordinary');});
check('second declarative template remains light template',()=>{const h=document.getElementById('duplicate');eq(h.shadowRoot.textContent,'first');eq(h.querySelectorAll('template').length,1);eq(h.querySelector('template').content.textContent,'second');});
check('nested declarative shadow roots use separate hosts',()=>{const r=document.getElementById('nested').shadowRoot;eq(r.firstChild.shadowRoot.textContent,'nested');eq(r.firstChild.shadowRoot.host,r.firstChild);});
check('enumerated shadow mode is ASCII case insensitive',()=>eq(document.getElementById('uppercase').shadowRoot.textContent,'upper'));
check('shadow mode whitespace is not stripped',()=>eq(document.getElementById('spaced').shadowRoot,null));
check('ordinary template stays inert with a separate owner',()=>{const t=document.getElementById('ordinary');eq(t.content.textContent,'inert');ok(t.content.ownerDocument!==document);eq(t.content.firstChild.isConnected,false);});
check('DOMParser keeps declarative shadow parsing disabled',()=>{const d=new DOMParser().parseFromString('<div><template shadowrootmode=open>x</template></div>','text/html');eq(d.querySelector('div').shadowRoot,null);eq(d.querySelector('template').content.textContent,'x');});
check('innerHTML keeps declarative shadow parsing disabled',()=>{const h=document.createElement('div');h.innerHTML='<div><template shadowrootmode=open>x</template></div>';eq(h.firstChild.shadowRoot,null);ok(h.querySelector('template'));});
check('fragment parsing does not attach a root to context',()=>{const h=document.createElement('div');document.body.appendChild(h);h.insertAdjacentHTML('beforeend','<template shadowrootmode=open>x</template>');eq(h.shadowRoot,null);ok(h.firstChild.content);h.remove();});
check('shallow document clone preserves allow-declarative policy',()=>{const d=document.cloneNode(false);d.write('<div><template shadowrootmode=open><b>clone</b></template></div>');d.close();eq(d.querySelector('div').shadowRoot.textContent,'clone');});
check('deep document clone preserves allow-declarative policy',()=>{const d=document.cloneNode(true);d.open();d.write('<div><template shadowrootmode=open>deep</template></div>');d.close();eq(d.querySelector('div').shadowRoot.textContent,'deep');});
check('cloned detached document keeps copied policy again',()=>{const d=document.cloneNode(false).cloneNode(false);d.write('<div><template shadowrootmode=open>again</template></div>');d.close();eq(d.querySelector('div').shadowRoot.textContent,'again');});
check('new HTML document write keeps declarative parsing disabled',()=>{const d=document.implementation.createHTMLDocument();d.write('<div><template shadowrootmode=open>inert</template></div>');d.close();eq(d.querySelector('div').shadowRoot,null);eq(d.querySelector('template').content.textContent,'inert');});
check('DOMParser document clone preserves disabled policy',()=>{const d=new DOMParser().parseFromString('<p>old</p>','text/html').cloneNode(false);d.write('<div><template shadowrootmode=open>inert</template></div>');d.close();eq(d.querySelector('div').shadowRoot,null);ok(d.querySelector('template'));});
check('detached writes do not insert into active document',()=>{const before=document.body.childNodes.length;const d=document.cloneNode(false);d.write('<p id=detached-only>isolated</p>');d.close();eq(document.getElementById('detached-only'),null);eq(document.body.childNodes.length,before);eq(d.querySelector('p').textContent,'isolated');});
check('split writes retain tokenizer state across tags',()=>{const d=document.cloneNode(false);d.write('<di');d.write('v><template shadowrootmode="op');d.write('en"><b>spl');d.write('it</b></template></div>');d.close();eq(d.querySelector('div').shadowRoot.textContent,'split');});
check('two detached parsers can interleave independently',()=>{const a=document.cloneNode(false),b=document.cloneNode(false);a.write('<div><template shadowrootmode=open>A');b.write('<div><template shadowrootmode=open>B');a.write('1</template></div>');b.write('2</template></div>');a.close();b.close();eq(a.querySelector('div').shadowRoot.textContent,'A1');eq(b.querySelector('div').shadowRoot.textContent,'B2');});
check('open detaches old nodes without invalidating held wrappers',()=>{const d=document.cloneNode(false);d.write('<p>old</p>');d.close();const old=d.querySelector('p');d.open();d.write('<b>new</b>');d.close();eq(old.textContent,'old');eq(old.isConnected,false);eq(old.ownerDocument,d);eq(d.querySelector('p'),null);eq(d.querySelector('b').textContent,'new');});
check('open disposes an unfinished parser before replacement',()=>{const d=document.cloneNode(false);d.write('<div><template shadowrootmode=open>unfinished');d.open();d.write('<p>replacement</p>');d.close();eq(d.querySelector('div'),null);eq(d.querySelector('p').textContent,'replacement');});
check('intervening DOM removal leaves parser state alive',()=>{const d=document.cloneNode(false);d.write('<div><span>before');d.body.textContent='';d.write('after</span></div><p>end</p>');d.close();eq(d.querySelector('p').textContent,'end');eq(d.querySelector('span'),null);});
check('detached Unicode writes preserve leading FEFF',()=>{const d=document.cloneNode(false);d.write('\ufeff<b>unicode</b>');d.close();eq(d.body.textContent,'\ufeffunicode');});
check('detached scripts do not execute in the active realm',()=>{window.__dsdDetachedExecution=0;const d=document.cloneNode(false);d.write('<script>window.__dsdDetachedExecution++<\/script><p>safe</p>');d.close();eq(window.__dsdDetachedExecution,0);eq(d.querySelector('p').textContent,'safe');});
check('close handles an unclosed declarative template',()=>{const d=document.cloneNode(false);d.write('<div><template shadowrootmode=open><b>end');d.close();eq(d.querySelector('div').shadowRoot.textContent,'end');});
check('declarative attachment options are native state',()=>{const r=document.getElementById('options').shadowRoot;eq(r.delegatesFocus,true);eq(r.clonable,true);eq(r.serializable,true);eq(r.slotAssignment,'named');});
check('clonable root children copy even on shallow host clone',()=>{const h=document.getElementById('options');const c=h.cloneNode(false);eq(c.childNodes.length,0);eq(c.shadowRoot.textContent,'copy');eq(c.shadowRoot.host,c);eq(c.shadowRoot.clonable,true);ok(c.shadowRoot!==h.shadowRoot);});
check('matching imperative mode reuses and clears declarative root',()=>{const h=document.getElementById('uppercase'),before=h.shadowRoot;const after=h.attachShadow({mode:'open'});eq(after,before);eq(after.childNodes.length,0);throws('NotSupportedError',()=>h.attachShadow({mode:'open'}));});
check('conflicting imperative mode leaves declarative content intact',()=>{throws('NotSupportedError',()=>host.attachShadow({mode:'closed'}));eq(host.shadowRoot.textContent,'alpha');});
check('ordinary imperative open and closed roots remain supported',()=>{const a=document.createElement('div'),b=document.createElement('span');const ar=a.attachShadow({mode:'open'}),br=b.attachShadow({mode:'closed'});eq(a.shadowRoot,ar);eq(b.shadowRoot,null);eq(br.mode,'closed');eq(ar.ownerDocument,document);});
check('imperative attachment rejects missing or invalid mode',()=>{throws('TypeError',()=>document.createElement('div').attachShadow({}));throws('TypeError',()=>document.createElement('div').attachShadow({mode:'bad'}));});
check('imperative attachment rejects invalid HTML and foreign hosts',()=>{throws('NotSupportedError',()=>document.createElement('a').attachShadow({mode:'open'}));throws('NotSupportedError',()=>document.createElementNS('http://www.w3.org/2000/svg','g').attachShadow({mode:'open'}));});
check('unfinished detached parser remains owned until realm teardown',()=>{const d=document.cloneNode(false);d.write('<div><template shadowrootmode=open><b>retained');eq(d.querySelector('div').shadowRoot.textContent,'retained');});
const passed=rows.filter(x=>x.ok).length;console.log('DSD_RESULTS '+JSON.stringify({total:rows.length,passed,failed:rows.length-passed}));document.documentElement.setAttribute('data-dsd-done','1');
</script>'''
EXPECTED=36
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        data=HTML.encode();self.send_response(200);self.send_header('Content-Type','text/html;charset=utf-8');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    def log_message(self,*args):pass
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--binary',type=Path,required=True);a=parser.parse_args();binary=a.binary.resolve()
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='lume-dsd-') as profile:
            env={k:v for k,v in os.environ.items() if not k.startswith('LUMEN_')};env.update(LUMEN_PROFILE_DIR=profile,LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-dsd-done')
            run=subprocess.run([str(binary),'http://127.0.0.1:%d/'%server.server_port,'20000'],capture_output=True,text=True,env=env,timeout=25)
    finally:server.shutdown();server.server_close();thread.join(timeout=2)
    print(run.stdout,end='');print(run.stderr,end='');rows=[];summaries=[]
    for line in (run.stdout+'\n'+run.stderr).splitlines():
        for marker,target in [('DSD_CHECK ',rows),('DSD_RESULTS ',summaries)]:
            at=line.find(marker)
            if at>=0:target.append(json.loads(line[at+len(marker):]))
    if run.returncode or len(rows)!=EXPECTED or len(summaries)!=1 or [r['i'] for r in rows]!=list(range(EXPECTED)):
        print('DSD_INFRA '+json.dumps({'exit':run.returncode,'checks':len(rows),'summaries':summaries}));return 2
    passed=sum(r['ok'] is True for r in rows)
    if summaries[0]!={'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed}:raise RuntimeError('Inconsistent DSD regression receipt')
    print('DSD_RECEIPT '+json.dumps({'binarySHA256':hashlib.sha256(binary.read_bytes()).hexdigest(),'fixtureSHA256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'htmlSHA256':hashlib.sha256(HTML.encode()).hexdigest(),'total':EXPECTED,'passed':passed,'failed':EXPECTED-passed}))
    return 0 if passed==EXPECTED else 1
if __name__=='__main__':raise SystemExit(main())
