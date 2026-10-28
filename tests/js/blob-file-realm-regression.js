// Uses a real parsed same-origin iframe, never a fabricated realm object.
async function runBlobFileRealmRegression(child) {
    const rows = [];
    const assert = (value, message = 'assertion failed') => { if (!value) throw new Error(message); };
    const equal = (actual, expected) => assert(Object.is(actual, expected), String(actual) + ' !== ' + String(expected));
    const test = async (name, body) => {
        try { await body(); rows.push({name: 'realm: ' + name, status: 'PASS'}); }
        catch (error) { rows.push({name: 'realm: ' + name, status: 'FAIL', message: String(error && error.stack || error)}); }
    };
    await test('parsed iframe owns separate Blob File and Promise interfaces', () => {
        assert(child && child !== window && child.Blob !== Blob && child.File !== File && child.Promise !== Promise);
    });
    await test('parent arrayBuffer method preserves child receiver allocation realm', async () => {
        const promise = Blob.prototype.arrayBuffer.call(new child.Blob(['child']));
        assert(promise instanceof child.Promise && !(promise instanceof Promise));
        const bytes = await promise; assert(bytes instanceof child.ArrayBuffer && !(bytes instanceof ArrayBuffer));
        equal(new child.Uint8Array(bytes)[0], 99);
    });
    await test('child arrayBuffer method preserves parent receiver allocation realm', async () => {
        const promise = child.Blob.prototype.arrayBuffer.call(new Blob(['parent']));
        assert(promise instanceof Promise && !(promise instanceof child.Promise));
        const bytes = await promise; assert(bytes instanceof ArrayBuffer && !(bytes instanceof child.ArrayBuffer));
        equal(new Uint8Array(bytes)[0], 112);
    });
    await test('parent bytes method returns child Promise and typed-array result', async () => {
        const promise = Blob.prototype.bytes.call(new child.Blob(['child']));
        assert(promise instanceof child.Promise);
        const bytes = await promise; assert(bytes instanceof child.Uint8Array && !(bytes instanceof Uint8Array)); equal(bytes[0], 99);
    });
    await test('child bytes method returns parent Promise and typed-array result', async () => {
        const promise = child.Blob.prototype.bytes.call(new Blob(['parent']));
        assert(promise instanceof Promise);
        const bytes = await promise; assert(bytes instanceof Uint8Array && !(bytes instanceof child.Uint8Array)); equal(bytes[0], 112);
    });
    await test('parent text method returns a child receiver Promise', async () => {
        const promise = Blob.prototype.text.call(new child.Blob(['child']));
        assert(promise instanceof child.Promise && !(promise instanceof Promise)); equal(await promise, 'child');
    });
    await test('child text method returns a parent receiver Promise', async () => {
        const promise = child.Blob.prototype.text.call(new Blob(['parent']));
        assert(promise instanceof Promise && !(promise instanceof child.Promise)); equal(await promise, 'parent');
    });
    await test('invalid parent operation receiver rejects in the parent method realm', async () => {
        let promise;
        try { promise = Blob.prototype.arrayBuffer.call(new child.Object()); }
        catch (_) { throw new Error('operation must return a rejected Promise'); }
        assert(promise instanceof Promise && !(promise instanceof child.Promise));
        let error; try { await promise; } catch (caught) { error = caught; }
        assert(error instanceof TypeError && !(error instanceof child.TypeError));
    });
    await test('invalid child operation receiver rejects in the child method realm', async () => {
        let promise;
        try { promise = child.Blob.prototype.arrayBuffer.call({}); }
        catch (_) { throw new Error('operation must return a rejected Promise'); }
        assert(promise instanceof child.Promise && !(promise instanceof Promise));
        let error; try { await promise; } catch (caught) { error = caught; }
        assert(error instanceof child.TypeError && !(error instanceof TypeError));
    });
    await test('parent slice method creates a Blob in the child receiver realm', async () => {
        const result = Blob.prototype.slice.call(new child.File(['abcd'], 'n'), 1, 3);
        equal(Object.getPrototypeOf(result), child.Blob.prototype); assert(!(result instanceof child.File)); equal(await result.text(), 'bc');
    });
    await test('child slice method creates a Blob in the parent receiver realm', async () => {
        const result = child.Blob.prototype.slice.call(new File(['abcd'], 'n'), 1, 3);
        equal(Object.getPrototypeOf(result), Blob.prototype); assert(!(result instanceof File)); equal(await result.text(), 'bc');
    });
    await test('parent File getters accept genuine child File private records', () => {
        const file = new child.File(['abc'], 'name', {type: 'APPLICATION/TEST', lastModified: 0});
        equal(Object.getOwnPropertyDescriptor(File.prototype, 'name').get.call(file), 'name');
        equal(Object.getOwnPropertyDescriptor(File.prototype, 'lastModified').get.call(file), 0);
        equal(Object.getOwnPropertyDescriptor(Blob.prototype, 'size').get.call(file), 3);
        equal(Object.getOwnPropertyDescriptor(Blob.prototype, 'type').get.call(file), 'application/test');
    });
    await test('parent Blob constructor copies a child Blob without public branding', async () => {
        const blob = new child.Blob(['child']); Object.setPrototypeOf(blob, null);
        Object.defineProperty(blob, Symbol.toStringTag, {value: 'Unrelated'});
        equal(await new Blob([blob]).text(), 'child');
    });
    await test('child Blob constructor copies parent BufferSource byte ranges', async () => {
        const bytes = new Uint8Array([65, 66, 67]);
        equal(await new child.Blob([bytes.subarray(1), new DataView(bytes.buffer, 0, 1)]).text(), 'BCA');
    });
    await test('parent Blob constructor copies child BufferSource byte ranges', async () => {
        const bytes = new child.Uint8Array([65, 66, 67]);
        equal(await new Blob([bytes.subarray(1), new child.DataView(bytes.buffer, 0, 1)]).text(), 'BCA');
    });
    await test('primitive newTarget prototype uses its child realm Blob and File fallbacks', () => {
        const Target = child.Function(''); Target.prototype = null;
        equal(Object.getPrototypeOf(Reflect.construct(Blob, [['x']], Target)), child.Blob.prototype);
        equal(Object.getPrototypeOf(Reflect.construct(File, [[], 'name'], Target)), child.File.prototype);
    });
    await test('Proxy newTarget fallback reads prototype once without prototype traps', () => {
        const Target = child.Function(''); Target.prototype = 0;
        let gets = 0, traps = 0;
        const Wrapped = new Proxy(Target, {get(target, key, receiver) {
            if (key === 'prototype') ++gets; return Reflect.get(target, key, receiver);
        }, getPrototypeOf() { ++traps; throw new Error('unexpected prototype trap'); }});
        equal(Object.getPrototypeOf(Reflect.construct(Blob, [[], {}], Wrapped)), child.Blob.prototype);
        equal(gets, 1); equal(traps, 0);
    });
    await test('receiver allocation remains stable after public prototype replacement', async () => {
        const blob = new child.Blob(['private']); Object.setPrototypeOf(blob, Blob.prototype);
        const promise = Blob.prototype.bytes.call(blob); assert(promise instanceof child.Promise);
        const bytes = await promise; assert(bytes instanceof child.Uint8Array); equal(bytes[0], 112);
    });
    await test('parent stream method preserves child stream ownership and immutable bytes', async () => {
        const blob = new child.Blob(['child']), stream = Blob.prototype.stream.call(blob);
        assert(stream instanceof child.ReadableStream && !(stream instanceof ReadableStream));
        const reader = stream.getReader(), first = await reader.read();
        assert(first.value instanceof child.Uint8Array); first.value[0] = 90;
        equal(await Blob.prototype.text.call(blob), 'child'); assert((await reader.read()).done); reader.releaseLock();
    });
    await test('child stream method preserves parent stream ownership and immutable bytes', async () => {
        const blob = new Blob(['parent']), stream = child.Blob.prototype.stream.call(blob);
        assert(stream instanceof ReadableStream && !(stream instanceof child.ReadableStream));
        const reader = stream.getReader(), first = await reader.read();
        assert(first.value instanceof Uint8Array); first.value[0] = 90;
        equal(await blob.text(), 'parent'); assert((await reader.read()).done); reader.releaseLock();
    });
    return rows;
}

