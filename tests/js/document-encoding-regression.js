/* Real HTTP iframe responses exercise transport bytes and their owner realms.
 * This is custom engine coverage, not an alteration of upstream WPT tests. */
(() => {
    'use strict';
    const rows = [];
    globalThis.__documentEncodingRows = rows;
    globalThis.__documentEncodingDone = false;
    const test = (name, callback) => {
        try {
            if (callback() !== true) throw new Error('Unexpected document encoding result');
            rows.push({name, status: 'PASS'});
        } catch (error) {
            rows.push({name, status: 'FAIL', message: String(error && error.stack || error)});
        }
    };
    const throwsType = callback => {
        try { callback(); return false; } catch (error) { return error instanceof TypeError; }
    };
    const load = path => new Promise((resolve, reject) => {
        const frame = document.createElement('iframe');
        const timer = setTimeout(() => { frame.remove(); reject(new Error('HTTP frame timeout: ' + path)); }, 2000);
        frame.onload = () => { clearTimeout(timer); resolve(frame); };
        frame.onerror = () => { clearTimeout(timer); frame.remove(); reject(new Error('HTTP frame load error: ' + path)); };
        frame.src = globalThis.__documentEncodingOrigin + path;
        document.body.append(frame);
    });
    const cases = [
        ['/utf8', 'UTF-8', '\u2020', '?%E2%80%A0', '#%E2%80%A0'],
        ['/windows1252', 'windows-1252', '\u2020', '?%86', '#%E2%80%A0'],
        ['/big5', 'Big5', '\u4E00', '?%26%238224%3B', '#%E2%80%A0'],
        ['/euckr', 'EUC-KR', '\u2020', '?%A2%D3', '#%E2%80%A0'],
        ['/iso2022jp', 'ISO-2022-JP', '\u3042', '?%1B(J~%1B(B\\', '#%E2%80%BE\\'],
        ['/gb18030', 'gb18030', '\u4E2D', '?%26%2358853%3B', '#%EE%97%A5'],
        ['/shiftjis', 'Shift_JIS', '\u3042', '?%81|', '#%E2%88%92'],
        ['/iso88592', 'ISO-8859-2', '\u0104', '?%26%23162%3B', '#%C2%A2'],
        ['/bom8', 'UTF-8', '\u03A3', '?%CE%A3', '#%CE%A3'],
        ['/bom16le', 'UTF-16LE', '\u03A3', '?%CE%A3', '#%CE%A3'],
        ['/header16be', 'UTF-16BE', '\u03A3', '?%CE%A3', '#%CE%A3'],
        ['/unknown', 'UTF-8', '\u03A3', '?%CE%A3', '#%CE%A3'],
    ];
    async function run() {
        const loaded = new Map();
        for (const [path, encoding, text, search, hash] of cases) {
            let frame = null, error = null;
            try { frame = await load(path); loaded.set(path, frame); } catch (failure) { error = failure; }
            const documentFor = () => {
                if (error) throw error;
                if (!frame || !frame.contentDocument) throw new Error('Missing actual child document');
                return frame.contentDocument;
            };
            test(path + ' exposes the actual transport or BOM encoding', () =>
                documentFor().characterSet === encoding);
            test(path + ' replacement-decodes real response bytes before parsing', () =>
                documentFor().getElementById('payload').textContent === text);
            test(path + ' encodes href query separately from UTF8 fragment', () => {
                const anchor = documentFor().getElementById('link');
                return anchor.search === search && anchor.hash === hash;
            });
        }
        const legacyFrame = loaded.get('/windows1252');
        const legacyDocument = legacyFrame && legacyFrame.contentDocument;
        const legacy = () => {
            if (!legacyDocument) throw new Error('Missing legacy HTTP fixture');
            const anchor = legacyDocument.createElement('a');
            anchor.href = '?\u2020#\u2020';
            return anchor;
        };
        const characterSet = Object.getOwnPropertyDescriptor(Document.prototype, 'characterSet').get;
        const charset = Object.getOwnPropertyDescriptor(Document.prototype, 'charset').get;
        const inputEncoding = Object.getOwnPropertyDescriptor(Document.prototype, 'inputEncoding').get;
        const search = Object.getOwnPropertyDescriptor(HTMLAnchorElement.prototype, 'search');
        const href = Object.getOwnPropertyDescriptor(HTMLAnchorElement.prototype, 'href');
        test('a borrowed document encoding getter uses its genuine foreign receiver', () =>
            characterSet.call(legacyDocument) === 'windows-1252');
        test('the charset alias uses the same foreign document metadata', () =>
            charset.call(legacyDocument) === 'windows-1252');
        test('the inputEncoding alias uses the same foreign document metadata', () =>
            inputEncoding.call(legacyDocument) === 'windows-1252');
        test('a borrowed anchor getter uses the node document encoding', () =>
            search.get.call(legacy()) === '?%86');
        test('hyperlink search assignment keeps the UTF8 basic-parser default', () => {
            const anchor = legacy(); anchor.search = '?\u2020';
            return anchor.search === '?%E2%80%A0' && anchor.hash === '#%E2%80%A0';
        });
        test('a borrowed hyperlink setter also uses UTF8 assignment semantics', () => {
            const anchor = legacy(); search.set.call(anchor, '?\u2020');
            return anchor.search === '?%E2%80%A0';
        });
        test('changing another URL component preserves the existing encoded query', () => {
            const anchor = legacy(); anchor.pathname = '/\u2020';
            return anchor.pathname === '/%E2%80%A0' && anchor.search === '?%86';
        });
        test('absolute raw href assignment still uses the legacy document encoding', () => {
            const anchor = legacy(); anchor.href = 'https://example.test/?\u2020#\u2020';
            return anchor.href === 'https://example.test/?%86#%E2%80%A0';
        });
        test('modern URL construction stays UTF8 inside a legacy document realm', () => {
            const Constructor = legacyFrame.contentWindow.URL;
            return new Constructor('https://example.test/?\u2020').search === '?%E2%80%A0';
        });
        test('an area element shares genuine document-aware href resolution', () => {
            const area = legacyDocument.createElement('area'); area.href = '?\u2020';
            return area.search === '?%86';
        });
        test('base href resolution uses the document encoding', () => {
            const base = legacyDocument.createElement('base'); base.href = '?\u2020';
            legacyDocument.head.prepend(base);
            const observed = legacyDocument.baseURI;
            base.remove();
            return observed === globalThis.__documentEncodingOrigin + '/windows1252?%86';
        });
        test('a borrowed baseURI getter uses the actual foreign document', () => {
            const getter = Object.getOwnPropertyDescriptor(Document.prototype, 'baseURI').get;
            return getter.call(legacyDocument) === globalThis.__documentEncodingOrigin + '/windows1252';
        });
        test('adopting an anchor changes its parser document without rewriting its raw href', () => {
            const anchor = legacy(); document.adoptNode(anchor);
            return anchor.getAttribute('href') === '?\u2020#\u2020' && anchor.search === '?%E2%80%A0';
        });
        test('a reentrant component conversion reads the updated href', () => {
            const anchor = legacy();
            search.set.call(anchor, {toString() { anchor.href = 'https://example.test/reentered?old'; return '\u2020'; }});
            return anchor.href === 'https://example.test/reentered?%E2%80%A0';
        });
        test('USVString conversion replaces a lone surrogate before legacy query encoding', () => {
            const anchor = legacy(); anchor.href = '?\uD800';
            return anchor.search === '?%26%2365533%3B';
        });
        test('an invalid receiver rejects before author string conversion', () => {
            let converted = false;
            return throwsType(() => search.set.call({}, {toString() { converted = true; return 'x'; }})) && !converted;
        });
        test('a Symbol href throws without changing the existing raw attribute', () => {
            const anchor = legacy(), previous = anchor.getAttribute('href');
            return throwsType(() => href.set.call(anchor, Symbol('x'))) && anchor.getAttribute('href') === previous;
        });
        test('prototype imitation does not acquire an anchor receiver identity', () =>
            throwsType(() => href.get.call(Object.create(HTMLAnchorElement.prototype))));
        test('a Proxy wrapper does not acquire a genuine DOM receiver identity', () =>
            throwsType(() => href.get.call(new Proxy(legacy(), {}))));
        test('document metadata rejects a prototype-only fake', () =>
            throwsType(() => characterSet.call(Object.create(Document.prototype))));
        test('hyperlink mixin accessors require their own host interface', () => {
            const area = legacyDocument.createElement('area'); area.href = '?kept';
            const areaHref = Object.getOwnPropertyDescriptor(HTMLAreaElement.prototype, 'href');
            let conversions = 0;
            const input = {toString() { conversions++; return '?changed'; }};
            return throwsType(() => href.get.call(area)) &&
                throwsType(() => href.set.call(area, input)) &&
                throwsType(() => areaHref.get.call(legacy())) &&
                throwsType(() => areaHref.set.call(legacy(), input)) && conversions === 0;
        });
        test('hyperlink accessors are genuine named nonconstructible builtins', () => {
            for (const prototype of [HTMLAnchorElement.prototype, HTMLAreaElement.prototype]) {
                for (const key of ['href', 'protocol', 'username', 'password', 'host', 'hostname',
                                   'port', 'pathname', 'search', 'hash', 'origin']) {
                    const descriptor = Object.getOwnPropertyDescriptor(prototype, key);
                    if (!descriptor || !descriptor.enumerable || !descriptor.configurable ||
                        descriptor.get.name !== 'get ' + key || descriptor.get.length !== 0 ||
                        Object.hasOwn(descriptor.get, 'prototype') ||
                        !throwsType(() => Reflect.construct(Object, [], descriptor.get))) return false;
                    if (key === 'origin') { if (descriptor.set !== undefined) return false; }
                    else if (descriptor.set.name !== 'set ' + key || descriptor.set.length !== 1 ||
                        Object.hasOwn(descriptor.set, 'prototype') ||
                        !throwsType(() => Reflect.construct(Object, [], descriptor.set))) return false;
                }
            }
            return true;
        });
        test('hyperlink stringifiers have separate concise method identity', () => {
            for (const prototype of [HTMLAnchorElement.prototype, HTMLAreaElement.prototype]) {
                const descriptor = Object.getOwnPropertyDescriptor(prototype, 'toString');
                const getter = Object.getOwnPropertyDescriptor(prototype, 'href').get;
                if (!descriptor || !descriptor.writable || !descriptor.enumerable || !descriptor.configurable ||
                    descriptor.value === getter || descriptor.value.name !== 'toString' || descriptor.value.length !== 0 ||
                    Object.hasOwn(descriptor.value, 'prototype') ||
                    !throwsType(() => Reflect.construct(Object, [], descriptor.value))) return false;
            }
            const anchor = legacy(); Object.defineProperty(anchor, 'href', {get() { throw new Error('author href getter'); }});
            return HTMLAnchorElement.prototype.toString.call(anchor).endsWith('/windows1252?%86#%E2%80%A0');
        });
        test('href resolution is independent of the mutable public URL constructor', () => {
            const saved = globalThis.URL;
            try { globalThis.URL = function () { throw new Error('public URL invoked'); };
                return href.get.call(legacy()).endsWith('/windows1252?%86#%E2%80%A0');
            } finally { globalThis.URL = saved; }
        });
        test('component mutation uses the captured element setter', () => {
            const anchor = legacy(); anchor.setAttribute = () => { throw new Error('public setter invoked'); };
            search.set.call(anchor, 'x'); return anchor.search === '?x';
        });
        test('script-created documents retain their own UTF8 default', () => {
            const fresh = legacyDocument.implementation.createHTMLDocument('new');
            const anchor = fresh.createElement('a'); anchor.href = 'https://example.test/?\u2020';
            return fresh.characterSet === 'UTF-8' && anchor.search === '?%E2%80%A0';
        });
        let retainedAnchor = null, retainedSetupError = null;
        try { retainedAnchor = legacy(); } catch (error) { retainedSetupError = error; }
        const retained = () => { if (retainedSetupError) throw retainedSetupError; return retainedAnchor; };
        if (legacyFrame) legacyFrame.remove();
        await new Promise(resolve => setTimeout(resolve, 0));
        test('removing the child deactivates its public frame association', () =>
            !!legacyFrame && legacyFrame.contentWindow === null);
        test('a retained inactive document keeps its own immutable encoding metadata', () =>
            characterSet.call(legacyDocument) === 'windows-1252');
        test('a retained inactive anchor still resolves using its owner document', () =>
            search.get.call(retained()) === '?%86');
        test('a retained inactive anchor can use the stateless native URL mutator', () => {
            search.set.call(retained(), '\u2020'); return search.get.call(retained()) === '?%E2%80%A0';
        });
        for (const frame of loaded.values()) frame.remove();
    }
    run().catch(error => { globalThis.__documentEncodingFatal = String(error && error.stack || error); })
         .finally(() => { globalThis.__documentEncodingDone = true; });
})();
