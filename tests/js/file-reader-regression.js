/* Custom public-engine regressions. These are never upstream WPT assertions. */
async function runFileReaderRegression(mode) {
    'use strict';
    const rows = [], Reader = globalThis.FileReader, Sync = globalThis.FileReaderSync;
    const IntrinsicTypeError = TypeError, IntrinsicDOMException = DOMException;
    const equal = (actual, expected, message = '') => {
        if (!Object.is(actual, expected)) throw new Error(message + ': ' + String(actual) + ' !== ' + String(expected));
    };
    const arrayEqual = (actual, expected) => equal(JSON.stringify(actual), JSON.stringify(expected));
    const assert = (condition, message = 'assertion failed') => { if (!condition) throw new Error(message); };
    const throws = (fn, Constructor = IntrinsicTypeError, name) => {
        let value, caught = false;
        try { fn(); } catch (error) { caught = true; value = error; }
        assert(caught && value instanceof Constructor, 'expected intrinsic exception');
        if (name) equal(value.name, name);
        return value;
    };
    const check = async (name, body) => {
        try { await body(); rows.push({name, status: 'PASS'}); }
        catch (error) { rows.push({name, status: 'FAIL', message: String(error && error.stack || error)}); }
    };
    const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
    const blob = (bytes, type = '') => new Blob([new Uint8Array(bytes)], {type});
    const methods = ['readAsArrayBuffer', 'readAsBinaryString', 'readAsText', 'readAsDataURL'];
    const events = ['loadstart', 'progress', 'load', 'abort', 'error', 'loadend'];

    function read(method, input, encoding, configure) {
        const reader = new Reader(), seen = [];
        return new Promise((resolve, reject) => {
            let closed = false;
            const timer = setTimeout(() => {
                if (closed) return;
                closed = true;
                try { reader.abort(); } catch (_) {}
                reject(new Error('FileReader case deadline exceeded'));
            }, 1000);
            const close = () => {
                if (closed) return;
                closed = true; clearTimeout(timer);
                resolve({reader, seen});
            };
            for (const name of events) reader.addEventListener(name, event => {
                seen.push({name, state: reader.readyState, result: reader.result, error: reader.error,
                    loaded: event.loaded, total: event.total, lengthComputable: event.lengthComputable,
                    trusted: event.isTrusted, target: event.target === reader,
                    current: event.currentTarget === reader, phase: event.eventPhase,
                    bubbles: event.bubbles, cancelable: event.cancelable,
                    progressEvent: event instanceof ProgressEvent, event});
            });
            try {
                if (configure) configure(reader, seen);
                reader.addEventListener('loadend', close);
                const returned = method === 'readAsText' ? reader[method](input, encoding) : reader[method](input);
                equal(returned, undefined, 'read method return');
            } catch (error) {
                closed = true; clearTimeout(timer);
                try { reader.abort(); } catch (_) {}
                reject(error);
            }
        });
    }

    await check('FileReader is an own exposed interface', () => {
        assert(Object.prototype.hasOwnProperty.call(globalThis, 'FileReader'));
        equal(typeof Reader, 'function'); equal(Reader.name, 'FileReader'); equal(Reader.length, 0);
    });
    await check('FileReaderSync exposure follows its realm', () => {
        equal(typeof Sync, mode === 'worker' ? 'function' : 'undefined');
    });
    await check('reader construction and EventTarget inheritance are genuine', () => {
        const reader = new Reader();
        assert(reader instanceof Reader && reader instanceof EventTarget);
        equal(Object.getPrototypeOf(Reader.prototype), EventTarget.prototype);
        equal(Reader.prototype.constructor, Reader); throws(() => Reader());
    });
    await check('reader initial state is EMPTY with null result and error', () => {
        const reader = new Reader(); equal(reader.readyState, 0); equal(reader.result, null); equal(reader.error, null);
    });
    await check('reader constants have Web IDL descriptors on both objects', () => {
        for (const [name, value] of [['EMPTY', 0], ['LOADING', 1], ['DONE', 2]]) {
            for (const object of [Reader, Reader.prototype]) {
                const descriptor = Object.getOwnPropertyDescriptor(object, name);
                assert(descriptor && descriptor.enumerable && !descriptor.writable && !descriptor.configurable);
                equal(descriptor.value, value);
            }
        }
    });
    await check('readonly reader attributes are enumerable branded accessors', () => {
        for (const name of ['readyState', 'result', 'error']) {
            const descriptor = Object.getOwnPropertyDescriptor(Reader.prototype, name);
            assert(descriptor.enumerable && descriptor.configurable && typeof descriptor.get === 'function');
            equal(descriptor.set, undefined); equal(descriptor.get.name, 'get ' + name);
            throws(() => descriptor.get.call(Object.create(Reader.prototype)));
            throws(() => Reflect.construct(descriptor.get, []));
        }
        equal(Object.prototype.toString.call(new Reader()), '[object FileReader]');
    });
    await check('reader methods have required names arities and descriptors', () => {
        for (const name of methods.concat('abort')) {
            const descriptor = Object.getOwnPropertyDescriptor(Reader.prototype, name);
            assert(descriptor.writable && descriptor.enumerable && descriptor.configurable);
            equal(descriptor.value.name, name); equal(descriptor.value.length, name === 'abort' ? 0 : 1);
            throws(() => Reflect.construct(descriptor.value, []));
        }
    });
    await check('reader receiver conversion rejects prototype and Proxy impostors', () => {
        const input = new Blob(['a']);
        for (const receiver of [{}, Object.create(Reader.prototype), new Proxy(new Reader(), {})])
            for (const method of methods) throws(() => Reader.prototype[method].call(receiver, input));
    });
    await check('reader private state survives a changed public prototype', async () => {
        const reader = new Reader(), get = Object.getOwnPropertyDescriptor(Reader.prototype, 'readyState').get;
        Object.setPrototypeOf(reader, null);
        equal(get.call(reader), 0);
        Reader.prototype.readAsText.call(reader, new Blob(['private']));
        equal(get.call(reader), 1); Reader.prototype.abort.call(reader); equal(get.call(reader), 2);
        await wait(0);
    });
    await check('genuine readers cannot be cloned through public identity changes', () => {
        const reader = new Reader(); let reads = 0;
        for (const key of ['constructor', Symbol.toStringTag, 'publicValue'])
            Object.defineProperty(reader, key, {enumerable: true, configurable: true,
                get() { ++reads; throw new Error('reader clone must not inspect public identity'); }});
        for (const value of [reader, {reader}, [reader], new Map([['reader', reader]]), new Set([reader])])
            throws(() => structuredClone(value), IntrinsicDOMException, 'DataCloneError');
        Object.setPrototypeOf(reader, null);
        throws(() => structuredClone({reader}), IntrinsicDOMException, 'DataCloneError');
        equal(reads, 0);
    });
    await check('ordinary reader prototype impostors remain ordinary cloneable objects', () => {
        const impostor = Object.create(Reader.prototype); impostor.value = 7;
        const copy = structuredClone({impostor});
        equal(Object.getPrototypeOf(copy.impostor), Object.prototype); equal(copy.impostor.value, 7);
    });
    await check('Blob conversion rejects missing primitive and forged arguments', () => {
        for (const method of methods) {
            const reader = new Reader(); throws(() => reader[method]());
            for (const input of [undefined, null, 'text', {}, Object.create(Blob.prototype), new Proxy(new Blob(), {})])
                throws(() => reader[method](input));
            equal(reader.readyState, 0);
        }
    });
    await check('Blob validation precedes optional encoding conversion', () => {
        const reader = new Reader(); let converted = 0;
        throws(() => reader.readAsText({}, {toString() { ++converted; return 'utf-8'; }}));
        equal(converted, 0); throws(() => reader.readAsText(new Blob(), Symbol('encoding')));
        equal(reader.readyState, 0);
    });
    await check('busy reads still convert encoding before InvalidStateError', () => {
        const reader = new Reader(), input = new Blob(['a']); let converted = 0;
        reader.readAsText(input);
        try {
            throws(() => reader.readAsText(input, {toString() { ++converted; return 'utf-8'; }}), IntrinsicDOMException, 'InvalidStateError');
            equal(converted, 1);
            for (const method of methods) throws(() => reader[method](input), IntrinsicDOMException, 'InvalidStateError');
        } finally { reader.abort(); }
    });
    await check('reentrant encoding conversion observes the current read state', async () => {
        const reader = new Reader(), input = new Blob(['replacement']);
        reader.readAsText(new Blob(['old']));
        reader.readAsText(input, {toString() { reader.abort(); return 'utf-8'; }});
        equal(reader.readyState, 1);
        await new Promise((resolve, reject) => {
            const timer = setTimeout(() => reject(new Error('reentrant read deadline')), 1000);
            reader.addEventListener('loadend', () => { clearTimeout(timer); resolve(); }, {once: true});
        });
        equal(reader.result, 'replacement');
    });
    await check('reader event handlers start null and retain object assignments', () => {
        const reader = new Reader();
        for (const event of events) {
            const name = 'on' + event; equal(reader[name], null);
            reader[name] = 42; equal(reader[name], null);
            const object = {}; reader[name] = object; equal(reader[name], object);
            reader[name] = null; equal(reader[name], null);
        }
    });
    await check('handler replacement preserves ordinary listener order', () => {
        const reader = new Reader(), order = [];
        reader.addEventListener('load', () => order.push('before'));
        reader.onload = () => order.push('old');
        reader.addEventListener('load', () => order.push('after'));
        reader.onload = () => order.push('replacement');
        reader.dispatchEvent(new Event('load'));
        arrayEqual(order, ['before', 'replacement', 'after']);
    });
    await check('nulling and reinstating a handler registers at the new position', () => {
        const reader = new Reader(), order = [];
        reader.onload = () => order.push('old');
        reader.addEventListener('load', () => order.push('listener'));
        reader.onload = null; reader.onload = () => order.push('new');
        reader.dispatchEvent(new Event('load')); arrayEqual(order, ['listener', 'new']);
    });
    await check('public dispatchEvent does not force an author-stack checkpoint', async () => {
        const reader = new Reader(), order = [];
        reader.addEventListener('load', () => { order.push('first'); Promise.resolve().then(() => order.push('microtask')); });
        reader.addEventListener('load', () => order.push('second'));
        reader.dispatchEvent(new Event('load')); order.push('returned');
        arrayEqual(order, ['first', 'second', 'returned']);
        await Promise.resolve(); arrayEqual(order, ['first', 'second', 'returned', 'microtask']);
    });
    await check('abort on EMPTY does not emit events or change state', () => {
        const reader = new Reader(), order = [];
        for (const name of events) reader.addEventListener(name, () => order.push(name));
        equal(reader.abort(), undefined); equal(reader.readyState, 0); equal(reader.result, null); arrayEqual(order, []);
    });
    await check('abort on LOADING is synchronous and cancels queued events', async () => {
        const reader = new Reader(), order = [];
        for (const name of events) reader.addEventListener(name, () => order.push(name));
        reader.readAsText(new Blob(['cancel'])); equal(reader.readyState, 1);
        equal(reader.abort(), undefined); equal(reader.readyState, 2); equal(reader.result, null); equal(reader.error, null);
        arrayEqual(order, ['abort', 'loadend']); await wait(20); arrayEqual(order, ['abort', 'loadend']);
    });
    await check('abort on DONE clears the result without more events', async () => {
        const {reader, seen} = await read('readAsText', new Blob(['done']));
        const before = seen.length; reader.abort(); equal(reader.readyState, 2); equal(reader.result, null); equal(seen.length, before);
    });
    await check('author-stack abort does not drain listener promises early', async () => {
        const reader = new Reader(), order = [];
        reader.onabort = () => { order.push('abort'); Promise.resolve().then(() => order.push('promise')); };
        reader.onloadend = () => order.push('end');
        reader.readAsText(new Blob(['a'])); reader.abort(); order.push('returned');
        arrayEqual(order, ['abort', 'end', 'returned']);
        await Promise.resolve(); arrayEqual(order, ['abort', 'end', 'returned', 'promise']);
    });
    await check('ArrayBuffer results copy all bytes into a new buffer', async () => {
        const input = new Uint8Array([0, 127, 128, 255]), source = new Blob([input]); input.fill(42);
        const first = (await read('readAsArrayBuffer', source)).reader.result;
        assert(first instanceof ArrayBuffer); arrayEqual(Array.from(new Uint8Array(first)), [0, 127, 128, 255]);
        new Uint8Array(first)[0] = 99;
        const second = (await read('readAsArrayBuffer', source)).reader.result;
        assert(second !== first); arrayEqual(Array.from(new Uint8Array(second)), [0, 127, 128, 255]);
    });
    await check('BinaryString retains every byte including NUL and high bytes', async () => {
        const input = Array.from({length: 256}, (_, index) => index);
        const result = (await read('readAsBinaryString', blob(input))).reader.result;
        equal(result.length, 256);
        for (let index = 0; index < 256; ++index) equal(result.charCodeAt(index), index);
    });
    await check('DataURL handles one-byte base64 padding', async () => equal((await read('readAsDataURL', blob([255], 'text/plain'))).reader.result, 'data:text/plain;base64,/w=='));
    await check('DataURL handles two-byte base64 padding', async () => equal((await read('readAsDataURL', blob([0, 255], 'x/test'))).reader.result, 'data:x/test;base64,AP8='));
    await check('DataURL handles complete base64 blocks', async () => equal((await read('readAsDataURL', blob([0, 127, 255], 'x/test'))).reader.result, 'data:x/test;base64,AH//'));
    await check('DataURL preserves the established empty-type browser form', async () => equal((await read('readAsDataURL', new Blob())).reader.result, 'data:application/octet-stream;base64,'));
    await check('reader MIME uses the immutable Blob type rather than a public getter', async () => {
        const input = blob([0x80], 'text/plain;charset=windows-1252');
        Object.defineProperty(input, 'type', {get() { throw new Error('public Blob.type getter must not run'); }, configurable: true});
        equal((await read('readAsText', input)).reader.result, '\u20ac');
        equal((await read('readAsDataURL', input)).reader.result, 'data:text/plain;charset=windows-1252;base64,gA==');
    });
    await check('reader bytes ignore mutable public Blob methods and size', async () => {
        const input = new Blob(['private-bytes']);
        for (const name of ['size', 'arrayBuffer', 'stream', 'text'])
            Object.defineProperty(input, name, {get() { throw new Error('public Blob accessor must not run'); }, configurable: true});
        equal((await read('readAsText', input)).reader.result, 'private-bytes');
    });
    await check('successful event sequence has one start progress load and end', async () => {
        const {seen} = await read('readAsText', new Blob(['events']));
        arrayEqual(seen.map(event => event.name), ['loadstart', 'progress', 'load', 'loadend']);
    });
    await check('event state and result reflect LOADING then DONE', async () => {
        const {seen} = await read('readAsText', new Blob(['state']));
        for (const event of seen) {
            equal(event.state, event.name === 'loadstart' || event.name === 'progress' ? 1 : 2);
            equal(event.result, event.state === 1 ? null : 'state'); equal(event.error, null);
        }
    });
    await check('reader events have ProgressEvent target and trusted flags', async () => {
        const {seen} = await read('readAsText', new Blob(['1234']));
        for (const event of seen) {
            assert(event.progressEvent && event.trusted && event.target && event.current);
            equal(event.phase, 2); equal(event.bubbles, false); equal(event.cancelable, false);
            equal(event.total, 4); equal(event.loaded, event.name === 'loadstart' ? 0 : 4); assert(event.lengthComputable);
        }
    });
    await check('event currentTarget and phase reset after the whole task', async () => {
        const {seen} = await read('readAsText', new Blob(['after'])); await wait(0);
        for (const {event} of seen) { equal(event.currentTarget, null); equal(event.eventPhase, 0); }
    });
    await check('empty Blob completes asynchronously with an empty result', async () => {
        const reader = new Reader(); let called = false;
        reader.onload = () => { called = true; };
        reader.readAsText(new Blob()); equal(reader.readyState, 1); equal(called, false);
        await new Promise((resolve, reject) => {
            const timer = setTimeout(() => reject(new Error('empty read deadline')), 1000);
            reader.onloadend = () => { clearTimeout(timer); resolve(); };
        });
        equal(reader.result, ''); equal(reader.readyState, 2); equal(called, true);
    });
    await check('abort from loadstart suppresses progress and completion', async () => {
        const {seen, reader} = await read('readAsText', new Blob(['cancel']), undefined, target => { target.onloadstart = () => target.abort(); });
        arrayEqual(seen.map(event => event.name), ['loadstart', 'abort', 'loadend']); equal(reader.result, null);
    });
    await check('abort from progress suppresses load and stale completion', async () => {
        const {seen, reader} = await read('readAsText', new Blob(['cancel']), undefined, target => { target.onprogress = () => target.abort(); });
        await wait(0); arrayEqual(seen.map(event => event.name), ['loadstart', 'progress', 'abort', 'loadend']); equal(reader.result, null);
    });
    await check('new read in load suppresses the superseded loadend', async () => {
        let loads = 0;
        const {seen, reader} = await read('readAsText', new Blob(['first']), undefined, target => {
            target.onload = () => { if (++loads === 1) target.readAsText(new Blob(['second'])); };
        });
        equal(loads, 2); equal(reader.result, 'second'); equal(seen.filter(event => event.name === 'loadend').length, 1);
    });
    await check('nested load-listener promises can begin a new read before loadend', async () => {
        let loads = 0;
        const {seen, reader} = await read('readAsText', new Blob(['first']), undefined, target => {
            target.addEventListener('load', () => {
                if (++loads === 1) Promise.resolve().then(() => Promise.resolve().then(() => target.readAsText(new Blob(['second']))));
            });
        });
        equal(loads, 2); equal(reader.result, 'second'); equal(seen.filter(event => event.name === 'loadend').length, 1);
    });
    await check('queued read listeners receive complete checkpoints between callbacks', async () => {
        const order = [];
        await read('readAsText', new Blob(['a']), undefined, reader => {
            reader.addEventListener('load', () => { order.push('first'); Promise.resolve().then(() => Promise.resolve().then(() => order.push('nested'))); });
            reader.addEventListener('load', () => order.push('second'));
        });
        arrayEqual(order, ['first', 'nested', 'second']);
    });
    await check('throwing listeners retain reports and both reaction checkpoints', async () => {
        const order = [], previous = globalThis.onerror;
        globalThis.onerror = (message, source, line, column, error) => {
            if (error && error.message === 'reader-listener-marker') {
                order.push('error'); Promise.resolve().then(() => order.push('error-promise')); return true;
            }
            return false;
        };
        try {
            await read('readAsText', new Blob(['a']), undefined, reader => {
                reader.addEventListener('load', () => {
                    order.push('first'); Promise.resolve().then(() => order.push('listener-promise'));
                    throw new Error('reader-listener-marker');
                });
                reader.addEventListener('load', () => order.push('second'));
            });
            arrayEqual(order, ['first', 'listener-promise', 'error', 'error-promise', 'second']);
        } finally { globalThis.onerror = previous; }
    });
    await check('a loadstart reaction can abort before the next read task', async () => {
        const {seen} = await read('readAsText', new Blob(['a']), undefined, reader => {
            reader.onloadstart = () => Promise.resolve().then(() => Promise.resolve().then(() => reader.abort()));
        });
        await wait(0); arrayEqual(seen.map(event => event.name), ['loadstart', 'abort', 'loadend']);
    });
    await check('a new read from abort suppresses the old synchronous loadend', async () => {
        const reader = new Reader(), order = [];
        for (const name of events) reader.addEventListener(name, () => order.push(name));
        reader.onabort = () => reader.readAsText(new Blob(['next']));
        reader.readAsText(new Blob(['old'])); reader.abort(); arrayEqual(order, ['abort']);
        await new Promise((resolve, reject) => {
            const timer = setTimeout(() => reject(new Error('abort chaining deadline')), 1000);
            reader.onloadend = () => { clearTimeout(timer); resolve(); };
        });
        equal(reader.result, 'next'); equal(order.filter(name => name === 'loadend').length, 1);
    });
    await check('listener removal during a checkpoint prevents later invocation', async () => {
        const order = [];
        await read('readAsText', new Blob(['a']), undefined, reader => {
            const later = () => order.push('removed');
            reader.addEventListener('load', () => { order.push('first'); Promise.resolve().then(() => reader.removeEventListener('load', later)); });
            reader.addEventListener('load', later);
            reader.addEventListener('load', () => order.push('last'));
        });
        arrayEqual(order, ['first', 'last']);
    });
    await check('stopImmediatePropagation does not suppress the separate loadend event', async () => {
        const order = [];
        await read('readAsText', new Blob(['a']), undefined, reader => {
            reader.addEventListener('load', event => { order.push('load'); event.stopImmediatePropagation(); });
            reader.addEventListener('load', () => order.push('unexpected'));
            reader.addEventListener('loadend', () => order.push('end'));
        });
        arrayEqual(order, ['load', 'end']);
    });
    await check('read methods retain intrinsic errors after global constructors change', () => {
        const savedType = globalThis.TypeError, savedDOM = globalThis.DOMException;
        globalThis.TypeError = function ChangedTypeError() {}; globalThis.DOMException = function ChangedDOMException() {};
        const reader = new Reader(), input = new Blob(['a']);
        try {
            throws(() => reader.readAsText(input, Symbol('label')));
            reader.readAsText(input); throws(() => reader.readAsText(input), IntrinsicDOMException, 'InvalidStateError');
        } finally { reader.abort(); globalThis.TypeError = savedType; globalThis.DOMException = savedDOM; }
    });

    const codecCases = [
        ['UTF8 default and NUL', [0x61, 0, 0xce, 0xa3], '', undefined, 'a\0\u03a3'],
        ['UTF8 malformed continuation', [0xe1, 0x80, 0x41], '', undefined, '\ufffdA'],
        ['UTF8 BOM overrides legacy label', [0xef, 0xbb, 0xbf, 0xce, 0xa3], '', 'windows-1252', '\u03a3'],
        ['UTF16BE BOM overrides UTF8', [0xfe, 0xff, 0, 0x41], '', 'utf-8', 'A'],
        ['UTF16LE BOM overrides explicit BE', [0xff, 0xfe, 0x41, 0], '', 'utf-16be', 'A'],
        ['UTF16LE explicit', [0x41, 0, 0xa3, 3], '', 'utf-16le', 'A\u03a3'],
        ['UTF16BE explicit', [0, 0x41, 3, 0xa3], '', 'utf-16be', 'A\u03a3'],
        ['Windows1252 explicit', [0x80, 0x93], '', 'windows-1252', '\u20ac\u201c'],
        ['Latin1 label maps Windows1252', [0x80], '', 'ISO-8859-1', '\u20ac'],
        ['MIME charset fallback', [0x80], 'text/plain;charset=windows-1252', undefined, '\u20ac'],
        ['invalid label uses MIME', [0x80], 'text/plain;charset=windows-1252', 'unknown', '\u20ac'],
        ['empty label uses MIME', [0x80], 'text/plain;charset=windows-1252', '', '\u20ac'],
        ['NUL label cannot truncate', [0x80], 'text/plain;charset=windows-1252', 'utf-8\0suffix', '\u20ac'],
        ['MIME first duplicate wins', [0x80], 'text/plain;charset=windows-1252;charset=utf-8', undefined, '\u20ac'],
        ['quoted MIME charset', [0x80], 'text/plain;charset="windows-1252"', undefined, '\u20ac'],
        ['invalid MIME defaults UTF8', [0xce, 0xa3], 'not-a-mime;charset=windows-1252', undefined, '\u03a3'],
        ['explicit label overrides MIME', [0x80], 'text/plain;charset=utf-8', 'windows-1252', '\u20ac'],
        ['ShiftJIS', [0x82, 0xa0], '', 'shift_jis', '\u3042'],
        ['EUCJP', [0xa4, 0xa2], '', 'euc-jp', '\u3042'],
        ['ISO2022JP', [0x1b, 0x24, 0x42, 0x24, 0x22, 0x1b, 0x28, 0x42], '', 'iso-2022-jp', '\u3042'],
        ['GB18030 four-byte scalar', [0x90, 0x30, 0x81, 0x30], '', 'gb18030', '\ud800\udc00'],
        ['Big5 combining mapping', [0x88, 0x62], '', 'big5', '\u00ca\u0304'],
        ['EUCKR', [0xb0, 0xa1], '', 'euc-kr', '\uac00'],
        ['replacement label', [0x61, 0x62], '', 'iso-2022-kr', '\ufffd'],
        ['x-user-defined', [0x80, 0xff], '', 'x-user-defined', '\uf780\uf7ff'],
        ['UTF16 pending surrogate and odd EOF', [0, 0xd8, 0], '', 'utf-16le', '\ufffd'],
        ['GB18030 pending digit EOF', [0x81, 0x30], '', 'gb18030', '\ufffd'],
        ['ISO2022JP incomplete escape', [0x1b, 0x24], '', 'iso-2022-jp', '\ufffd$'],
    ];
    for (const [name, bytes, type, encoding, expected] of codecCases)
        await check('asynchronous text decoding: ' + name, async () => equal((await read('readAsText', blob(bytes, type), encoding)).reader.result, expected));

    if (mode === 'worker') {
        await check('FileReaderSync interface constructor has the correct shape', () => {
            equal(Sync.name, 'FileReaderSync'); equal(Sync.length, 0); throws(() => Sync());
            const reader = new Sync(); equal(Object.getPrototypeOf(reader), Sync.prototype);
            equal(Object.getPrototypeOf(Sync.prototype), Object.prototype);
            equal(Object.prototype.toString.call(reader), '[object FileReaderSync]');
        });
        await check('FileReaderSync methods use genuine descriptors and receiver brands', () => {
            for (const method of methods) {
                const descriptor = Object.getOwnPropertyDescriptor(Sync.prototype, method);
                assert(descriptor.enumerable && descriptor.writable && descriptor.configurable);
                equal(descriptor.value.name, method); equal(descriptor.value.length, 1);
                throws(() => Reflect.construct(descriptor.value, []));
                throws(() => descriptor.value.call(Object.create(Sync.prototype), new Blob()));
                throws(() => descriptor.value.call(new Proxy(new Sync(), {}), new Blob()));
            }
        });
        await check('FileReaderSync performs required Blob and encoding conversions', () => {
            const reader = new Sync();
            for (const method of methods) { throws(() => reader[method]()); throws(() => reader[method]({})); }
            throws(() => reader.readAsText(new Blob(), Symbol('encoding')));
            let converted = 0;
            throws(() => reader.readAsText({}, {toString() { ++converted; return 'utf-8'; }})); equal(converted, 0);
        });
        await check('FileReaderSync state survives a replaced public prototype', () => {
            const reader = new Sync(); Object.setPrototypeOf(reader, null);
            equal(Sync.prototype.readAsText.call(reader, new Blob(['retained'])), 'retained');
        });
        await check('genuine synchronous readers retain nonserializable private identity', () => {
            const reader = new Sync(); let reads = 0;
            Object.defineProperty(reader, Symbol.toStringTag, {get() { ++reads; throw new Error('public tag'); }});
            Object.defineProperty(reader, 'value', {enumerable: true, get() { ++reads; throw new Error('public value'); }});
            throws(() => structuredClone(reader), IntrinsicDOMException, 'DataCloneError');
            throws(() => structuredClone({reader}), IntrinsicDOMException, 'DataCloneError');
            Object.setPrototypeOf(reader, null);
            throws(() => structuredClone([reader]), IntrinsicDOMException, 'DataCloneError');
            equal(reads, 0);
        });
        await check('ordinary synchronous reader prototype impostors can be cloned', () => {
            const impostor = Object.create(Sync.prototype); impostor.value = 9;
            const copy = structuredClone(impostor);
            equal(Object.getPrototypeOf(copy), Object.prototype); equal(copy.value, 9);
        });
        await check('FileReaderSync ArrayBuffer outputs are independent copies', () => {
            const reader = new Sync(), input = blob([0, 128, 255]);
            const first = reader.readAsArrayBuffer(input), second = reader.readAsArrayBuffer(input);
            assert(first !== second && first instanceof ArrayBuffer && second instanceof ArrayBuffer);
            new Uint8Array(first)[0] = 99; arrayEqual(Array.from(new Uint8Array(second)), [0, 128, 255]);
        });
        await check('FileReaderSync BinaryString retains every byte', () => {
            const reader = new Sync(), bytes = Array.from({length: 256}, (_, index) => index);
            const result = reader.readAsBinaryString(blob(bytes)); equal(result.length, 256);
            for (let index = 0; index < 256; ++index) equal(result.charCodeAt(index), index);
        });
        await check('FileReaderSync DataURL handles binary data and MIME', () => {
            const reader = new Sync(); equal(reader.readAsDataURL(blob([0, 127, 255], 'x/test')), 'data:x/test;base64,AH//');
            equal(reader.readAsDataURL(new Blob()), 'data:application/octet-stream;base64,');
        });
        await check('FileReaderSync does not invoke shadowed public Blob operations', () => {
            const input = blob([0x80], 'text/plain;charset=windows-1252');
            for (const name of ['type', 'size', 'stream', 'text', 'arrayBuffer'])
                Object.defineProperty(input, name, {get() { throw new Error('public getter'); }, configurable: true});
            equal(new Sync().readAsText(input), '\u20ac');
        });
        for (const [name, bytes, type, encoding, expected] of codecCases)
            await check('synchronous text decoding: ' + name, () => equal(new Sync().readAsText(blob(bytes, type), encoding), expected));
    }
    return rows;
}
