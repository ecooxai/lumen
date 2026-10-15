const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {test} = require('node:test');
const source = fs.readFileSync(process.env.LUMEN_TEST_FETCH_SOURCE || 'src/js/prelude/07b-net.js', 'utf8');
function harness() {
    const calls = [];
    const context = { URL, URLSearchParams, ReadableStream, AbortController, Blob, FormData,
        ArrayBuffer, Uint8Array, atob, document: {baseURI: 'https://page.test/'},
        location: {origin: 'https://page.test'}, objectURLs: new Map(),
        def: (o, k, v) => Object.defineProperty(o, k, {value:v, configurable:true, writable:true}),
        toBytes: b => b instanceof ArrayBuffer ? b : b.buffer.slice(b.byteOffset, b.byteOffset+b.byteLength),
        N: {encode: s => new TextEncoder().encode(s).buffer, decode: b => new TextDecoder().decode(b),
            fetch(...args) { calls.push(args); args[4](200, 'OK', args[1], [], new TextEncoder().encode('ok').buffer, false); return calls.length; }, abort() {}}
    };
    vm.runInNewContext(source.slice(0, source.indexOf('class FormData')) + '\nconst XHR_FORBIDDEN = new Set(); this.API = {Request, fetch};', context);
    return {...context.API, calls};
}
test('fetch and Request cloning ignore overridden public request getters', async () => {
    const h = harness(), url = 'data:application/json,%7B%22value%22%3A42%7D';
    const r = new h.Request(url);
    for (const key of ['url','method','headers','signal','credentials','mode','cache','redirect','referrer','integrity','keepalive','priority','bodyUsed','body']) {
        Object.defineProperty(r, key, {get() { throw new Error('public getter '+key+' must not be read'); }});
    }
    const copy = new h.Request(r), clone = r.clone();
    assert.equal(copy.url, url); assert.equal(clone.url, url); assert.equal(copy.method, 'GET');
    assert.equal((await (await h.fetch(r)).json()).value, 42);
    assert.equal(h.calls.length, 0);
});
test('a forged visible URL cannot redirect a local Request to the network', async () => {
    const h = harness(), r = new h.Request('data:text/plain,local');
    Object.defineProperty(r, 'url', {get: () => 'https://page.test/unexpected'});
    Object.defineProperty(r, 'method', {get: () => 'POST'});
    assert.equal(await (await h.fetch(r)).text(), 'local');
    assert.equal(h.calls.length, 0);
});
test('request copies retain options, headers and body; explicit init overrides win', async () => {
    const h = harness(), signal = new AbortController().signal;
    const r = new h.Request('/endpoint', {method:'POST', body:'payload', headers:{'X-One':'1'}, signal,
        credentials:'include', mode:'same-origin', cache:'no-store', redirect:'error', integrity:'sha256-example', keepalive:true});
    const copy = new h.Request(r);
    for (const key of ['url','method','credentials','mode','cache','redirect','integrity','keepalive']) assert.equal(copy[key],r[key]);
    assert.equal(copy.signal,signal); assert.equal(await copy.text(),'payload');
    copy.headers.set('X-One','2'); assert.equal(r.headers.get('X-One'),'1');
    const changed = new h.Request(r, {method:'PUT', body:'replacement', credentials:'omit', headers:{'X-Two':'2'}});
    assert.equal(changed.method,'PUT');assert.equal(changed.credentials,'omit');assert.equal(changed.headers.get('X-One'),null);
    assert.equal(await changed.text(),'replacement');
    await h.fetch(r);
    assert.equal(h.calls.length,1);assert.equal(h.calls[0][0],'POST');assert.equal(h.calls[0][1],'https://page.test/endpoint');
    assert.equal(new TextDecoder().decode(h.calls[0][3]),'payload');
});
test('readonly getters cannot silently change the stored request', () => {
    const h = harness(), r = new h.Request('/a');
    assert.equal(Reflect.set(r,'url','https://elsewhere.test/'),false);
    assert.equal(r.url,'https://page.test/a');
    assert.throws(() => Object.getOwnPropertyDescriptor(h.Request.prototype,'url').get.call({}), /Illegal invocation/);
    assert.equal(new h.Request('/a',{method:'custom'}).method,'custom');
    assert.equal(new h.Request('/a',{method:'post'}).method,'POST');
    assert.throws(() => new h.Request('/a',{method:'TRACE'}), /Invalid request method/);
    assert.throws(() => new h.Request('/a',{method:'G ET'}), /Invalid request method/);
    assert.throws(() => new h.Request('/a',{body:'invalid'}), /cannot have a body/);
});
