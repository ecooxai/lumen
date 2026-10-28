#!/usr/bin/env python3
"""Real native iframe document MIME, namespace and casing regressions.

Serves bounded loopback responses and runs the actual V8/jsrun browser runtime.
The fixture does not change the pinned upstream WPT files or their denominator.
"""
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
(async () => {
 const checks = [];
 function check(name, fn) {
   try { checks.push({name, passed: fn() === true}); }
   catch (error) { checks.push({name, passed: false, error: error.name + ': ' + error.message}); }
 }
 async function frame(path) {
   const element = document.createElement('iframe');
   const ready = new Promise((resolve, reject) => {
     element.onload = () => resolve(element);
     element.onerror = () => reject(new Error('Frame load error: ' + path));
     setTimeout(() => reject(new Error('Frame load deadline: ' + path)), 6000);
   });
   element.src = path; document.body.appendChild(element);
   return await ready;
 }
 const xml = await frame('/fixture.xml'), xd = xml.contentDocument, xw = xml.contentWindow;
 check('XML frame uses the child realm XMLDocument interface', () => xd instanceof xw.XMLDocument);
 check('XML response preserves its content type', () => xd.contentType === 'application/xml');
 check('XML has one original root without HTML insertion', () => xd.documentElement.localName === 'Root' && xd.documentElement.textContent === 'Text');
 check('XML parser keeps descendant case and null namespace', () => xd.documentElement.firstElementChild.localName === 'MiXeD' && xd.documentElement.firstElementChild.namespaceURI === null);
 check('XML createElement preserves local and qualified name', () => { const e=xd.createElement('MiXeD'); return e.localName==='MiXeD' && e.tagName==='MiXeD' && e.namespaceURI===null && e instanceof xw.Element; });
 check('XML descendant ownerDocument is the browsing context document', () => xd.documentElement.firstElementChild.ownerDocument === xd);
 const xhtml = await frame('/fixture.xhtml'), hd = xhtml.contentDocument, hw = xhtml.contentWindow;
 check('XHTML frame is an XMLDocument in its child realm', () => hd instanceof hw.XMLDocument);
 check('XHTML retains its MIME type', () => hd.contentType === 'application/xhtml+xml');
 check('XHTML parses the declared HTML namespace without trailing text', () => hd.documentElement.namespaceURI==='http://www.w3.org/1999/xhtml' && hd.documentElement.textContent==='TitleBody');
 check('XHTML createElement uses HTML namespace with original case', () => { const e=hd.createElement('MiXeD'); return e.localName==='MiXeD' && e.tagName==='MiXeD' && e.namespaceURI==='http://www.w3.org/1999/xhtml' && e instanceof hw.Element; });
 check('XHTML parsed HTML elements keep XML tag casing', () => hd.body.tagName==='body' && hd.body.firstElementChild.tagName==='div');
 const html = await frame('/fixture.html'), dd=html.contentDocument;
 check('HTML response keeps HTMLDocument and normal HTML parsing', () => dd instanceof html.contentWindow.HTMLDocument && dd.documentElement.localName==='html' && dd.body.textContent==='HTML');
 check('HTML createElement keeps ASCII casing rules', () => { const e=dd.createElement('MiXeD'); return e.localName==='mixed' && e.tagName==='MIXED' && e.namespaceURI==='http://www.w3.org/1999/xhtml'; });
 const broken = await frame('/broken.xml'), bd=broken.contentDocument;
 check('Malformed XML yields an explicit parser error', () => bd.contentType==='application/xml' && !!bd.querySelector('parsererror'));
 const custom = await frame('/custom.xml'), cd=custom.contentDocument;
 check('SVG XML MIME keeps its document root and namespace', () => cd.contentType==='image/svg+xml' && cd.documentElement.localName==='svg' && cd.documentElement.namespaceURI==='http://www.w3.org/2000/svg' && cd.documentElement.textContent==='SVG');
 const utf16 = await frame('/utf16.xml'), ud=utf16.contentDocument;
 check('XML UTF-16 BOM is decoded before parsing', () => ud.documentElement.localName==='Root' && ud.documentElement.textContent==='Snow \u2603');
 check('XML reports the actual BOM-selected encoding', () => ud.characterSet==='UTF-16LE');
 check('DOMParser XHTML uses XML document and casing rules', () => { const d=new DOMParser().parseFromString('<html xmlns="http://www.w3.org/1999/xhtml"><body/></html>','application/xhtml+xml'); return d instanceof XMLDocument && d.documentElement.tagName==='html' && d.createElement('MiXeD').tagName==='MiXeD'; });
 globalThis.domParserScriptCount=0;
 const inert = new DOMParser().parseFromString('<html xmlns="http://www.w3.org/1999/xhtml"><script>globalThis.domParserScriptCount++;</scr'+'ipt></html>','application/xhtml+xml');
 check('DOMParser XML never executes scripts during parsing', () => inert.documentElement.localName==='html' && domParserScriptCount===0);
 check('A DOMParser-created script remains inert after explicit insertion', () => { const script=inert.documentElement.firstElementChild; document.body.appendChild(script); return script.localName==='script' && domParserScriptCount===0; });
 const scripted=await frame('/scripted.xhtml');
 check('Navigated XHTML executes complete scripts after the document tree exists', () => scripted.contentWindow.xmlScriptResult==='Ready');
 const malformed=await frame('/malformed-script.xhtml');
 check('A completed XHTML script remains executed before a later XML error', () => !!malformed.contentDocument.querySelector('parsererror') && malformed.contentWindow.partialScriptRan===true);
 const blocked=await frame('/blocked-script.xhtml');
 check('XHTML script execution still obeys its response CSP', () => blocked.contentDocument.contentType==='application/xhtml+xml' && blocked.contentWindow.xmlScriptResult===undefined);
 for (const element of [xml,xhtml,html,broken,custom,utf16,scripted,malformed,blocked]) element.remove();
 check('Removed frames release their public browsing context association', () => [xml,xhtml,html,broken,custom,utf16].every(e=>e.contentWindow===null && e.contentDocument===null));
 for (let index=0; index<checks.length; index++) console.log('XML_FRAME_CHECK '+JSON.stringify({index,...checks[index]}));
 console.log('XML_FRAME_RESULTS '+JSON.stringify({passed:checks.filter(c=>c.passed).length, total:checks.length}));
 document.documentElement.setAttribute('data-xml-fixture-done','1');
})().catch(error => {
 console.log('XML_FRAME_FATAL '+error.name+': '+error.message);
 document.documentElement.setAttribute('data-xml-fixture-done','1');
});
</script></body></html>'''

RESPONSES = {
    '/': ('text/html; charset=utf-8', PAGE.encode()),
    '/fixture.xml': ('application/xml', b'<Root><MiXeD>Text</MiXeD></Root>\n'),
    '/fixture.xhtml': ('application/xhtml+xml; charset=utf-8', b'<!DOCTYPE html><html xmlns="http://www.w3.org/1999/xhtml"><head><title>Title</title></head><body><div>Body</div></body></html>\n'),
    '/fixture.html': ('text/html; charset=utf-8', b'<!doctype html><html><body>HTML</body></html>'),
    '/broken.xml': ('application/xml', b'<root><wrong></root>'),
    '/custom.xml': ('image/svg+xml; charset=UTF-8', b'<svg xmlns="http://www.w3.org/2000/svg"><title>SVG</title></svg>\n'),
    '/utf16.xml': ('application/xml', b'\xff\xfe' + '<Root>Snow \u2603</Root>'.encode('utf-16-le')),
    '/scripted.xhtml': ('application/xhtml+xml', b'<html xmlns="http://www.w3.org/1999/xhtml"><head/><body>Ready</body><script>globalThis.xmlScriptResult=document.body.textContent;</script></html>'),
    '/malformed-script.xhtml': ('application/xhtml+xml', b'<html xmlns="http://www.w3.org/1999/xhtml"><head/><body><script>globalThis.partialScriptRan=true;</script></wrong></html>'),
    '/blocked-script.xhtml': ('application/xhtml+xml', b'<html xmlns="http://www.w3.org/1999/xhtml"><head/><body>Ready</body><script>globalThis.xmlScriptResult=document.body.textContent;</script></html>'),
}


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        response = RESPONSES.get(self.path)
        if response is None:
            self.send_error(404)
            return
        content_type, body = response
        self.send_response(200)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        if self.path == '/blocked-script.xhtml':
            self.send_header('Content-Security-Policy', "script-src 'none'")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, _format, *_args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file():
        parser.error('The actual jsrun executable is required')
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='lumen-xml-frame-profile-') as profile:
            env = dict(os.environ, LUMEN_PROFILE_DIR=profile, LUMEN_CONSOLE='1', JSRUN_UNTIL_ATTR='data-xml-fixture-done')
            result = subprocess.run([str(binary), f'http://127.0.0.1:{server.server_port}/', '12000'],
                                    text=True, capture_output=True, env=env, timeout=20)
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        transcript = result.stdout + result.stderr
        lines = [line.split('XML_FRAME_RESULTS ', 1)[1] for line in transcript.splitlines()
                 if 'XML_FRAME_RESULTS ' in line]
        if result.returncode or len(lines) != 1:
            return 1
        report = json.loads(lines[0])
        checks = [json.loads(line.split('XML_FRAME_CHECK ', 1)[1]) for line in transcript.splitlines()
                  if 'XML_FRAME_CHECK ' in line]
        if len(checks) != 24 or sorted(item['index'] for item in checks) != list(range(24)):
            print('XML_FRAME_INFRASTRUCTURE missing or duplicate bounded case records')
            return 1
        if sum(item['passed'] is True for item in checks) != report['passed']:
            print('XML_FRAME_INFRASTRUCTURE aggregate count differs from per-case records')
            return 1
        print(f"RESULTS xml-frame-documents {'PASS' if report['passed']==report['total'] else 'FAIL'} ({report['passed']}/{report['total']} checks)")
        return 0 if report['total'] == 24 and report['passed'] == 24 else 1
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


if __name__ == '__main__':
    raise SystemExit(main())
