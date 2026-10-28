// Same-origin child-realm regressions. The native test supplies an actual
// parsed iframe WindowProxy; no realm or browser API is emulated here.
globalThis.runURLIDLRealmRegression = function (child) {
    'use strict';
    const rows = [], OriginalTypeError = TypeError;
    function test(name, body) {
        try {
            if (body() !== true) throw new Error('Unexpected cross-realm result');
            rows.push({ name, status: 'PASS' });
        } catch (error) {
            rows.push({ name, status: 'FAIL', message: String(error && error.stack || error) });
        }
    }
    function typeError(body) {
        try { body(); return false; } catch (error) { return error instanceof OriginalTypeError; }
    }
    const getter = (Interface, name) => Object.getOwnPropertyDescriptor(Interface.prototype, name).get;
    const setter = (Interface, name) => Object.getOwnPropertyDescriptor(Interface.prototype, name).set;
    test('child realm exposes independent genuine interface constructors', () =>
        child.URL !== URL && child.URLSearchParams !== URLSearchParams && child.DOMException !== DOMException &&
        child.Array !== Array && child.Function !== Function);
    test('parent URL getters accept a genuine child instance', () => {
        const value = new child.URL('https://child.test/path?q=one');
        return getter(URL, 'href').call(value) === 'https://child.test/path?q=one' &&
            getter(URL, 'origin').call(value) === 'https://child.test';
    });
    test('child URL getters accept a genuine parent instance', () => {
        const value = new URL('https://parent.test/path');
        return getter(child.URL, 'href').call(value) === 'https://parent.test/path';
    });
    test('foreign URL getter creates linked parameters in the URL owner realm', () => {
        const value = new child.URL('https://child.test/?q=one');
        const params = getter(URL, 'searchParams').call(value);
        return Object.getPrototypeOf(params) === child.URLSearchParams.prototype && params === value.searchParams &&
            params.toString() === 'q=one';
    });
    test('foreign URL setters preserve the owner parameter list and identity', () => {
        const value = new child.URL('https://child.test/?q=one'), params = value.searchParams;
        setter(URL, 'search').call(value, '?x=two');
        URLSearchParams.prototype.append.call(params, 'x', 'three');
        return value.searchParams === params && value.search === '?x=two&x=three' &&
            params.toString() === 'x=two&x=three';
    });
    test('foreign URL setter conversion reentrancy reads the current record', () => {
        const value = new child.URL('https://first.test/before');
        setter(URL, 'pathname').call(value, { toString() { value.href = 'https://after.test/base?kept=yes'; return '/next'; } });
        return value.href === 'https://after.test/next?kept=yes';
    });
    test('URLSearchParams parent operations accept genuine child state', () => {
        const value = new child.URLSearchParams('a=one&a=two');
        URLSearchParams.prototype.set.call(value, 'a', 'after');
        return getter(URLSearchParams, 'size').call(value) === 1 &&
            URLSearchParams.prototype.toString.call(value) === 'a=after';
    });
    test('URLSearchParams child operations accept genuine parent state', () => {
        const value = new URLSearchParams('a=one');
        child.URLSearchParams.prototype.append.call(value, 'a', 'two');
        return child.URLSearchParams.prototype.getAll.call(value, 'a').join() === 'one,two';
    });
    test('cross-realm URL and parameter prototype impostors do not gain private state', () =>
        typeError(() => getter(URL, 'href').call(Object.create(child.URL.prototype))) &&
        typeError(() => URLSearchParams.prototype.toString.call(Object.create(child.URLSearchParams.prototype))));
    test('parent DOMException getters accept genuine child fields and code', () => {
        const value = new child.DOMException('child detail', 'SyntaxError');
        return getter(DOMException, 'name').call(value) === 'SyntaxError' &&
            getter(DOMException, 'message').call(value) === 'child detail' && getter(DOMException, 'code').call(value) === 12;
    });
    test('child DOMException getters accept genuine parent fields and code', () => {
        const value = new DOMException('parent detail', 'DataCloneError');
        return getter(child.DOMException, 'message').call(value) === 'parent detail' &&
            getter(child.DOMException, 'code').call(value) === 25;
    });
    test('cross-realm DOMException prototype impostors and Proxy wrappers are rejected', () => {
        let traps = 0;
        const proxy = new Proxy(new child.DOMException(), { get() { traps++; throw new Error('Unexpected proxy trap'); } });
        return typeError(() => getter(DOMException, 'name').call(Object.create(child.DOMException.prototype))) &&
            typeError(() => getter(DOMException, 'name').call(proxy)) && traps === 0;
    });
    test('DOMException fallback uses a foreign function target realm', () => {
        const target = child.Function('return undefined;');
        target.prototype = null;
        const value = Reflect.construct(DOMException, ['detail', 'SyntaxError'], target);
        return Object.getPrototypeOf(value) === child.DOMException.prototype && value.message === 'detail' && value.code === 12;
    });
    test('DOMException fallback follows foreign targets through a local Proxy', () => {
        const target = child.Function('return undefined;');
        target.prototype = null;
        let reads = 0;
        const proxy = new Proxy(target, { get(object, key, receiver) {
            if (key === 'prototype') reads++;
            return Reflect.get(object, key, receiver);
        } });
        const value = Reflect.construct(DOMException, ['detail'], proxy);
        return Object.getPrototypeOf(value) === child.DOMException.prototype && reads === 1 && value.message === 'detail';
    });
    test('DOMException fallback follows a foreign bound function target', () => {
        const target = child.Function('return undefined;').bind(null);
        const value = Reflect.construct(DOMException, ['bound'], target);
        return Object.getPrototypeOf(value) === child.DOMException.prototype && value.message === 'bound';
    });
    test('URL fallback follows a foreign target through a local Proxy', () => {
        const target = child.Function('return undefined;');
        target.prototype = null;
        const value = Reflect.construct(URL, ['https://example.test/'], new Proxy(target, {}));
        return Object.getPrototypeOf(value) === child.URL.prototype && value.href === 'https://example.test/';
    });
    test('URL fallback follows a foreign bound function target', () => {
        const target = child.Function('return undefined;').bind(null);
        const value = Reflect.construct(URL, ['https://example.test/?x=one'], target);
        const params = getter(child.URL, 'searchParams').call(value);
        return Object.getPrototypeOf(value) === child.URL.prototype &&
            Object.getPrototypeOf(params) === URLSearchParams.prototype && params.toString() === 'x=one';
    });
    return rows;
};

