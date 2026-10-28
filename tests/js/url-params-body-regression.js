// Public-API regression coverage; this file is not an upstream WPT result.
// The same function runs in a real window realm and a real dedicated worker.
globalThis.runURLParamsBodyRegression = async function (realm) {
    'use strict';
    const rows = [];
    async function test(name, body) {
        try {
            if (await body() !== true) throw new Error('Public API result differs from its specified value');
            rows.push({ name, status: 'PASS' });
        } catch (error) {
            rows.push({ name, status: 'FAIL', message: String(error && error.stack || error) });
        }
    }
    function throwsTypeError(body) {
        try { body(); return false; } catch (error) { return error instanceof TypeError; }
    }
    async function rejectsTypeError(body) {
        try { await body(); return false; } catch (error) { return error instanceof TypeError; }
    }
    const pairs = value => JSON.stringify([...value]);
    await test('optional constructor defaults differ from explicit null USVString', () =>
        String(new URLSearchParams()) === '' && String(new URLSearchParams(undefined)) === '' &&
        String(new URLSearchParams(null)) === 'null=');
    await test('only one leading question mark is removed', () =>
        new URLSearchParams('??q=1').get('?q') === '1');
    await test('function objects use enumerable record fields', () => {
        function record() {} record.alpha = 'one'; record.beta = 'two';
        return String(new URLSearchParams(record)) === 'alpha=one&beta=two';
    });
    await test('record keys collapse after USVString conversion without changing insertion order', () =>
        pairs(new URLSearchParams({ '\ud800x': 'first', middle: 'kept', '\ud801x': 'last' })) ===
        JSON.stringify([['\ufffdx', 'last'], ['middle', 'kept']]));
    await test('enumerable symbol keys throw before their value getter runs', () => {
        let read = false;
        const record = Object.defineProperty({}, Symbol('key'), { enumerable: true, get() { read = true; return 'value'; } });
        return throwsTypeError(() => new URLSearchParams(record)) && !read;
    });
    await test('record conversion observes current descriptors and skips a deleted later property', () => {
        const record = { get first() { delete record.second; return 'one'; }, second: 'two' };
        return String(new URLSearchParams(record)) === 'first=one';
    });
    await test('a non-enumerable symbol does not poison record conversion', () => {
        const record = { key: 'value' }; Object.defineProperty(record, Symbol('ignored'), { value: 1 });
        return String(new URLSearchParams(record)) === 'key=value';
    });
    await test('an existing params object still uses its custom iterator', () => {
        const source = new URLSearchParams('old=1');
        source[Symbol.iterator] = function* () { yield ['new', '2']; };
        return String(new URLSearchParams(source)) === 'new=2';
    });
    await test('the outer iterator getter is evaluated once', () => {
        let reads = 0;
        const source = { get [Symbol.iterator]() { reads++; return function* () { yield ['q', 'v']; }; } };
        return String(new URLSearchParams(source)) === 'q=v' && reads === 1;
    });
    await test('a noncallable iterator is not silently treated as a record', () =>
        throwsTypeError(() => new URLSearchParams({ [Symbol.iterator]: 1, q: 'v' })));
    await test('primitive inner strings are not sequence objects', () =>
        throwsTypeError(() => new URLSearchParams(['ab'])));
    await test('boxed inner strings use their actual iterator', () =>
        String(new URLSearchParams([new String('ab')])) === 'a=b');
    await test('short and long inner sequences both reject', () =>
        throwsTypeError(() => new URLSearchParams([['a']])) &&
        throwsTypeError(() => new URLSearchParams([['a', 'b', 'c']])));
    await test('sequence conversion completes before constructor pair-size validation', () => {
        const marker = {}, later = { toString() { throw marker; } };
        try { new URLSearchParams([['short'], ['later', later]]); return false; }
        catch (error) { return error === marker; }
    });
    await test('record getter exceptions retain their original identity', () => {
        const marker = {};
        try { new URLSearchParams({ get q() { throw marker; } }); return false; }
        catch (error) { return error === marker; }
    });
    await test('converted sequence inputs and output pairs are copied', () => {
        const source = [['q', 'one']], params = new URLSearchParams(source), first = params.entries().next().value;
        source[0][1] = 'changed'; first[1] = 'also changed';
        return params.get('q') === 'one';
    });
    await test('mixed valid and malformed percent escapes decode independently', () =>
        new URLSearchParams('q=%2sf%2a&other=%2%2af%2a&third=%%2a').get('q') === '%2sf*' &&
        new URLSearchParams('other=%2%2af%2a').get('other') === '%2*f*' &&
        new URLSearchParams('third=%%2a').get('third') === '%*');
    await test('plus is decoded before percent bytes so encoded plus remains literal', () =>
        new URLSearchParams('q=+%2B%20').get('q') === ' + ');
    await test('UTF8 decoder reconsumes an invalid continuation', () =>
        new URLSearchParams('q=%C2x%E2%82y').get('q') === '\ufffdx\ufffdy');
    await test('UTF8 decoder rejects overlong sequences surrogates and out-of-range scalars', () =>
        new URLSearchParams('q=%C0%AF%ED%A0%80%F4%90%80%80').get('q') === '\ufffd'.repeat(9));
    await test('UTF8 decoder emits one replacement for a truncated pending sequence', () =>
        new URLSearchParams('q=%F0%9F%92').get('q') === '\ufffd');
    await test('raw and percent-encoded byte-order marks remain query data', () =>
        pairs(new URLSearchParams('\ufeffq=%EF%BB%BFv')) === JSON.stringify([['\ufeffq', '\ufeffv']]));
    await test('NULL and non-BMP characters survive query round trips', () => {
        const params = new URLSearchParams([['\0\ud83d\udca9', '\0\ud83d\udca9']]);
        return pairs(new URLSearchParams(String(params))) === pairs(params);
    });
    await test('empty fields are skipped while duplicate names and first equals are preserved', () =>
        pairs(new URLSearchParams('&&a=1&&a=2=3&=empty&tail&')) ===
        JSON.stringify([['a', '1'], ['a', '2=3'], ['', 'empty'], ['tail', '']]));
    await test('form serialization uses its exact percent-encode set', () =>
        String(new URLSearchParams([['q', " AZaz09*-._~!'()+\0"]])) === 'q=+AZaz09*-._%7E%21%27%28%29%2B%00');
    await test('mutating methods normalize lone surrogate names and values', () => {
        const params = new URLSearchParams(); params.append('\ud800', '\udc00'); params.set('\ud801', 'changed');
        return params.size === 1 && params.get('\ufffd') === 'changed' && params.has('\ud802');
    });
    await test('get converts a name exactly once even for multiple entries', () => {
        const params = new URLSearchParams('a=1&q=2&q=3'); let count = 0;
        return params.get({ toString() { count++; return 'q'; } }) === '2' && count === 1;
    });
    await test('empty lookups still perform mandatory argument conversion', () => {
        const marker = {};
        try { new URLSearchParams().get({ toString() { throw marker; } }); return false; }
        catch (error) { return error === marker; }
    });
    await test('delete optional value conversion runs once and explicit undefined means omitted', () => {
        const params = new URLSearchParams('q=1&q=2&q=2'); let count = 0;
        params.delete('q', { toString() { count++; return '2'; } });
        if (String(params) !== 'q=1' || count !== 1) return false;
        params.delete('q', undefined); return params.size === 0;
    });
    await test('has optional value distinguishes present empty values', () => {
        const params = new URLSearchParams('q=&q=2');
        return params.has('q', '') && !params.has('q', '3') && params.has('q', undefined);
    });
    await test('set preserves first position while removing later duplicates', () => {
        const params = new URLSearchParams('a=1&q=old&b=2&q=later'); params.set('q', 'new');
        return String(params) === 'a=1&q=new&b=2';
    });
    await test('set reads current linked list after reentrant value conversion', () => {
        const url = new URL('https://example.test/?old=1'), params = url.searchParams;
        params.set('q', { toString() { url.search = 'current=2'; return 'value'; } });
        return String(params) === 'current=2&q=value' && url.search === '?current=2&q=value';
    });
    await test('delete reads current linked list after reentrant name conversion', () => {
        const url = new URL('https://example.test/?old=1'), params = url.searchParams;
        params.delete({ toString() { url.search = 'current=2&kept=3'; return 'current'; } });
        return String(params) === 'kept=3' && url.search === '?kept=3';
    });
    await test('required method arguments throw before mutation', () => {
        const params = new URLSearchParams('q=kept');
        return ['append', 'delete', 'get', 'getAll', 'has', 'set', 'forEach'].every(name => throwsTypeError(() => params[name]())) &&
            throwsTypeError(() => params.append('q')) && throwsTypeError(() => params.set('q')) && String(params) === 'q=kept';
    });
    await test('all query methods reject Symbol string arguments', () => {
        const params = new URLSearchParams('q=kept');
        return ['append', 'delete', 'get', 'getAll', 'has', 'set'].every(name =>
            throwsTypeError(() => params[name](Symbol('q'), 'value'))) &&
            throwsTypeError(() => params.append('q', Symbol('value'))) && String(params) === 'q=kept';
    });
    await test('public methods reject forged receivers before argument side effects', () => {
        let converted = false; const value = { toString() { converted = true; return 'q'; } };
        const fake = Object.create(URLSearchParams.prototype);
        return ['append', 'delete', 'get', 'getAll', 'has', 'set', 'sort', 'forEach', 'keys', 'values', 'entries', 'toString']
            .every(name => throwsTypeError(() => URLSearchParams.prototype[name].call(fake, value, value))) && !converted &&
            throwsTypeError(() => Object.getOwnPropertyDescriptor(URLSearchParams.prototype, 'size').get.call(fake));
    });
    await test('sort is stable and compares UTF16 code units', () => {
        const params = new URLSearchParams([['\ufffd', 'last'], ['\ud83d\udca9', 'first'], ['a', '1'], ['a', '2']]);
        params.sort(); return pairs(params) === JSON.stringify([['a', '1'], ['a', '2'], ['\ud83d\udca9', 'first'], ['\ufffd', 'last']]);
    });
    await test('iterators observe removal of the next pair', () => {
        const params = new URLSearchParams('a=1&b=2&c=3'), values = [];
        for (const pair of params) { values.push(pair); if (pair[0] === 'a') params.delete('b'); }
        return JSON.stringify(values) === JSON.stringify([['a', '1'], ['c', '3']]);
    });
    await test('iterators advance their index after removal of the current pair', () => {
        const params = new URLSearchParams('a=1&b=2&c=3'), values = [];
        for (const pair of params) { values.push(pair[0]); params.delete(pair[0]); }
        return values.join(',') === 'a,c' && String(params) === 'b=2';
    });
    await test('iterators read a replacement list after URL search mutation', () => {
        const url = new URL('https://example.test/?a=1&b=2&c=3'), iterator = url.searchParams.entries();
        const first = iterator.next().value; url.search = 'x=4&y=5&z=6';
        return first[0] === 'a' && iterator.next().value[0] === 'y' && iterator.next().value[0] === 'z' && iterator.next().done;
    });
    await test('iterators retain their index when an exhausted list later grows', () => {
        const params = new URLSearchParams(), iterator = params.keys();
        if (!iterator.next().done) return false;
        params.append('later', 'value'); return iterator.next().value === 'later';
    });
    await test('forEach reads live state and passes value key and owner', () => {
        const url = new URL('https://example.test/?a=1&b=2&c=3'), params = url.searchParams, receiver = {}, seen = [];
        params.forEach(function (value, key, owner) {
            if (this !== receiver || owner !== params) throw new Error('Bad callback receiver');
            seen.push([key, value]); if (key === 'a') url.search = 'x=4&y=5&z=6';
        }, receiver);
        return JSON.stringify(seen) === JSON.stringify([['a', '1'], ['y', '5'], ['z', '6']]);
    });
    await test('forEach sees appends but does not revisit removed current entries', () => {
        const params = new URLSearchParams('a=1&b=2'), seen = [];
        params.forEach((value, key) => { seen.push(key); if (key === 'a') { params.delete('a'); params.append('c', '3'); } });
        return seen.join(',') === 'a,c';
    });
    await test('linked params preserve identity through href and query mutations', () => {
        const url = new URL('https://example.test/?a=1'), params = url.searchParams;
        url.href = 'data:example?b=2'; params.append('c', 'two words');
        return params === url.searchParams && url.href === 'data:example?b=2&c=two+words';
    });
    await test('linked params update internal URL state without an overridable _set hook', () => {
        const url = new URL('https://example.test/'); url._set = () => { throw new Error('User hook should not run'); };
        url.searchParams.append('q', 'v'); return url.href === 'https://example.test/?q=v';
    });
    await test('clearing params removes a query while retaining opaque path bytes', () => {
        const url = new URL('data:space ?q=1'); url.searchParams.delete('q');
        return url.href === 'data:space%20' && url.searchParams.size === 0;
    });
    await test('interface methods have their required lengths and enumerable descriptors', () => {
        const lengths = { append: 2, delete: 1, get: 1, getAll: 1, has: 1, set: 2, sort: 0, forEach: 1, keys: 0, values: 0, entries: 0, toString: 0 };
        return URLSearchParams.length === 0 && Object.entries(lengths).every(([key, length]) =>
            URLSearchParams.prototype[key].length === length && Object.getOwnPropertyDescriptor(URLSearchParams.prototype, key).enumerable) &&
            URLSearchParams.prototype[Symbol.iterator] === URLSearchParams.prototype.entries;
    });
    await test('query and iterator tags have the correct branded behavior', () => {
        const params = new URLSearchParams('q=v'), iterator = params.entries(), next = iterator.next;
        return Object.prototype.toString.call(params) === '[object URLSearchParams]' &&
            Object.prototype.toString.call(iterator) === '[object URLSearchParams Iterator]' &&
            iterator[Symbol.iterator]() === iterator && throwsTypeError(() => next.call({}));
    });
    await test('query state is not exposed as mutable own list fields', () =>
        Object.getOwnPropertyNames(new URLSearchParams('q=v')).length === 0);
    await test('query codec captures byte-array intrinsics before page monkeypatching', () => {
        const Original = globalThis.Uint8Array, subarray = Original.prototype.subarray;
        try {
            globalThis.Uint8Array = function () { throw new Error('Page constructor'); };
            Original.prototype.subarray = function () { throw new Error('Page method'); };
            return new URLSearchParams('?q=%C2x').get('q') === '\ufffdx' &&
                String(new URLSearchParams([['q', '\ud83d\udca9']])) === 'q=%F0%9F%92%A9';
        } finally { globalThis.Uint8Array = Original; Original.prototype.subarray = subarray; }
    });
    await test('Response formData uses forgiving UTF8 regardless of charset labels', async () => {
        const form = await new Response('q=%C2x&valid=%C3%A9', { headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=shift_jis' } }).formData();
        return form instanceof FormData && form.get('q') === '\ufffdx' && form.get('valid') === '\u00e9';
    });
    await test('Request formData uses the same byte parser for nonstandard HTTP methods', async () => {
        const form = await new Request('about:blank', { method: 'LADIDA', body: 'q=%C2x',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=windows-1252' } }).formData();
        return form.get('q') === '\ufffdx';
    });
    await test('formData retains a leading byte-order mark in both names and values', async () => {
        const form = await new Response('\ufeffq=\ufeffv', { headers: { 'Content-Type': 'application/x-www-form-urlencoded' } }).formData();
        return pairs(form) === JSON.stringify([['\ufeffq', '\ufeffv']]);
    });
    await test('formData decodes percent escapes after splitting bytes into fields', async () => {
        const form = await new Response('a=%26%3D&b=%2B+&nul=%00', { headers: { 'Content-Type': 'application/x-www-form-urlencoded' } }).formData();
        return pairs(form) === JSON.stringify([['a', '&='], ['b', '+ '], ['nul', '\0']]);
    });
    await test('formData preserves duplicate field order and empty names', async () => {
        const form = await new Response('q=1&q=2&=empty&tail', { headers: { 'Content-Type': 'application/x-www-form-urlencoded' } }).formData();
        return pairs(form) === JSON.stringify([['q', '1'], ['q', '2'], ['', 'empty'], ['tail', '']]);
    });
    await test('raw body bytes use the same replacement and BOM rules', async () => {
        const form = await new Response(new Uint8Array([239, 187, 191, 113, 61, 194, 120]),
            { headers: { 'Content-Type': 'application/x-www-form-urlencoded' } }).formData();
        return form.get('\ufeffq') === '\ufffdx';
    });
    await test('URLSearchParams body extraction supplies the correct media type', async () => {
        const response = new Response(new URLSearchParams([['q', 'two words']]));
        return response.headers.get('content-type') === 'application/x-www-form-urlencoded;charset=UTF-8' &&
            (await response.formData()).get('q') === 'two words';
    });
    await test('formData consumes a non-null body exactly once', async () => {
        const response = new Response('q=v', { headers: { 'Content-Type': 'application/x-www-form-urlencoded' } });
        await response.formData(); return response.bodyUsed && await rejectsTypeError(() => response.formData());
    });
    await test('formData rejects an unrelated media type after consuming its body', async () => {
        const response = new Response('q=v', { headers: { 'Content-Type': 'text/plain' } });
        return await rejectsTypeError(() => response.formData()) && response.bodyUsed;
    });
    if (realm === 'worker') {
        await test('worker FormData preserves ordered live pairs and USVString data', () => {
            const form = new FormData(); form.append('a', '1'); form.append('\ud800', '\udc00'); form.append('a', '2');
            const iterator = form.entries(); iterator.next(); form.delete('a');
            return form.get('\ufffd') === '\ufffd' && form.getAll('a').length === 0 && iterator.next().done;
        });
        await test('worker FormData wraps Blob values in real File entries', async () => {
            const form = new FormData(); form.append('file', new Blob(['abc'], { type: 'text/plain' }));
            const file = form.get('file');
            return file instanceof File && file.name === 'blob' && file.type === 'text/plain' && await file.text() === 'abc';
        });
        await test('worker FormData requires Blob values when a filename is supplied', () =>
            throwsTypeError(() => new FormData().append('file', 'text', 'file.txt')));
        await test('worker Request validates methods and body restrictions', () =>
            throwsTypeError(() => new Request()) && throwsTypeError(() => new Request('about:blank', { method: 'TRACE' })) &&
            throwsTypeError(() => new Request('about:blank', { body: 'body' })) &&
            new Request('about:blank', { method: 'custom' }).method === 'custom');
        await test('worker Request reads each supplied dictionary getter once', async () => {
            const reads = {}, init = {};
            for (const [key, value] of Object.entries({ method: 'POST', body: 'abc', headers: { 'X-Example': 'value' }, credentials: 'omit' })) {
                Object.defineProperty(init, key, { get() {
                    reads[key] = (reads[key] || 0) + 1;
                    if (reads[key] > 1) throw new Error('Dictionary member read twice');
                    return value;
                } });
            }
            const request = new Request('about:blank', init);
            return request.method === 'POST' && request.credentials === 'omit' && await request.text() === 'abc' &&
                ['method', 'body', 'headers', 'credentials'].every(key => reads[key] === 1);
        });
        await test('worker Response status conversion rejects BigInt and observes each getter once', () => {
            let statusReads = 0, textReads = 0;
            const response = new Response(null, {
                get status() { statusReads++; return 201; },
                get statusText() { textReads++; return 'Created'; },
            });
            return response.status === 201 && response.statusText === 'Created' && statusReads === 1 && textReads === 1 &&
                throwsTypeError(() => new Response(null, { status: 200n })) &&
                throwsTypeError(() => Response.redirect('https://example.test/', 302n));
        });
        await test('worker response body snapshots the selected view bytes', async () => {
            const backing = new Uint8Array([9, 65, 66, 9]), response = new Response(backing.subarray(1, 3));
            backing[1] = 90; return await response.text() === 'AB';
        });
        await test('worker clone bodies are independently consumable', async () => {
            const response = new Response('abc'), copy = response.clone();
            const first = await response.bytes(); first[0] = 88;
            const second = await copy.text();
            return first[0] === 88 && second === 'abc' && response.bodyUsed && copy.bodyUsed &&
                throwsTypeError(() => response.clone()) && await rejectsTypeError(() => response.text());
        });
        await test('worker Request cloning does not consume the original body', async () => {
            const request = new Request('about:blank', { method: 'POST', body: 'abc' }), copy = request.clone();
            return !request.bodyUsed && await request.text() === 'abc' && await copy.text() === 'abc';
        });
        await test('worker replacing Request ownership disturbs only a transferred input body', async () => {
            const source = new Request('about:blank', { method: 'POST', body: 'abc' }), copy = new Request(source);
            return source.bodyUsed && await rejectsTypeError(() => source.text()) && await copy.text() === 'abc';
        });
        await test('worker null body can be consumed repeatedly without becoming used', async () => {
            const response = new Response();
            return await response.text() === '' && await response.text() === '' && !response.bodyUsed;
        });
        await test('worker body methods reject forged receivers through a rejected promise', async () =>
            await rejectsTypeError(() => Response.prototype.text.call({})) &&
            throwsTypeError(() => Object.getOwnPropertyDescriptor(Response.prototype, 'bodyUsed').get.call({})));
        await test('worker multipart body extraction includes field and file bytes', async () => {
            const form = new FormData(); form.append('field', 'one\ntwo'); form.append('file', new Blob(['file bytes']), 'a.txt');
            const response = new Response(form), type = response.headers.get('content-type'), body = await response.text();
            return type.startsWith('multipart/form-data; boundary=') && body.includes('name="field"\r\n\r\none\r\ntwo\r\n') &&
                body.includes('filename="a.txt"\r\nContent-Type: application/octet-stream\r\n\r\nfile bytes\r\n');
        });
    }
    return rows;
};
