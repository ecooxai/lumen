#!/usr/bin/env python3
"""Native cloning of document identity, qualified names and detached attributes.

Focused regressions complement the unchanged upstream Node.cloneNode fixture.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

PAGE = r'''<!doctype html><html><body><script>
const checks=[];function check(name,fn){try{checks.push({name,passed:fn()===true});}catch(e){checks.push({name,passed:false,error:e.name+': '+e.message});}}
const HTML='http://www.w3.org/1999/xhtml',SVG='http://www.w3.org/2000/svg';
check('qualified HTML element clones preserve native name and interface',()=>{const e=document.createElementNS(HTML,'foo:div'),c=e.cloneNode();return c!==e&&c instanceof HTMLDivElement&&c.prefix==='foo'&&c.localName==='div'&&c.tagName==='FOO:DIV'&&c.namespaceURI===HTML;});
check('custom namespace element clones preserve the namespace identity',()=>{const e=document.createElementNS('urn:clone','p:MiXeD'),c=e.cloneNode();return c.prefix==='p'&&c.localName==='MiXeD'&&c.namespaceURI==='urn:clone'&&c.tagName==='p:MiXeD';});
check('SVG qualified clones preserve original localName casing',()=>{const e=document.createElementNS(SVG,'s:linearGradient'),c=e.cloneNode();return c.prefix==='s'&&c.localName==='linearGradient'&&c.tagName==='s:linearGradient'&&c.namespaceURI===SVG;});
check('public expando names cannot replace native qualified-name data',()=>{const e=document.createElementNS('urn:clone','p:Item');e.__pfx='evil';e.__nsu='urn:evil';e.__ln='Evil';const c=e.cloneNode();return e.prefix==='p'&&e.namespaceURI==='urn:clone'&&e.localName==='Item'&&c.tagName==='p:Item';});
check('deep element clones preserve descendant namespace data',()=>{const p=document.createElement('section');p.appendChild(document.createElementNS('urn:child','c:Leaf'));const c=p.cloneNode(true);return c.firstChild!==p.firstChild&&c.firstChild.prefix==='c'&&c.firstChild.localName==='Leaf'&&c.firstChild.namespaceURI==='urn:child'&&c.firstChild.ownerDocument===document;});
check('cloneNode keeps an inert source document as owner',()=>{const d=document.implementation.createDocument(null,'Root'),c=d.documentElement.cloneNode();return c.ownerDocument===d&&c.tagName==='Root';});
check('importNode assigns the explicit receiving document',()=>{const d=document.implementation.createDocument(null,'Root'),e=document.createElementNS('urn:clone','p:Item'),c=d.importNode(e,true);return c.ownerDocument===d&&e.ownerDocument===document&&c.prefix==='p'&&c.namespaceURI==='urn:clone';});
check('shallow XML document cloning creates another XMLDocument',()=>{const d=document.implementation.createDocument(null,'Root'),c=d.cloneNode();return c!==d&&c instanceof XMLDocument&&c.nodeType===9&&c.nodeName==='#document'&&c.ownerDocument===null&&c.childNodes.length===0;});
check('XML document clones preserve URL content type encoding and mode',()=>{const d=document.implementation.createDocument(null,null),c=d.cloneNode();return c.URL==='about:blank'&&c.documentURI==='about:blank'&&c.contentType==='application/xml'&&c.characterSet==='UTF-8'&&c.compatMode==='CSS1Compat';});
check('deep XML document clones rehome their entire subtree',()=>{const d=document.implementation.createDocument('urn:root','p:Root');d.documentElement.appendChild(d.createElementNS('urn:child','q:Child'));const c=d.cloneNode(true);return c.documentElement!==d.documentElement&&c.documentElement.ownerDocument===c&&c.documentElement.firstChild.ownerDocument===c&&c.documentElement.tagName==='p:Root'&&c.documentElement.firstChild.tagName==='q:Child';});
check('cloned XHTML documents keep their XML content type and casing',()=>{const d=document.implementation.createDocument(HTML,'html'),c=d.cloneNode(true);return c instanceof XMLDocument&&c.contentType==='application/xhtml+xml'&&c.documentElement.tagName==='html'&&c.createElement('MiXeD').tagName==='MiXeD'&&c.createElement('x').namespaceURI===HTML;});
check('shallow HTML document clones have no copied title tree',()=>{const d=document.implementation.createHTMLDocument('Original'),c=d.cloneNode();return c instanceof HTMLDocument&&c.nodeType===9&&c.title===''&&c.documentElement===null&&c.URL==='about:blank'&&c.contentType==='text/html';});
check('deep HTML document clones preserve content with independent nodes',()=>{const d=document.implementation.createHTMLDocument('Original');d.body.appendChild(d.createElement('div'));const c=d.cloneNode(true);return c.title==='Original'&&c.documentElement.ownerDocument===c&&c.body.firstChild.ownerDocument===c&&c.body.firstChild!==d.body.firstChild;});
check('detached attributes store their assigned value',()=>{const a=document.createAttribute('ClAsS');a.value='original';return a.name==='class'&&a.value==='original'&&a.ownerElement===null&&a.ownerDocument===document;});
check('detached attribute clone retains value and ownerDocument',()=>{const a=document.createAttribute('class');a.value='original';const c=a.cloneNode();return c!==a&&c instanceof Attr&&c.name==='class'&&c.value==='original'&&c.ownerDocument===document&&c.ownerElement===null;});
check('detached attribute clones remain independent after mutation',()=>{const a=document.createAttribute('class');a.value='first';const c=a.cloneNode();a.value='second';return a.value==='second'&&c.value==='first';});
check('attribute nodeValue and textContent setters write the value',()=>{const a=document.createAttribute('class');a.nodeValue='one';const one=a.value==='one';a.textContent='two';return one&&a.value==='two'&&a.nodeValue==='two';});
check('nullable attribute nodeValue and textContent clear the value',()=>{const a=document.createAttribute('class');a.value='a';a.nodeValue=null;const one=a.value==='';a.value='b';a.textContent=null;return one&&a.value==='';});
check('qualified attribute clones preserve namespace and prefix',()=>{const a=document.createAttributeNS('urn:attrs','p:ClAsS');a.value='value';const c=a.cloneNode();return c.name==='p:ClAsS'&&c.localName==='ClAsS'&&c.prefix==='p'&&c.namespaceURI==='urn:attrs'&&c.value==='value'&&c.ownerDocument===document;});
check('XML createAttribute preserves ASCII name casing',()=>{const d=document.implementation.createDocument(null,null),a=d.createAttribute('MiXeD');return a.name==='MiXeD'&&a.localName==='MiXeD'&&a.ownerDocument===d;});
check('createAttribute rejects invalid XML names',()=>{try{document.createAttribute('bad name');return false;}catch(e){return e.name==='InvalidCharacterError';}});
check('createAttributeNS rejects prefix without namespace',()=>{try{document.createAttributeNS(null,'p:name');return false;}catch(e){return e.name==='NamespaceError';}});
check('attribute construction remains a platform-only operation',()=>{try{new Attr();return false;}catch(e){return e instanceof TypeError;}});
check('attribute clone is detached from an attached original',()=>{const e=document.createElement('div');e.setAttribute('class','attached');const a=e.getAttributeNode('class'),c=a.cloneNode();e.setAttribute('class','changed');return a.value==='changed'&&c.value==='attached'&&c.ownerElement===null&&c.ownerDocument===document;});
check('Document constructor clones preserve their implemented interface',()=>{const d=new Document(),c=d.cloneNode();return c instanceof Document&&!(c instanceof XMLDocument)&&Object.getPrototypeOf(c)===Document.prototype;});
check('document clones ignore a page-defined prototype',()=>{const d=document.implementation.createDocument(null,null),p=Object.create(XMLDocument.prototype);Object.setPrototypeOf(d,p);const c=d.cloneNode();return c instanceof XMLDocument&&Object.getPrototypeOf(c)===XMLDocument.prototype;});
check('text clones preserve a detached XML owner document',()=>{const d=document.implementation.createDocument(null,null),t=d.createTextNode('text'),c=t.cloneNode();return c.ownerDocument===d&&c.data==='text';});
check('fragment clones preserve owner and descendant namespaces',()=>{const d=document.implementation.createDocument(null,null),f=d.createDocumentFragment();f.appendChild(d.createElementNS('urn:fragment','p:Leaf'));const c=f.cloneNode(true);return c.ownerDocument===d&&c.firstChild.ownerDocument===d&&c.firstChild.namespaceURI==='urn:fragment'&&c.firstChild.prefix==='p';});
check('shallow template clones have empty independent content',()=>{const t=document.createElement('template');t.content.appendChild(document.createElement('span'));const c=t.cloneNode();return c.content!==t.content&&c.content.childNodes.length===0&&t.content.childNodes.length===1;});
check('deep template clones preserve qualified descendant names',()=>{const t=document.createElement('template');t.content.appendChild(document.createElementNS('urn:template','p:Leaf'));const c=t.cloneNode(true);return c.content.firstChild!==t.content.firstChild&&c.content.firstChild.namespaceURI==='urn:template'&&c.content.firstChild.localName==='Leaf'&&c.content.firstChild.prefix==='p';});
check('attributes implement Node and remain childless roots',()=>{const a=document.createAttribute('class');return a instanceof Node&&a.parentNode===null&&a.firstChild===null&&a.childNodes.length===0&&a.getRootNode()===a;});
check('borrowed Node cloneNode supports attribute receivers',()=>{const a=document.createAttribute('class');a.value='value';const c=Node.prototype.cloneNode.call(a,true);return c instanceof Attr&&c.value==='value'&&c.ownerDocument===document;});
check('attribute access returns stable identity',()=>{const e=document.createElement('div');e.setAttribute('class','value');return e.getAttributeNode('class')===e.getAttributeNode('class')&&e.attributes.item(0)===e.getAttributeNode('class');});
check('removing an attribute returns and detaches the original object',()=>{const e=document.createElement('div');e.setAttribute('class','value');const a=e.getAttributeNode('class'),r=e.removeAttributeNode(a);a.value='detached';return r===a&&a.ownerElement===null&&a.value==='detached'&&a.ownerDocument===document&&!e.hasAttribute('class');});
check('importNode copies attributes into the receiving document',()=>{const d=document.implementation.createDocument(null,null),a=document.createAttributeNS('urn:attr','p:Value');a.value='value';const c=d.importNode(a);return c!==a&&c.ownerDocument===d&&c.ownerElement===null&&c.value==='value'&&c.prefix==='p'&&c.namespaceURI==='urn:attr';});
check('importNode rejects document nodes',()=>{try{document.importNode(document.implementation.createDocument(null,null),true);return false;}catch(e){return e.name==='NotSupportedError';}});
for(let index=0;index<checks.length;index++)console.log('CLONE_METADATA_CHECK '+JSON.stringify({index,...checks[index]}));
console.log('CLONE_METADATA_RESULTS '+JSON.stringify({passed:checks.filter(x=>x.passed).length,total:checks.length}));
document.documentElement.setAttribute('data-clone-fixture-done','1');
</script></body></html>'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',required=True,type=Path)
    args=parser.parse_args();binary=args.binary.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='lumen-clone-metadata-') as directory:
        page=Path(directory)/'clone.html';page.write_text(PAGE)
        env=dict(os.environ,LUMEN_PROFILE_DIR=directory,LUMEN_CONSOLE='1',JSRUN_UNTIL_ATTR='data-clone-fixture-done')
        result=subprocess.run([str(binary),str(page),'3000'],text=True,capture_output=True,env=env,timeout=8)
    sys.stdout.write(result.stdout);sys.stderr.write(result.stderr)
    transcript=result.stdout+result.stderr
    summaries=[json.loads(line.split('CLONE_METADATA_RESULTS ',1)[1]) for line in transcript.splitlines() if 'CLONE_METADATA_RESULTS ' in line]
    checks=[json.loads(line.split('CLONE_METADATA_CHECK ',1)[1]) for line in transcript.splitlines() if 'CLONE_METADATA_CHECK ' in line]
    if result.returncode or len(summaries)!=1 or len(checks)!=36 or sorted(x['index'] for x in checks)!=list(range(36)):
        print('CLONE_METADATA_INFRASTRUCTURE missing or duplicate fixture result');return 2
    report=summaries[0]
    if report['total']!=36 or report['passed']!=sum(x['passed'] is True for x in checks):return 2
    print(f"RESULTS clone-metadata {'PASS' if report['passed']==36 else 'FAIL'} ({report['passed']}/36 checks)")
    return 0 if report['passed']==36 else 1

if __name__=='__main__':raise SystemExit(main())