async function runBlobFileWindowRegression(child) {
    const rows = [];
    const assert = (value, message = 'assertion failed') => { if (!value) throw new Error(message); };
    const equal = (actual, expected) => assert(Object.is(actual, expected), 'Expected identical values');
    const test = async (name, body) => {
        try { await body(); rows.push({name: 'window consumer: ' + name, status: 'PASS'}); }
        catch (error) { rows.push({name: 'window consumer: ' + name, status: 'FAIL', message: String(error && error.stack || error)}); }
    };
    await test('Blob stream returns independent bytes through the existing reader', async () => {
        const blob = new Blob(['immutable']), reader = blob.stream().getReader();
        const first = await reader.read(); equal(first.done, false); first.value.fill(90);
        equal(await blob.text(), 'immutable'); equal((await reader.read()).done, true); reader.releaseLock();
    });
    await test('Blob URL fetch reads private bytes and MIME without public access', async () => {
        const blob = new Blob(['url payload'], {type: 'application/test'});
        Object.defineProperties(blob, {size: {get() { throw new Error('public size'); }},
            type: {get() { throw new Error('public type'); }}, arrayBuffer: {get() { throw new Error('public read'); }}});
        Object.setPrototypeOf(blob, null); const url = URL.createObjectURL(blob);
        try { const response = await fetch(url); equal(response.status, 200);
            equal(response.headers.get('content-type'), 'application/test'); equal(await response.text(), 'url payload'); }
        finally { URL.revokeObjectURL(url); }
    });
    await test('revoked Blob URL fetch rejects after private resource retirement', async () => {
        const url = URL.createObjectURL(new Blob(['gone'], {type: 'application/octet-stream'})); URL.revokeObjectURL(url);
        let error; try { await fetch(url); } catch (caught) { error = caught; } assert(error instanceof TypeError);
    });
    await test('DataTransfer accepts a child File through its genuine private brand', () => {
        const file = new child.File(['payload'], 'cross.txt', {type: 'APPLICATION/TEST'});
        Object.defineProperty(file, 'type', {get() { throw new Error('public type'); }});
        Object.setPrototypeOf(file, null);
        const data = new DataTransfer(), item = data.items.add(file);
        equal(item.kind, 'file'); equal(item.type, 'application/test'); equal(item.getAsFile(), file);
        equal(data.files.length, 1); equal(data.files[0], file);
    });
    await test('private Blob bytes remain readable by the existing FileReader bridge', async () => {
        const blob = new Blob(['reader'], {type: 'text/plain'});
        Object.defineProperties(blob, {size: {value: 0}, type: {get() { throw new Error('public type'); }}});
        Object.setPrototypeOf(blob, null);
        const reader = new FileReader();
        const result = await new Promise((resolve, reject) => { reader.onload = () => resolve(reader.result);
            reader.onerror = () => reject(reader.error); reader.readAsText(blob); });
        equal(result, 'reader');
    });
    await test('actual worker loads script from a privately branded Blob URL', async () => {
        const source = new Blob(["onmessage = event => postMessage({value:event.data.value + 1});"], {type: 'text/javascript'});
        Object.defineProperties(source, {type: {get() { throw new Error('public type'); }},
            size: {get() { throw new Error('public size'); }}}); Object.setPrototypeOf(source, null);
        const url = URL.createObjectURL(source); let worker, timer;
        try {
            const result = await new Promise((resolve, reject) => {
                worker = new Worker(url); worker.onmessage = event => resolve(event.data);
                worker.onerror = event => reject(new Error(event.message || 'worker error'));
                worker.onmessageerror = () => reject(new Error('worker messageerror'));
                timer = setTimeout(() => reject(new Error('private Blob worker deadline')), 3000);
                worker.postMessage({value: 41});
            }); equal(result.value, 42);
        } finally { if (timer) clearTimeout(timer); if (worker) worker.terminate(); URL.revokeObjectURL(url); }
    });
    return rows;
}