// The native driver snapshots these real child functions/objects, removes the
// frame, verifies its inactive association and canceled scheduled work, and
// only then invokes this function. Retained objects still keep its realm alive.
globalThis.runURLIDLDetachedRegression = function (saved) {
    'use strict';
    const rows = [];
    function test(name, body) {
        try {
            if (body() !== true) throw new Error('Unexpected detached-realm result');
            rows.push({ name, status: 'PASS' });
        } catch (error) {
            rows.push({ name, status: 'FAIL', message: String(error && error.stack || error) });
        }
    }
    test('retained detached URL constructor parses without an active child frame', () => {
        const value = new saved.Interface('../after?q=one', 'https://detached.test/path/base');
        return saved.href.get.call(value) === 'https://detached.test/after?q=one' &&
            Object.getPrototypeOf(value) === saved.prototype;
    });
    test('retained detached URL methods can operate on a genuine parent receiver', () => {
        const value = new URL('https://parent.test/before?kept=yes');
        saved.pathname.set.call(value, '/after');
        return saved.href.get.call(value) === 'https://parent.test/after?kept=yes';
    });
    test('retained detached linked parameters update their original URL record', () => {
        URLSearchParams.prototype.append.call(saved.params, 'after', 'two');
        return saved.href.get.call(saved.instance) === 'https://detached.test/?before=one&after=two' &&
            saved.searchParams.get.call(saved.instance) === saved.params;
    });
    test('retained detached Document and ordinary native-bound DOM methods remain usable', () => {
        const element = saved.createElement.call(saved.document, 'span');
        element.setAttribute('data-detached-idl', 'retained');
        const body = saved.document.body;
        body.appendChild(element);
        return element.ownerDocument === saved.document &&
            element.getAttribute('data-detached-idl') === 'retained' && body.removeChild(element) === element;
    });
    return rows;
};
