#!/usr/bin/env python3
"""Exercise real cross-frame template document ownership and realm retirement."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

PAGE = r'''<!doctype html><html><body><script>
(async()=>{
const checks=[];function check(name,fn){try{checks.push({name,passed:fn()===true});}catch(e){checks.push({name,passed:false,error:e.name+': '+e.message});}}
const frame=document.createElement('iframe'),ready=new Promise((resolve,reject)=>{frame.onload=resolve;frame.onerror=()=>reject(Error('frame load'));setTimeout(()=>reject(Error('frame deadline')),4000)});
frame.src='/owner.html';document.body.appendChild(frame);await ready;
const childWindow=frame.contentWindow,childDocument=frame.contentDocument,getOwner=Object.getOwnPropertyDescriptor(Node.prototype,'ownerDocument').get;
const template=childDocument.createElement('template'),content=template.content,leaf=content.appendChild(childDocument.createElement('span'));
const inertOwner=getOwner.call(content);
check('child template uses an inert owner distinct from its live document',()=>inertOwner!==childDocument&&inertOwner.documentElement===null);
check('first parent getter access preserves the owning frame document interface',()=>inertOwner instanceof childWindow.HTMLDocument&&!(inertOwner instanceof HTMLDocument));
check('parent and child getters return the same inert document wrapper',()=>content.ownerDocument===inertOwner&&childWindow.Object.getOwnPropertyDescriptor(childWindow.Node.prototype,'ownerDocument').get.call(content)===inertOwner);
const owned=childDocument.implementation.createHTMLDocument('owned'),ownedTemplate=owned.createElement('template'),ownedContent=ownedTemplate.content,ownedInert=getOwner.call(ownedContent);
check('auxiliary child documents own a separate inert template document',()=>ownedInert!==owned&&ownedInert!==inertOwner);
check('auxiliary inert document first accessed in parent keeps child realm',()=>ownedInert instanceof childWindow.HTMLDocument&&!(ownedInert instanceof HTMLDocument));
const expected=document.createElement('template').content.ownerDocument;
document.adoptNode(template);
check('adopting child template moves host into receiving document',()=>template.ownerDocument===document);
check('adopting child template redirects content to receiver inert owner',()=>content.ownerDocument===expected&&content.ownerDocument!==inertOwner);
check('adopted template descendants follow the new inert owner',()=>leaf.ownerDocument===expected&&content.firstChild===leaf);
const separate=document.implementation.createHTMLDocument('separate');
separate.adoptNode(ownedContent);
check('direct content adoption does not move the template host',()=>ownedContent.ownerDocument===separate&&ownedTemplate.ownerDocument===owned);
owned.adoptNode(ownedTemplate);
check('same-document host adoption does not repeat template adopting steps',()=>ownedContent.ownerDocument===separate);
frame.remove();
await new Promise(resolve=>setTimeout(resolve,10));
check('removed frame releases public window association',()=>frame.contentWindow===null&&frame.contentDocument===null);
check('retained original inert document remains readable after retirement',()=>inertOwner.URL==='about:blank'&&inertOwner.nodeType===9&&inertOwner.documentElement===null);
check('retained auxiliary inert document remains readable after retirement',()=>ownedInert.URL==='about:blank'&&ownedInert.nodeType===9&&ownedInert.documentElement===null);
check('retired frame inert document can create an owned node',()=>{const n=inertOwner.createElement('after-retirement');return n.ownerDocument===inertOwner&&n.localName==='after-retirement'});
check('retired auxiliary inert document can create an owned node',()=>{const n=ownedInert.createElement('after-auxiliary-retirement');return n.ownerDocument===ownedInert});
check('adopted template keeps its receiver owner after frame retirement',()=>template.ownerDocument===document&&content.ownerDocument===expected&&leaf.ownerDocument===expected);
check('retained old inert document can be cloned after retirement',()=>{const c=Node.prototype.cloneNode.call(inertOwner);return c!==inertOwner&&c.URL==='about:blank'&&c.documentElement===null});
check('import of adopted content keeps the explicit document',()=>{const c=document.importNode(content,true);return c.ownerDocument===document&&c.firstChild.ownerDocument===document&&c.firstChild.localName==='span'});
for(let index=0;index<checks.length;index++)console.log('TEMPLATE_FRAME_CHECK '+JSON.stringify({index,...checks[index]}));
console.log('TEMPLATE_FRAME_RESULTS '+JSON.stringify({passed:checks.filter(x=>x.passed).length,total:checks.length}));
document.documentElement.setAttribute('data-template-frame-done','1');
})().catch(e=>{console.log('TEMPLATE_FRAME_FATAL '+e.name+': '+e.message);document.documentElement.setAttribute('data-template-frame-done','1')});
</script></body></html>'''
EXPECTED = 18


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        body = (PAGE if self.path == '/' else '<!doctype html><html><body>Child document</body></html>').encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='lumen-template-frame-') as directory:
            env = {k: v for k, v in os.environ.items() if not k.startswith('LUMEN_')}
            env.update(LUMEN_PROFILE_DIR=directory, LUMEN_CONSOLE='1', JSRUN_UNTIL_ATTR='data-template-frame-done')
            result = subprocess.run([str(binary), f'http://127.0.0.1:{server.server_port}/', '7000'], text=True, capture_output=True, env=env, timeout=12)
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        transcript = result.stdout + result.stderr
        summaries = [json.loads(line.split('TEMPLATE_FRAME_RESULTS ', 1)[1]) for line in transcript.splitlines() if 'TEMPLATE_FRAME_RESULTS ' in line]
        checks = [json.loads(line.split('TEMPLATE_FRAME_CHECK ', 1)[1]) for line in transcript.splitlines() if 'TEMPLATE_FRAME_CHECK ' in line]
        if result.returncode or len(summaries) != 1 or len(checks) != EXPECTED or sorted(x['index'] for x in checks) != list(range(EXPECTED)):
            print('TEMPLATE_FRAME_INFRASTRUCTURE missing or duplicate result')
            return 2
        report = summaries[0]
        if report['total'] != EXPECTED or report['passed'] != sum(x['passed'] is True for x in checks):
            return 2
        print(f"RESULTS template-frame-ownership {'PASS' if report['passed'] == EXPECTED else 'FAIL'} ({report['passed']}/{EXPECTED} checks)")
        return 0 if report['passed'] == EXPECTED else 1
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


if __name__ == '__main__':
    raise SystemExit(main())
