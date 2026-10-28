// Public Web IDL/receiver regressions, shared by real window and worker realms.
globalThis.runURLIDLRegression = function () {
    'use strict';
    const rows = [], OriginalTypeError = TypeError;
    const names = ['href', 'origin', 'protocol', 'username', 'password', 'host', 'hostname',
                   'port', 'pathname', 'search', 'searchParams', 'hash'];
    const constants = [
        'INDEX_SIZE_ERR', 'DOMSTRING_SIZE_ERR', 'HIERARCHY_REQUEST_ERR', 'WRONG_DOCUMENT_ERR',
        'INVALID_CHARACTER_ERR', 'NO_DATA_ALLOWED_ERR', 'NO_MODIFICATION_ALLOWED_ERR',
        'NOT_FOUND_ERR', 'NOT_SUPPORTED_ERR', 'INUSE_ATTRIBUTE_ERR', 'INVALID_STATE_ERR',
        'SYNTAX_ERR', 'INVALID_MODIFICATION_ERR', 'NAMESPACE_ERR', 'INVALID_ACCESS_ERR',
        'VALIDATION_ERR', 'TYPE_MISMATCH_ERR', 'SECURITY_ERR', 'NETWORK_ERR', 'ABORT_ERR',
        'URL_MISMATCH_ERR', 'QUOTA_EXCEEDED_ERR', 'TIMEOUT_ERR', 'INVALID_NODE_TYPE_ERR', 'DATA_CLONE_ERR',
    ];
    function test(name, body) {
        try {
            if (body() !== true) throw new Error('Unexpected public API result');
            rows.push({ name, status: 'PASS' });
        } catch (error) {
            rows.push({ name, status: 'FAIL', message: String(error && error.stack || error) });
        }
    }
    function typeError(body) {
        try { body(); return false; } catch (error) { return error instanceof OriginalTypeError; }
    }
    const makeURL = () => new URL('https://user:pass@example.test:8443/path?q=one#old');
    test('URL constructor metadata and prototype are Web IDL shaped', () => {
        const d = Object.getOwnPropertyDescriptor(URL, 'prototype');
        return URL.length === 1 && URL.name === 'URL' && Object.getPrototypeOf(URL) === Function.prototype &&
            d.value === URL.prototype && !d.writable && !d.enumerable && !d.configurable;
    });
    test('URL constructor requires new and its required argument', () =>
        typeError(() => URL('https://example.test/')) && typeError(() => new URL()));
    test('URL instances have private state rather than observable storage keys', () => Reflect.ownKeys(makeURL()).length === 0);
    test('URL subclasses retain their own prototype and real state', () => {
        class Derived extends URL {}
        const value = new Derived('/path', 'https://example.test/base');
        return value instanceof Derived && value instanceof URL && value.href === 'https://example.test/path';
    });
    for (const name of names) {
        test('URL.' + name + ' accessor descriptor', () => {
            const d = Object.getOwnPropertyDescriptor(URL.prototype, name);
            const readOnly = name === 'origin' || name === 'searchParams';
            return !!d && d.enumerable && d.configurable && typeof d.get === 'function' &&
                d.get.name === 'get ' + name && d.get.length === 0 &&
                (readOnly ? d.set === undefined : typeof d.set === 'function' && d.set.name === 'set ' + name && d.set.length === 1);
        });
        test('URL.' + name + ' getter rejects unbranded receivers', () => {
            const get = Object.getOwnPropertyDescriptor(URL.prototype, name).get;
            return [undefined, null, {}, URL.prototype, Object.create(URL.prototype), Object.create(makeURL())]
                .every(receiver => typeError(() => get.call(receiver)));
        });
        if (name !== 'origin' && name !== 'searchParams') {
            test('URL.' + name + ' setter checks receiver before conversion', () => {
                const set = Object.getOwnPropertyDescriptor(URL.prototype, name).set;
                let calls = 0;
                const input = { toString() { calls++; throw new Error('Unbranded conversion was reached'); } };
                return typeError(() => set.call({}, input)) && calls === 0;
            });
        }
    }
    test('URL accessors have no own constructor prototype', () => names.every(name => {
        const d = Object.getOwnPropertyDescriptor(URL.prototype, name);
        return [d.get, d.set].filter(fn => fn !== undefined)
            .every(fn => !Object.prototype.hasOwnProperty.call(fn, 'prototype'));
    }));
    test('URL accessor functions cannot be newTarget constructors', () => names.every(name => {
        const d = Object.getOwnPropertyDescriptor(URL.prototype, name);
        // Reflect.construct tests [[Construct]] without invoking the accessor
        // body, so a receiver-brand exception cannot conceal constructibility.
        return [d.get, d.set].filter(fn => fn !== undefined)
            .every(fn => typeError(() => Reflect.construct(Object, [], fn)));
    }));
    for (const name of ['toString', 'toJSON']) {
        test('URL.' + name + ' operation descriptor and private receiver', () => {
            const d = Object.getOwnPropertyDescriptor(URL.prototype, name);
            return d.enumerable && d.configurable && d.writable && d.value.name === name && d.value.length === 0 &&
                typeError(() => d.value.call({ _p: ['forged'] })) && d.value.call(makeURL()) === makeURL().href;
        });
    }
    for (const name of ['parse', 'canParse']) {
        test('URL.' + name + ' static operation descriptor', () => {
            const d = Object.getOwnPropertyDescriptor(URL, name);
            return d.enumerable && d.configurable && d.writable && d.value.name === name && d.value.length === 1;
        });
        test('URL.' + name + ' requires its input and propagates Symbol conversion', () =>
            typeError(() => URL[name]()) && typeError(() => URL[name](Symbol())) &&
            typeError(() => URL[name]('https://example.test/', Symbol())));
        test('URL.' + name + ' preserves conversion order and abrupt completion', () => {
            const order = [], sentinel = {};
            const input = { toString() { order.push('input'); return '/path'; } };
            const base = { toString() { order.push('base'); return 'https://example.test/'; } };
            const value = URL[name].call(null, input, base);
            if (order.join() !== 'input,base' || !(name === 'parse' ? value.href === 'https://example.test/path' : value === true)) return false;
            try { URL[name]({ toString() { throw sentinel; } }, { toString() { order.push('unexpected'); return ''; } }); }
            catch (error) { return error === sentinel && order.join() === 'input,base'; }
            return false;
        });
    }
    test('URL.parse invalid inputs return null without swallowing conversion failures', () =>
        URL.parse('relative') === null && URL.parse('https://example.test/', 'bad base') === null &&
        URL.canParse('relative') === false && URL.canParse('https://example.test/', 'bad base') === false);
    test('URL.parse returns a real base-interface instance independent of its receiver', () => {
        class Derived extends URL {}
        const value = URL.parse.call(Derived, 'https://example.test/');
        return Object.getPrototypeOf(value) === URL.prototype && value.href === 'https://example.test/';
    });
    test('URL USVString conversion replaces lone surrogates', () => {
        const value = new URL('/\ud800', 'https://example.test/');
        value.hash = '\udc00';
        return value.pathname === '/%EF%BF%BD' && value.hash === '#%EF%BF%BD';
    });
    test('URL constructor conversion errors propagate in argument order', () => {
        const order = [], sentinel = {};
        try { new URL({ toString() { order.push('input'); throw sentinel; } }, { toString() { order.push('base'); return ''; } }); }
        catch (error) { return error === sentinel && order.join() === 'input'; }
        return false;
    });
    test('URL constructor converts arguments before one newTarget prototype read', () => {
        const order = [], prototype = {};
        const target = new Proxy(function () {}, { get(object, key, receiver) {
            if (key === 'prototype') { order.push('prototype'); return prototype; }
            return Reflect.get(object, key, receiver);
        } });
        const value = Reflect.construct(URL, [
            { toString() { order.push('input'); return '/path'; } },
            { toString() { order.push('base'); return 'https://example.test/'; } },
        ], target);
        return order.join() === 'input,base,prototype' && Object.getPrototypeOf(value) === prototype &&
            Object.getOwnPropertyDescriptor(URL.prototype, 'href').get.call(value) === 'https://example.test/path';
    });
    test('URL constructor prototype errors precede URL parsing failures', () => {
        const sentinel = {}, order = [];
        const target = new Proxy(function () {}, { get(object, key, receiver) {
            if (key === 'prototype') { order.push('prototype'); throw sentinel; }
            return Reflect.get(object, key, receiver);
        } });
        try { Reflect.construct(URL, [{ toString() { order.push('input'); return 'invalid'; } }], target); }
        catch (error) { return error === sentinel && order.join() === 'input,prototype'; }
        return false;
    });
    test('URL constructor nonobject newTarget prototype falls back to its interface', () => {
        function Target() {}
        Target.prototype = null;
        const value = Reflect.construct(URL, ['https://example.test/'], Target);
        return Object.getPrototypeOf(value) === URL.prototype && value.href === 'https://example.test/';
    });
    test('URL receiver checks reject Proxy wrappers without invoking their traps', () => {
        let calls = 0;
        const value = new Proxy(makeURL(), { get() { calls++; throw new Error('Unexpected receiver trap'); } });
        return typeError(() => Object.getOwnPropertyDescriptor(URL.prototype, 'href').get.call(value)) && calls === 0;
    });
    test('URL setter conversion reentrancy uses the latest URL record', () => {
        const value = makeURL();
        value.pathname = { toString() { value.href = 'https://other.test/base?changed=1'; return '/after'; } };
        return value.href === 'https://other.test/after?changed=1';
    });
    test('URL linked parameters retain identity across href and query changes', () => {
        const value = makeURL(), params = value.searchParams;
        value.href = 'https://other.test/new?q=two';
        params.append('q', 'three'); value.search = '?x=last';
        return value.searchParams === params && params.toString() === 'x=last' && value.href === 'https://other.test/new?x=last';
    });
    test('URL and query errors retain their intrinsic after global replacement', () => {
        const saved = globalThis.TypeError;
        let calls = 0;
        try {
            globalThis.TypeError = function ReplacementTypeError() {
                calls++;
                return new Error('The public replacement must not construct a platform error');
            };
            const pass = typeError(() => new URL(Symbol())) &&
                typeError(() => URL.parse(Symbol())) &&
                typeError(() => new URLSearchParams(Symbol())) &&
                typeError(() => URLSearchParams.prototype.get.call({}, 'key'));
            return pass && calls === 0;
        } finally {
            globalThis.TypeError = saved;
        }
    });
    test('URL partial host commit and default-port mutation remain native', () => {
        const value = new URL('https://old.test:8443/path');
        value.host = 'new.test:invalid';
        if (value.hostname !== 'new.test' || value.port !== '8443') return false;
        value.port = '443';
        return value.href === 'https://new.test/path';
    });
    test('URL object-URL operations keep Web IDL operation descriptors', () => {
        return ['createObjectURL', 'revokeObjectURL'].every(name => {
            const d = Object.getOwnPropertyDescriptor(URL, name);
            return d.enumerable && d.configurable && d.writable && d.value.name === name && d.value.length === 1 && typeError(() => URL[name]());
        });
    });
    test('URL object-URL hooks remain usable with each realm blob store', () => {
        const value = URL.createObjectURL(new Blob(['kept'], { type: 'text/plain' }));
        URL.revokeObjectURL(value);
        return typeof value === 'string' && value.startsWith('blob:');
    });
    test('DOMException defaults and intrinsic prototype relationships', () => {
        const value = new DOMException();
        return DOMException.length === 0 && DOMException.name === 'DOMException' &&
            Object.getPrototypeOf(DOMException) === Function.prototype &&
            Object.getPrototypeOf(DOMException.prototype) === Error.prototype && value instanceof Error &&
            value.name === 'Error' && value.message === '' && value.code === 0;
    });
    test('DOMException requires new', () => typeError(() => DOMException()));
    for (const name of ['name', 'message', 'code']) {
        test('DOMException.' + name + ' getter metadata and receiver brand', () => {
            const d = Object.getOwnPropertyDescriptor(DOMException.prototype, name);
            return d.enumerable && d.configurable && d.set === undefined && d.get.name === 'get ' + name && d.get.length === 0 &&
                [undefined, null, {}, new Error(), DOMException.prototype, Object.create(new DOMException())]
                    .every(receiver => typeError(() => d.get.call(receiver)));
        });
    }
    test('DOMException constants are complete immutable enumerable interface members', () => {
        return constants.every((name, index) => [DOMException, DOMException.prototype].every(object => {
            const d = Object.getOwnPropertyDescriptor(object, name);
            return d && d.value === index + 1 && d.enumerable && !d.writable && !d.configurable;
        })) && Object.keys(DOMException).join() === constants.join();
    });
    test('DOMException interface converts to its own constant record', () => {
        const expected = constants.map((name, index) => name + '=' + (index + 1)).join('&');
        return new URLSearchParams(DOMException).toString() === expected;
    });
    test('DOMException prototype record conversion invokes its genuine receiver checks', () =>
        typeError(() => new URLSearchParams(DOMException.prototype)));
    test('DOMException associated fields are read through the prototype', () => {
        const value = new DOMException('message', 'SyntaxError');
        return value.name === 'SyntaxError' && value.message === 'message' && value.code === 12 &&
            ['name', 'message', 'code'].every(name => !Object.prototype.hasOwnProperty.call(value, name));
    });
    test('DOMException code uses the private name despite an own shadow', () => {
        const value = new DOMException('message', 'DataCloneError');
        Object.defineProperty(value, 'name', { value: 'AbortError' });
        return value.name === 'AbortError' && value.code === 25;
    });
    test('DOMException obsolete constant names do not invent exception codes', () =>
        ['DOMStringSizeError', 'NoDataAllowedError', 'ValidationError', '__proto__', 'custom']
            .every(name => new DOMException('', name).code === 0));
    test('DOMException legacy error names retain their specified codes', () =>
        [['IndexSizeError', 1], ['NoModificationAllowedError', 7], ['InUseAttributeError', 10],
         ['TypeMismatchError', 17], ['SecurityError', 18], ['InvalidNodeTypeError', 24], ['DataCloneError', 25]]
            .every(([name, code]) => new DOMException('', name).code === code));
    test('DOMException converts message before name and preserves DOMString surrogates', () => {
        const order = [];
        const value = new DOMException({ toString() { order.push('message'); return '\ud800'; } },
                                       { toString() { order.push('name'); return 'custom'; } });
        return order.join() === 'message,name' && value.message === '\ud800' && value.name === 'custom';
    });
    test('DOMException conversion exceptions propagate before later arguments', () => {
        const sentinel = {}; let later = false;
        try { new DOMException({ toString() { throw sentinel; } }, { toString() { later = true; return 'custom'; } }); }
        catch (error) { return error === sentinel && !later; }
        return false;
    });
    test('DOMException converts message and name before one newTarget prototype read', () => {
        const order = [], prototype = {};
        const target = new Proxy(function () {}, { get(object, key, receiver) {
            if (key === 'prototype') { order.push('prototype'); return prototype; }
            return Reflect.get(object, key, receiver);
        } });
        const value = Reflect.construct(DOMException, [
            { toString() { order.push('message'); return 'detail'; } },
            { toString() { order.push('name'); return 'SyntaxError'; } },
        ], target);
        return order.join() === 'message,name,prototype' && Object.getPrototypeOf(value) === prototype &&
            Object.getOwnPropertyDescriptor(DOMException.prototype, 'message').get.call(value) === 'detail' &&
            Object.getOwnPropertyDescriptor(DOMException.prototype, 'code').get.call(value) === 12;
    });
    test('DOMException nonobject newTarget prototype falls back to its interface', () => {
        function Target() {}
        Target.prototype = null;
        const value = Reflect.construct(DOMException, ['detail', 'DataCloneError'], Target);
        return Object.getPrototypeOf(value) === DOMException.prototype && value.code === 25 && value.message === 'detail';
    });
    test('DOMException revoked target is rejected when fallback requires its realm', () => {
        let target;
        target = Proxy.revocable(function () {}, { get(object, key, receiver) {
            if (key === 'prototype') { target.revoke(); return null; }
            return Reflect.get(object, key, receiver);
        } });
        return typeError(() => Reflect.construct(DOMException, [], target.proxy));
    });
    test('DOMException object prototype avoids a redundant target realm lookup', () => {
        let target;
        const prototype = {};
        target = Proxy.revocable(function () {}, { get(object, key, receiver) {
            if (key === 'prototype') { target.revoke(); return prototype; }
            return Reflect.get(object, key, receiver);
        } });
        const value = Reflect.construct(DOMException, ['kept'], target.proxy);
        return Object.getPrototypeOf(value) === prototype &&
            Object.getOwnPropertyDescriptor(DOMException.prototype, 'message').get.call(value) === 'kept';
    });
    test('DOMException rejects Symbol message and name arguments', () =>
        typeError(() => new DOMException(Symbol())) && typeError(() => new DOMException('', Symbol())));
    test('DOMException subclasses preserve error slots and associated fields', () => {
        class Derived extends DOMException {}
        const value = new Derived('detail', 'DataCloneError');
        return value instanceof Derived && value instanceof DOMException && value instanceof Error &&
            value.name === 'DataCloneError' && value.message === 'detail' && value.code === 25;
    });
    test('DOMException inherits normal Error stringification with its actual fields', () =>
        String(new DOMException('detail', 'DataCloneError')) === 'DataCloneError: detail');
    return rows;
};
