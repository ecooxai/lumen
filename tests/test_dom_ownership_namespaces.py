#!/usr/bin/env python3
"""DOM adoption, qualified lookup and parsed foreign attribute regression cases.

These are targeted regressions, not a substitute for the frozen upstream WPT.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

PAGE = r'''<!doctype html><html><body><x<>parsed</x<>
<svg id="foreign" xmlns:xlink="http://www.w3.org/1999/xlink" xml:lang="en"><use xlink:href="#symbol" xlink:unknown="plain"></use><linearGradient id="gradient"></linearGradient></svg>
<div id="html-xmlns" xmlns:xlink="plain"></div><script>
const checks=[];function check(name,fn){try{checks.push({name,passed:fn()===true});}catch(e){checks.push({name,passed:false,error:e.name+': '+e.message});}}
const inert=()=>document.implementation.createHTMLDocument(''), HTML='http://www.w3.org/1999/xhtml',SVG='http://www.w3.org/2000/svg',XML='http://www.w3.org/XML/1998/namespace',XMLNS='http://www.w3.org/2000/xmlns/',XLINK='http://www.w3.org/1999/xlink';
check('adoptNode rejects a closed shadow root',()=>{const r=document.createElement('div').attachShadow({mode:'closed'});try{inert().adoptNode(r);return false;}catch(e){return e.name==='HierarchyRequestError'&&r.ownerDocument===document;}});
check('adoptNode rejects an open shadow root',()=>{const r=document.createElement('div').attachShadow({mode:'open'});try{inert().adoptNode(r);return false;}catch(e){return e.name==='HierarchyRequestError';}});
check('plain fragment adoption moves descendants',()=>{const f=document.createDocumentFragment(),n=f.appendChild(document.createElement('span')),d=inert();return d.adoptNode(f)===f&&f.ownerDocument===d&&n.ownerDocument===d&&f.firstChild===n;});
check('direct template content adoption leaves its host alone',()=>{const t=document.createElement('template'),n=t.content.appendChild(document.createElement('span')),d=inert();return d.adoptNode(t.content)===t.content&&t.content.ownerDocument===d&&n.ownerDocument===d&&t.ownerDocument===document;});
check('direct template content adoption can target a browsing document',()=>{const d=inert(),t=d.createElement('template'),n=t.content.appendChild(d.createElement('span'));return document.adoptNode(t.content)===t.content&&t.content.ownerDocument===document&&n.ownerDocument===document;});
check('template adoption updates content and descendants',()=>{const d=inert(),t=d.createElement('template'),n=t.content.appendChild(d.createElement('span')),old=t.content.ownerDocument,target=inert();return target.adoptNode(t)===t&&t.ownerDocument===target&&t.content.ownerDocument!==old&&n.ownerDocument===t.content.ownerDocument;});
check('nested template adoption uses one inert target owner',()=>{const d=inert(),a=d.createElement('template'),b=d.createElement('template'),n=b.content.appendChild(d.createElement('span'));a.content.appendChild(b);const target=inert();target.adoptNode(a);return a.ownerDocument===target&&b.ownerDocument===a.content.ownerDocument&&b.content.ownerDocument===a.content.ownerDocument&&n.ownerDocument===a.content.ownerDocument;});
check('fragment append adopts children without adopting fragment',()=>{const d=inert(),t=d.createElement('template'),n=t.content.appendChild(d.createElement('span')),old=t.content.ownerDocument,p=document.createElement('div');p.appendChild(t.content);return t.content.ownerDocument===old&&t.content.childNodes.length===0&&n.ownerDocument===document&&n.parentNode===p;});
check('fragment insertBefore preserves its own document',()=>{const d=inert(),f=d.createDocumentFragment(),n=f.appendChild(d.createElement('span')),p=document.createElement('div'),ref=p.appendChild(document.createElement('b'));p.insertBefore(f,ref);return f.ownerDocument===d&&n.ownerDocument===document&&p.firstChild===n&&n.nextSibling===ref;});
check('adopting a shadow host updates closed shadow descendants',()=>{const host=document.createElement('div'),r=host.attachShadow({mode:'closed'}),n=r.appendChild(document.createElement('span')),d=inert();d.adoptNode(host);return host.ownerDocument===d&&r.ownerDocument===d&&n.ownerDocument===d;});
check('template content is owned by an inert document',()=>{const d=inert(),t=d.createElement('template');return t.content.ownerDocument!==d&&t.content.ownerDocument.URL==='about:blank'&&t.content.ownerDocument.documentElement===null;});
check('templates in one document share their inert owner',()=>{const d=inert(),a=d.createElement('template'),b=d.createElement('template');return a.content.ownerDocument===b.content.ownerDocument&&a.content.ownerDocument!==d;});
check('different documents have different inert template owners',()=>{const a=inert(),b=inert();return a.createElement('template').content.ownerDocument!==b.createElement('template').content.ownerDocument;});
check('nested templates reuse an existing inert owner',()=>{const t=document.createElement('template'),owner=t.content.ownerDocument,nested=owner.createElement('template');return nested.content.ownerDocument===owner;});
check('deep template clones preserve inert content ownership',()=>{const d=inert(),t=d.createElement('template'),n=t.content.appendChild(d.createElement('span')),c=t.cloneNode(true);return c.ownerDocument===d&&c.content.ownerDocument===t.content.ownerDocument&&c.content.firstChild.ownerDocument===c.content.ownerDocument&&c.content.firstChild!==n;});
check('shallow template clones have empty inert content',()=>{const d=inert(),t=d.createElement('template');t.content.appendChild(d.createElement('span'));const c=t.cloneNode();return c.content.childNodes.length===0&&c.content.ownerDocument===t.content.ownerDocument&&c.ownerDocument===d;});
check('importing a template uses the destination inert owner',()=>{const a=inert(),b=inert(),t=a.createElement('template');t.content.appendChild(a.createElement('span'));const c=b.importNode(t,true),expected=b.createElement('template').content.ownerDocument;return c.ownerDocument===b&&c.content.ownerDocument===expected&&c.content.firstChild.ownerDocument===expected&&expected!==t.content.ownerDocument;});
check('template innerHTML uses the content owner document',()=>{const d=inert(),t=d.createElement('template');t.innerHTML='<span><b>inside</b></span>';return t.content.firstChild.ownerDocument===t.content.ownerDocument&&t.content.firstChild.firstChild.ownerDocument===t.content.ownerDocument&&t.content.ownerDocument!==d;});
check('adoptNode supports a detached Attr',()=>{const d=inert(),a=document.createAttributeNS('urn:test','p:value');a.value='kept';return d.adoptNode(a)===a&&a.ownerDocument===d&&a.ownerElement===null&&a.prefix==='p'&&a.value==='kept';});
check('adopting an attached Attr detaches its exact identity',()=>{const el=document.createElement('div');el.setAttribute('data-test','kept');const a=el.getAttributeNode('data-test'),d=inert();return d.adoptNode(a)===a&&a.ownerDocument===d&&a.ownerElement===null&&!el.hasAttribute('data-test')&&a.value==='kept';});
check('adoptNode checks its Document receiver',()=>{try{Document.prototype.adoptNode.call(document.body,document.createElement('span'));return false;}catch(e){return e instanceof TypeError;}});
check('parsed non-CSS tag names retain exact lookup identity',()=>{const a=document.getElementsByTagName('x<');return a.length===1&&a[0].localName==='x<'&&a[0].textContent==='parsed'&&document.getElementsByTagName('x').length===0;});
check('getElementsByTagName preserves qualified prefixes',()=>{const p=document.createElement('div'),e=document.createElementNS('urn:q','p:Item');p.appendChild(e);return p.getElementsByTagName('p:Item')[0]===e&&p.getElementsByTagName('Item').length===0;});
check('HTML tag lookup folds ASCII names',()=>{const p=document.createElement('div'),e=p.appendChild(document.createElement('span'));return p.getElementsByTagName('SpAn')[0]===e;});
check('foreign tag lookup preserves case',()=>{const p=document.createElement('div'),e=p.appendChild(document.createElementNS(SVG,'linearGradient'));return p.getElementsByTagName('linearGradient')[0]===e&&p.getElementsByTagName('lineargradient').length===0;});
check('wildcard tag lookup includes descendants only',()=>{const p=document.createElement('div'),e=p.appendChild(document.createElement('span'));e.appendChild(document.createElement('b'));const c=p.getElementsByTagName('*');return c.length===2&&c[0]===e&&c[1].localName==='b';});
check('tag lookup returns a live HTMLCollection',()=>{const p=document.createElement('div'),c=p.getElementsByTagName('span'),e=document.createElement('span');const empty=c.length===0;p.appendChild(e);const added=c[0]===e&&c.length===1;e.remove();return c instanceof HTMLCollection&&empty&&added&&c.length===0;});
check('empty tag name matches no elements',()=>document.getElementsByTagName('').length===0);
check('document tag lookup honors its own tree',()=>{const d=inert(),e=d.body.appendChild(d.createElement('unique-for-other-document'));return d.getElementsByTagName('unique-for-other-document')[0]===e&&document.getElementsByTagName('unique-for-other-document').length===0;});
check('parsed SVG xmlns attributes retain namespace metadata',()=>{const a=document.getElementById('foreign').getAttributeNode('xmlns:xlink');return a.namespaceURI===XMLNS&&a.prefix==='xmlns'&&a.localName==='xlink'&&a.name==='xmlns:xlink';});
check('parsed SVG xlink attributes retain namespace metadata',()=>{const a=document.querySelector('use').getAttributeNode('xlink:href');return a.namespaceURI===XLINK&&a.prefix==='xlink'&&a.localName==='href'&&a.value==='#symbol';});
check('unwrapped parsed SVG attributes preserve namespaces through cloning',()=>{const p=document.createElement('div');p.innerHTML='<svg xmlns:xlink="http://www.w3.org/1999/xlink"><use xlink:href="#other"></use></svg>';const c=p.firstChild.cloneNode(true);return c.attributes[0].namespaceURI===XMLNS&&c.firstChild.attributes[0].namespaceURI===XLINK&&c.firstChild.attributes[0].localName==='href';});
check('imported parsed SVG attributes retain namespace and owner',()=>{const p=document.createElement('div');p.innerHTML='<svg><use xlink:href="#imported"></use></svg>';const d=inert(),c=d.importNode(p.firstChild,true),a=c.firstChild.attributes[0];return a.namespaceURI===XLINK&&a.prefix==='xlink'&&a.ownerDocument===d;});
check('author setAttribute does not infer a namespace from its prefix',()=>{const e=document.createElementNS(SVG,'use');e.setAttribute('xlink:href','#plain');const a=e.attributes[0],c=e.cloneNode().attributes[0];return a.namespaceURI===null&&a.prefix===null&&a.localName==='xlink:href'&&c.namespaceURI===null;});
check('parsed foreign xml:lang uses the XML namespace',()=>{const a=document.getElementById('foreign').getAttributeNode('xml:lang');return a.namespaceURI===XML&&a.prefix==='xml'&&a.localName==='lang'&&a.value==='en';});
check('unrecognized foreign prefixes remain ordinary attributes',()=>{const a=document.querySelector('use').getAttributeNode('xlink:unknown');return a.namespaceURI===null&&a.prefix===null&&a.localName==='xlink:unknown';});
check('HTML parser does not namespace ordinary HTML xmlns attributes',()=>{const a=document.getElementById('html-xmlns').attributes[1];return a.name==='xmlns:xlink'&&a.namespaceURI===null&&a.localName==='xmlns:xlink';});
check('remove and ordinary re-add discard parsed attribute namespace',()=>{const p=document.createElement('div');p.innerHTML='<svg><use xlink:href="#first"></use></svg>';const e=p.firstChild.firstChild,old=e.getAttributeNode('xlink:href');e.removeAttribute('xlink:href');e.setAttribute('xlink:href','#plain');return old.ownerElement===null&&old.namespaceURI===XLINK&&e.attributes[0].namespaceURI===null;});
check('ordinary value changes preserve an existing parsed namespace',()=>{const p=document.createElement('div');p.innerHTML='<svg><use xlink:href="#first"></use></svg>';const e=p.firstChild.firstChild;e.setAttribute('xlink:href','#second');return e.attributes[0].namespaceURI===XLINK&&e.attributes[0].value==='#second';});
check('detached clone of a parsed Attr preserves namespace',()=>{const a=document.querySelector('use').getAttributeNode('xlink:href'),c=a.cloneNode();return c.ownerElement===null&&c.namespaceURI===XLINK&&c.prefix==='xlink'&&c.localName==='href'&&c.value==='#symbol';});
for(let index=0;index<checks.length;index++)console.log('DOM_OWNERSHIP_CHECK '+JSON.stringify({index,...checks[index]}));
console.log('DOM_OWNERSHIP_RESULTS '+JSON.stringify({passed:checks.filter(x=>x.passed).length,total:checks.length}));
document.documentElement.setAttribute('data-dom-ownership-done','1');
</script></body></html>'''

EXPECTED = 40


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='lumen-dom-ownership-') as directory:
        page = Path(directory) / 'dom.html'
        page.write_text(PAGE)
        env = {k: v for k, v in os.environ.items() if not k.startswith('LUMEN_')}
        env.update(LUMEN_PROFILE_DIR=directory, LUMEN_CONSOLE='1', JSRUN_UNTIL_ATTR='data-dom-ownership-done')
        result = subprocess.run([str(binary), str(page), '3000'], text=True, capture_output=True, env=env, timeout=8)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    transcript = result.stdout + result.stderr
    summaries = [json.loads(line.split('DOM_OWNERSHIP_RESULTS ', 1)[1]) for line in transcript.splitlines() if 'DOM_OWNERSHIP_RESULTS ' in line]
    checks = [json.loads(line.split('DOM_OWNERSHIP_CHECK ', 1)[1]) for line in transcript.splitlines() if 'DOM_OWNERSHIP_CHECK ' in line]
    if result.returncode or len(summaries) != 1 or len(checks) != EXPECTED or sorted(x['index'] for x in checks) != list(range(EXPECTED)):
        print('DOM_OWNERSHIP_INFRASTRUCTURE missing or duplicate fixture result')
        return 2
    report = summaries[0]
    if report['total'] != EXPECTED or report['passed'] != sum(x['passed'] is True for x in checks):
        return 2
    print(f"RESULTS dom-ownership {'PASS' if report['passed'] == EXPECTED else 'FAIL'} ({report['passed']}/{EXPECTED} checks)")
    return 0 if report['passed'] == EXPECTED else 1


if __name__ == '__main__':
    raise SystemExit(main())
