/* Real parsed-child realm checks. Native ownership is retained after removal;
 * this fixture does not claim collection of reachable realm/native owners. */
async function runFileReaderRealmRegression(child) {
    'use strict';
    const rows = [], ParentReader = FileReader, ChildReader = child.FileReader;
    const assert = (condition, message = 'realm assertion failed') => { if (!condition) throw new Error(message); };
    const equal = (actual, expected) => assert(Object.is(actual, expected), String(actual) + ' !== ' + String(expected));
    const check = async (name, body) => {
        try { await body(); rows.push({name, status: 'PASS'}); }
        catch (error) { rows.push({name, status: 'FAIL', message: String(error && error.stack || error)}); }
    };
    const thrownBy = (fn, Constructor, name) => {
        let caught = false, exception;
        try { fn(); } catch (error) { caught = true; exception = error; }
        assert(caught && exception instanceof Constructor, 'exception used the wrong realm');
        if (name) equal(exception.name, name);
    };
    function completed(reader, method, input) {
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => reject(new Error('cross-realm reader deadline')), 1000);
            reader.addEventListener('loadend', () => { clearTimeout(timer); resolve(reader.result); }, {once: true});
            try { Reflect.apply(method, reader, [input]); }
            catch (error) { clearTimeout(timer); reject(error); }
        });
    }
    await check('parent readonly getter recognizes a real child reader', () => {
        const get = Object.getOwnPropertyDescriptor(ParentReader.prototype, 'readyState').get;
        equal(get.call(new ChildReader()), 0);
    });
    await check('child readonly getter recognizes a real parent reader', () => {
        const get = Object.getOwnPropertyDescriptor(ChildReader.prototype, 'readyState').get;
        equal(get.call(new ParentReader()), 0);
    });
    await check('cross-realm prototype impostors use the borrowing getter TypeError', () => {
        const get = Object.getOwnPropertyDescriptor(ParentReader.prototype, 'result').get;
        thrownBy(() => get.call(child.Object.create(ChildReader.prototype)), TypeError);
        const childGet = Object.getOwnPropertyDescriptor(ChildReader.prototype, 'result').get;
        thrownBy(() => childGet.call(Object.create(ParentReader.prototype)), child.TypeError);
    });
    await check('borrowed parent read preserves the child result buffer realm', async () => {
        const reader = new ChildReader();
        const result = await completed(reader, ParentReader.prototype.readAsArrayBuffer, new Blob(['child']));
        assert(result instanceof child.ArrayBuffer && !(result instanceof ArrayBuffer));
        equal(new child.Uint8Array(result).length, 5);
    });
    await check('borrowed child read preserves the parent result buffer realm', async () => {
        const reader = new ParentReader();
        const result = await completed(reader, ChildReader.prototype.readAsArrayBuffer, new child.Blob(['parent']));
        assert(result instanceof ArrayBuffer && !(result instanceof child.ArrayBuffer));
        equal(new Uint8Array(result).length, 6);
    });
    await check('a parent reader accepts genuine child Blob bytes', async () => {
        const reader = new ParentReader();
        equal(await completed(reader, ParentReader.prototype.readAsText, new child.Blob(['\u03a3\0child'])), '\u03a3\0child');
    });
    await check('borrowed parent busy-read exception has the parent DOMException realm', () => {
        const reader = new ChildReader(), input = new child.Blob(['a']); reader.readAsText(input);
        try { thrownBy(() => ParentReader.prototype.readAsText.call(reader, input), DOMException, 'InvalidStateError'); }
        finally { reader.abort(); }
    });
    await check('borrowed child busy-read exception has the child DOMException realm', () => {
        const reader = new ParentReader(), input = new Blob(['a']); reader.readAsText(input);
        try { thrownBy(() => ChildReader.prototype.readAsText.call(reader, input), child.DOMException, 'InvalidStateError'); }
        finally { reader.abort(); }
    });
    await check('borrowed method conversion exceptions use the method realm', () => {
        thrownBy(() => ChildReader.prototype.readAsText.call(new ParentReader(), new Blob(), Symbol()), child.TypeError);
        thrownBy(() => ParentReader.prototype.readAsText.call(new ChildReader(), new child.Blob(), Symbol()), TypeError);
    });
    await check('cross-realm handler setters retain one listener and its receiver', async () => {
        const reader = new ChildReader(), setter = Object.getOwnPropertyDescriptor(ParentReader.prototype, 'onload').set;
        let count = 0, receiver;
        setter.call(reader, function () { ++count; receiver = this; });
        await completed(reader, ParentReader.prototype.readAsText, new Blob(['handler']));
        equal(count, 1); equal(receiver, reader);
    });
    await check('FileReader construction uses one observable newTarget prototype get', () => {
        let reads = 0;
        const target = new Proxy(function Target() {}, {get(object, key, receiver) {
            if (key === 'prototype') { ++reads; return 3; }
            return Reflect.get(object, key, receiver);
        }});
        const reader = Reflect.construct(ChildReader, [], target);
        equal(reads, 1); equal(Object.getPrototypeOf(reader), ParentReader.prototype);
        equal(Object.getOwnPropertyDescriptor(ParentReader.prototype, 'readyState').get.call(reader), 0);
    });
    await check('borrowed read dispatches events allocated by the receiver owner', async () => {
        const reader = new ChildReader(); let event;
        reader.onload = value => { event = value; };
        await completed(reader, ParentReader.prototype.readAsText, new Blob(['event']));
        assert(event instanceof child.ProgressEvent && !(event instanceof ProgressEvent));
    });
    return rows;
}

async function runFileReaderDetachedRegression(saved) {
    'use strict';
    const rows = [];
    const check = async (name, body) => {
        try { await body(); rows.push({name, status: 'PASS'}); }
        catch (error) { rows.push({name, status: 'FAIL', message: String(error && error.stack || error)}); }
    };
    const assert = (condition, message = 'detached reader assertion failed') => { if (!condition) throw new Error(message); };
    await check('retained child reader getter remains usable after frame detachment', () => {
        assert(saved.getReady.call(saved.reader) === 1);
    });
    await check('retained child reader abort releases its canceled read bytes safely', () => {
        saved.abort.call(saved.reader);
        assert(saved.getReady.call(saved.reader) === 2 && saved.getResult.call(saved.reader) === null);
    });
    await check('retained child method can read into an active parent reader', async () => {
        const reader = new FileReader();
        await new Promise((resolve, reject) => {
            const timer = setTimeout(() => reject(new Error('retained method read deadline')), 1000);
            reader.onloadend = () => { clearTimeout(timer); resolve(); };
            try { saved.readBuffer.call(reader, new Blob(['safe'])); }
            catch (error) { clearTimeout(timer); reject(error); }
        });
        assert(reader.result instanceof ArrayBuffer && new Uint8Array(reader.result).length === 4);
    });
    await check('ordinary retained native-bound DOM remains usable with the reader realm', () => {
        const element = saved.createElement.call(saved.document, 'span');
        element.setAttribute('data-reader-retained', 'yes');
        assert(element.getAttribute('data-reader-retained') === 'yes');
    });
    return rows;
}
