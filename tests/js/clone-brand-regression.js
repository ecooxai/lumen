// Focused public-API identity/serialization regressions, not upstream WPT rows.
// The same checks execute in real window and dedicated-worker realms.
globalThis.runCloneBrandRegression = function () {
    'use strict';
    const rows = [], OriginalDOMException = DOMException;
    const url = () => new URL('https://example.test/path?q=one');
    const params = () => new URLSearchParams('q=one');
    function test(name, body) {
        try {
            if (body() !== true) throw new Error('Unexpected public API result');
            rows.push({ name, status: 'PASS' });
        } catch (error) {
            rows.push({ name, status: 'FAIL', message: String(error && error.stack || error) });
        }
    }
    function cloneError(body) {
        try { body(); return false; }
        catch (error) {
            return error instanceof OriginalDOMException && error.name === 'DataCloneError' && error.code === 25;
        }
    }
    for (const [name, create] of [['URL', url], ['URLSearchParams', params]]) {
        test(name + ' rejects direct structured cloning as a platform object', () => cloneError(() => structuredClone(create())));
        test(name + ' rejects inside an ordinary object', () => cloneError(() => structuredClone({ nested: create() })));
        test(name + ' rejects inside an array', () => cloneError(() => structuredClone([create()])));
        test(name + ' rejects as a Map key', () => cloneError(() => structuredClone(new Map([[create(), 'value']]))));
        test(name + ' rejects as a Map value', () => cloneError(() => structuredClone(new Map([['key', create()]]))));
        test(name + ' rejects inside a Set', () => cloneError(() => structuredClone(new Set([create()]))));
        test(name + ' brand survives prototype replacement', () => {
            const object = create(); Object.setPrototypeOf(object, null);
            return cloneError(() => structuredClone(object));
        });
        test(name + ' brand survives a public toStringTag disguise', () => {
            return ['Object', 'Blob', 'Map', 'Date'].every(tag => {
                const object = create(); Object.defineProperty(object, Symbol.toStringTag, { value: tag });
                return cloneError(() => structuredClone(object));
            });
        });
        test(name + ' is rejected before a throwing public toStringTag getter runs', () => {
            const object = create(); let read = false;
            Object.defineProperty(object, Symbol.toStringTag, { get() { read = true; throw new Error('Unexpected tag read'); } });
            return cloneError(() => structuredClone(object)) && !read;
        });
        test(name + ' is rejected before an own enumerable getter runs', () => {
            const object = create(); let read = false;
            Object.defineProperty(object, 'trap', { enumerable: true, get() { read = true; throw new Error('Unexpected traversal'); } });
            return cloneError(() => structuredClone(object)) && !read;
        });
        test(name + ' rejection does not read its public constructor', () => {
            const object = create(); let read = false;
            Object.defineProperty(object, 'constructor', { enumerable: true, get() { read = true; throw new Error('Unexpected constructor read'); } });
            return cloneError(() => structuredClone(object)) && !read;
        });
        test(name + ' identity is not inherited by an ordinary descendant', () => {
            const object = Object.create(create()); object.payload = 'kept';
            const clone = structuredClone(object);
            return Object.getPrototypeOf(clone) === Object.prototype && clone.payload === 'kept';
        });
    }
    test('URL subclasses retain genuine platform identity', () => {
        class ChildURL extends URL {}
        return cloneError(() => structuredClone(new ChildURL('https://example.test/')));
    });
    test('URLSearchParams subclasses retain genuine platform identity', () => {
        class ChildParams extends URLSearchParams {}
        return cloneError(() => structuredClone(new ChildParams('q=one')));
    });
    test('URL.parse creates the same nonserializable platform identity', () => cloneError(() => structuredClone(URL.parse('https://example.test/'))));
    test('linked URL.searchParams has genuine platform identity', () => cloneError(() => structuredClone(url().searchParams)));
    test('a URL prototype impostor remains an ordinary serializable object', () => {
        const object = Object.create(URL.prototype); object.payload = 'kept';
        const clone = structuredClone(object);
        return Object.getPrototypeOf(clone) === Object.prototype && clone.payload === 'kept';
    });
    test('a URLSearchParams prototype impostor remains an ordinary serializable object', () => {
        const object = Object.create(URLSearchParams.prototype); object.payload = 'kept';
        const clone = structuredClone(object);
        return Object.getPrototypeOf(clone) === Object.prototype && clone.payload === 'kept';
    });
    test('a plain object with URL-shaped fields has no platform brand', () => {
        const clone = structuredClone({ href: 'https://example.test/', searchParams: { q: 'one' }, _p: ['copy'] });
        return clone.href === 'https://example.test/' && clone.searchParams.q === 'one' && clone._p[0] === 'copy';
    });
    test('URLSearchParams native identity adds no observable own keys', () => Reflect.ownKeys(params()).length === 0);
    test('ordinary JavaScript private fields do not create a platform brand', () => {
        class Ordinary { #hidden = 'private'; constructor() { this.payload = 'kept'; } }
        const clone = structuredClone(new Ordinary());
        return Object.keys(clone).join() === 'payload' && clone.payload === 'kept';
    });
    test('a Proxy is rejected without consulting its traps', () => {
        let traps = 0;
        const object = new Proxy({}, { ownKeys() { traps++; return []; }, get() { traps++; return undefined; } });
        return cloneError(() => structuredClone(object)) && traps === 0;
    });
    test('a revoked Proxy reports a genuine clone error', () => {
        const pair = Proxy.revocable({}, {}); pair.revoke();
        return cloneError(() => structuredClone(pair.proxy));
    });
    test('a getter returning a branded object runs exactly once then cloning rejects', () => {
        let reads = 0;
        const object = { get nested() { reads++; return url(); } };
        return cloneError(() => structuredClone(object)) && reads === 1;
    });
    test('changing the public DOMException constructor does not change the intrinsic exception', () => {
        const saved = globalThis.DOMException;
        try { globalThis.DOMException = function () { throw new Error('Public replacement used'); };
              return cloneError(() => structuredClone(url())); }
        finally { globalThis.DOMException = saved; }
    });
    test('ordinary cycles and shared references remain preserved', () => {
        const child = { value: 3 }, object = { first: child, second: child }; object.self = object;
        const clone = structuredClone(object);
        return clone !== object && clone.self === clone && clone.first === clone.second && clone.first !== child && clone.first.value === 3;
    });
    test('a cycle does not hide a nested genuine platform instance', () => {
        const object = {}; object.self = object; object.url = url();
        return cloneError(() => structuredClone(object));
    });
    test('ordinary Map Set and typed-array data remain cloneable', () => {
        const input = { map: new Map([['key', 5]]), set: new Set(['value']), bytes: new Uint8Array([1, 2, 3]) };
        const clone = structuredClone(input); clone.bytes[0] = 9;
        return clone.map.get('key') === 5 && clone.set.has('value') && input.bytes[0] === 1 && clone.bytes[0] === 9;
    });
    test('functions and Symbols retain genuine clone errors', () =>
        cloneError(() => structuredClone(function () {})) && cloneError(() => structuredClone(Symbol())));
    test('an ordinary getter exception propagates unchanged', () => {
        const marker = {};
        try { structuredClone({ get value() { throw marker; } }); return false; }
        catch (error) { return error === marker; }
    });
    return rows;
};
