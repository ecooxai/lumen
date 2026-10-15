const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {test} = require('node:test');
const source = fs.readFileSync('src/js/prelude/06-document.js', 'utf8');
function harness() {
    let url = 'https://page.test/original/page', queries = 0;
    const context = {N: {
        version: d => d.version,
        url: () => url,
        query(d) { queries++; return d.bases.find(b => b.href !== undefined); },
        attr: b => b.href,
        urlParse(href, base) { try { return [new URL(href, base).href]; } catch { return null; } }
    }};
    vm.runInNewContext(source.slice(0, source.indexOf('const allNamed')) + '\nthis.baseURI = documentBaseURI;', context);
    return {read: context.baseURI, queries: () => queries, setURL: u => url = u};
}
test('unchanged document performs one lookup across repeated base URI reads', () => {
    const h = harness(), doc = {version: 1, bases: []};
    for (let i = 0; i < 250; i++) assert.equal(h.read(doc), 'https://page.test/original/page');
    assert.equal(h.queries(), 1);
});
test('insertion, href mutation, reordering and removal invalidate the cache', () => {
    const h = harness(), doc = {version: 1, bases: []};
    h.read(doc);
    doc.bases.push({href:'/one/'}, {href:'/two/'}); doc.version++;
    assert.equal(h.read(doc), 'https://page.test/one/');
    doc.bases[0].href = '/changed/'; doc.version++;
    assert.equal(h.read(doc), 'https://page.test/changed/');
    doc.bases.reverse(); doc.version++;
    assert.equal(h.read(doc), 'https://page.test/two/');
    doc.bases[0].href = undefined; doc.version++;
    assert.equal(h.read(doc), 'https://page.test/changed/');
    doc.bases = []; doc.version++;
    assert.equal(h.read(doc), 'https://page.test/original/page');
});
test('history URL changes invalidate relative resolution without a DOM mutation', () => {
    const h = harness(), doc = {version: 1, bases: [{href:'relative/'}]};
    assert.equal(h.read(doc), 'https://page.test/original/relative/');
    h.setURL('https://page.test/new/page');
    assert.equal(h.read(doc), 'https://page.test/new/relative/');
    assert.equal(h.queries(), 2);
});
test('documents have separate caches and invalid base URLs fall back', () => {
    const h = harness(), a = {version: 1, bases: [{href:'/one/'}]}, b = {version: 1, bases: [{href:'http://['}]};
    assert.equal(h.read(a), 'https://page.test/one/');
    assert.equal(h.read(b), 'https://page.test/original/page');
    assert.equal(h.read(a), 'https://page.test/one/');
    assert.equal(h.queries(), 2);
});
