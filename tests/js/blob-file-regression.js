// Custom public-API regressions. These rows are not upstream WPT assertions.
// The same source runs in an actual window and an actual dedicated worker.
async function runBlobFileRegression(realm) {
    const rows = [];
    const assert = (value, message = 'assertion failed') => { if (!value) throw new Error(message); };
    const equal = (actual, expected) => assert(Object.is(actual, expected), String(actual) + ' !== ' + String(expected));
    const throwsType = callback => {
        let error;
        try { callback(); } catch (caught) { error = caught; }
        assert(error instanceof TypeError, 'expected a TypeError');
    };
    const rejectsType = async callback => {
        let promise;
        try { promise = callback(); } catch (_) { throw new Error('operation threw instead of returning a Promise'); }
        assert(promise instanceof Promise, 'operation did not return a realm Promise');
        let error;
        try { await promise; } catch (caught) { error = caught; }
        assert(error instanceof TypeError, 'expected a rejected TypeError');
    };
    const test = async (name, body) => {
        try { await body(); rows.push({name: realm + ': ' + name, status: 'PASS'}); }
        catch (error) { rows.push({name: realm + ': ' + name, status: 'FAIL', message: String(error && error.stack || error)}); }
    };
    const text = value => Blob.prototype.text.call(value);
    const size = Object.getOwnPropertyDescriptor(Blob.prototype, 'size').get;
    const typeDescriptor = Object.getOwnPropertyDescriptor(Blob.prototype, 'type');
    const type = value => { assert(typeDescriptor && typeof typeDescriptor.get === 'function'); return typeDescriptor.get.call(value); };

    await test('Blob defaults and explicit undefined produce empty bytes', async () => {
        for (const value of [new Blob(), new Blob(undefined), new Blob(undefined, null)]) {
            equal(value.size, 0); equal(value.type, ''); equal(await value.text(), '');
            assert(value instanceof Blob);
        }
    });
    await test('Blob accepts a Set sequence in insertion order', async () => {
        equal(await new Blob(new Set(['first', '/', 'second'])).text(), 'first/second');
    });
    await test('Blob accepts a generator without array methods', async () => {
        function* values() { yield 'A'; yield new Uint8Array([66]); yield new Blob(['C']); }
        equal(await new Blob(values()).text(), 'ABC');
    });
    await test('Blob rejects nonobject and noniterable sequence values', () => {
        for (const value of [null, 2, true, 'abc', {}, {length: 0}]) throwsType(() => new Blob(value));
    });
    await test('Blob obtains its sequence iterator once', async () => {
        let gets = 0, calls = 0;
        const input = {get [Symbol.iterator]() { ++gets; return function* () { ++calls; yield 'ok'; }; }};
        equal(await new Blob(input).text(), 'ok'); equal(gets, 1); equal(calls, 1);
    });
    await test('Blob closes an iterator when a Symbol part conversion throws', () => {
        let closed = 0;
        const input = {[Symbol.iterator]() { return {next() { return {value: Symbol('bad'), done: false}; },
            return() { ++closed; return {done: true}; }}; }};
        throwsType(() => new Blob(input)); equal(closed, 1);
    });
    await test('Blob part conversion failure occurs before dictionary getters', () => {
        let touched = 0;
        throwsType(() => new Blob([Symbol('bad')], {get endings() { ++touched; return 'transparent'; }}));
        equal(touched, 0);
    });
    await test('Blob converts all parts before ordered dictionary members', async () => {
        const log = [];
        const blob = new Blob([{toString() { log.push('part'); return 'data'; }}], {
            get type() { log.push('type'); return 'TEXT/PLAIN'; },
            get endings() { log.push('endings'); return 'transparent'; },
        });
        equal(log.join(','), 'part,endings,type'); equal(await blob.text(), 'data'); equal(blob.type, 'text/plain');
    });
    await test('Blob turns unpaired surrogates into replacement UTF-8', async () => {
        equal(await new Blob(['a\ud800b\udc00c\ud83d\ude00']).text(), 'a\ufffdb\ufffdc\ud83d\ude00');
    });
    await test('Blob stringifies primitive parts using USVString conversion', async () => {
        equal(await new Blob([undefined, null, true, 42, 7n]).text(), 'undefinednulltrue427');
    });
    await test('Blob honors typed-array byte offsets and DataView lengths', async () => {
        const bytes = new Uint8Array([65, 66, 67, 68, 69]);
        equal(await new Blob([bytes.subarray(1, 3), new DataView(bytes.buffer, 3, 1)]).text(), 'BCD');
    });
    await test('Blob ignores author shadows of BufferSource accessors', async () => {
        const bytes = new Uint8Array([65, 66, 67]), view = new DataView(bytes.buffer, 1, 1);
        for (const object of [bytes, view]) for (const key of ['buffer', 'byteOffset', 'byteLength'])
            Object.defineProperty(object, key, {get() { throw new Error('shadow ' + key); }});
        equal(await new Blob([bytes, view]).text(), 'ABCB');
    });
    await test('Blob snapshots BufferSource after dictionary conversion', async () => {
        const input = new Uint8Array([65]);
        const blob = new Blob([input], {get type() { input[0] = 66; return ''; }});
        input[0] = 67; equal(await blob.text(), 'B');
    });
    await test('Blob snapshots every source independently of later writes', async () => {
        const source = new Uint8Array([65, 66, 67]);
        const blob = new Blob([source.buffer, source.subarray(1)]);
        source.fill(90); equal(await blob.text(), 'ABCBC');
    });
    await test('native ArrayBuffer transfer really detaches the source backing store', () => {
        assert(typeof ArrayBuffer.prototype.transfer === 'function', 'native transfer is required by this regression fixture');
        const buffer = new Uint8Array([65, 66]).buffer, transferred = ArrayBuffer.prototype.transfer.call(buffer);
        equal(buffer.byteLength, 0); equal(transferred.byteLength, 2); equal(new Uint8Array(transferred)[0], 65);
    });
    await test('Blob treats an actually detached ArrayBuffer as empty', async () => {
        const buffer = new Uint8Array([65, 66]).buffer; ArrayBuffer.prototype.transfer.call(buffer);
        equal(buffer.byteLength, 0); const blob = new Blob([buffer, 'tail']);
        equal(blob.size, 4); equal(await blob.text(), 'tail');
    });
    await test('Blob treats an actually detached offset typed-array view as empty', async () => {
        const buffer = new Uint8Array([65, 66, 67, 68]).buffer, view = new Uint8Array(buffer, 1, 2);
        ArrayBuffer.prototype.transfer.call(buffer); equal(buffer.byteLength, 0);
        const blob = new Blob([view, 'tail']); equal(blob.size, 4); equal(await blob.text(), 'tail');
    });
    await test('Blob treats an actually detached offset DataView as empty', async () => {
        const buffer = new Uint8Array([65, 66, 67, 68]).buffer, view = new DataView(buffer, 1, 2);
        ArrayBuffer.prototype.transfer.call(buffer); equal(buffer.byteLength, 0);
        const blob = new Blob([view, 'tail']); equal(blob.size, 4); equal(await blob.text(), 'tail');
    });
    await test('Blob handles detachment during dictionary conversion for every BufferSource kind', async () => {
        const buffer = new Uint8Array([65, 66, 67, 68]).buffer;
        const blob = new Blob([buffer, new Uint8Array(buffer, 1, 2), new DataView(buffer, 2, 1), 'tail'], {
            get type() { ArrayBuffer.prototype.transfer.call(buffer); return 'TEXT/PLAIN'; },
        });
        equal(buffer.byteLength, 0); equal(blob.type, 'text/plain'); equal(blob.size, 4); equal(await blob.text(), 'tail');
    });
    await test('Blob and File handle detachment during newTarget prototype allocation', async () => {
        for (const file of [false, true]) {
            const buffer = new Uint8Array([65, 66, 67, 68]).buffer;
            const Target = new Proxy(function () {}, {get(target, key, receiver) {
                if (key === 'prototype') { ArrayBuffer.prototype.transfer.call(buffer); return file ? File.prototype : Blob.prototype; }
                return Reflect.get(target, key, receiver);
            }});
            const parts = [buffer, new Uint8Array(buffer, 1, 2), new DataView(buffer, 2, 1), 'tail'];
            const blob = Reflect.construct(file ? File : Blob, file ? [parts, 'name'] : [parts], Target);
            equal(buffer.byteLength, 0); equal(blob.size, 4); equal(await blob.text(), 'tail');
        }
    });
    await test('native resizable ArrayBuffer exposes an actual resizable backing store', () => {
        const descriptor = Object.getOwnPropertyDescriptor(ArrayBuffer.prototype, 'resizable');
        assert(descriptor && typeof descriptor.get === 'function', 'native resizable buffers are required by this regression fixture');
        const buffer = new ArrayBuffer(4, {maxByteLength: 8}); equal(descriptor.get.call(buffer), true);
        buffer.resize(6); equal(buffer.byteLength, 6);
    });
    await test('Blob and File reject resizable ArrayBuffers and their fixed or tracking views', () => {
        const descriptor = Object.getOwnPropertyDescriptor(ArrayBuffer.prototype, 'resizable');
        assert(descriptor && typeof descriptor.get === 'function', 'missing native resizable-buffer accessor');
        const buffer = new ArrayBuffer(4, {maxByteLength: 8}); equal(descriptor.get.call(buffer), true);
        for (const part of [buffer, new Uint8Array(buffer), new Uint8Array(buffer, 1, 2),
                            new DataView(buffer), new DataView(buffer, 1, 2)]) {
            throwsType(() => new Blob([part])); throwsType(() => new File([part], 'name'));
        }
    });
    await test('Blob native endings normalize string CR and CRLF on Apple', async () => {
        equal(await new Blob(['a\rb\r\nc\nd'], {endings: 'native'}).text(), 'a\nb\nc\nd');
    });
    await test('Blob native endings preserve byte and Blob parts', async () => {
        equal(await new Blob([new Uint8Array([65, 13, 10]), new Blob(['B\r']), 'C\r'], {endings: 'native'}).text(), 'A\r\nB\rC\n');
    });
    await test('Blob default endings preserve original string newlines', async () => {
        equal(await new Blob(['a\rb\r\nc']).text(), 'a\rb\r\nc');
    });
    await test('Blob rejects invalid line-ending enum values', () => {
        for (const endings of ['', 'Native', 'other', null, Symbol('bad')]) throwsType(() => new Blob([], {endings}));
    });
    await test('Blob rejects primitive dictionary values', () => {
        for (const value of [true, 1, 'type', Symbol('bad'), 1n]) throwsType(() => new Blob([], value));
    });
    await test('Blob normalizes printable ASCII MIME values only', () => {
        equal(new Blob([], {type: ' TEXT/PLAIN;X=YES '}).type, ' text/plain;x=yes ');
        for (const value of ['a\u0000b', 'a\u001fb', 'a\u007fb', 'caf\u00e9']) equal(new Blob([], {type: value}).type, '');
        equal(new Blob([], {type: null}).type, 'null');
    });
    await test('Blob rejects a Symbol MIME value', () => { throwsType(() => new Blob([], {type: Symbol('bad')})); });
    await test('Blob data and MIME live outside author-visible own properties', () => {
        const blob = new Blob(['abc'], {type: 'text/plain'});
        equal(Object.getOwnPropertyNames(blob).length, 0); equal(Object.getOwnPropertySymbols(blob).length, 0);
        assert(!Reflect.set(blob, 'size', 99)); assert(!Reflect.set(blob, 'type', 'forged'));
        equal(blob.size, 3); equal(blob.type, 'text/plain');
    });
    await test('Blob public legacy buffer properties cannot mutate its data', async () => {
        const blob = new Blob(['safe']);
        if (blob._buf instanceof ArrayBuffer) new Uint8Array(blob._buf).fill(90);
        if (ArrayBuffer.isView(blob._b)) blob._b.fill(90);
        try { Object.defineProperty(blob, '_buf', {value: new Uint8Array([88]).buffer, configurable: true}); } catch (_) {}
        try { Object.defineProperty(blob, '_b', {value: new Uint8Array([89]), configurable: true}); } catch (_) {}
        equal(await text(blob), 'safe'); equal(size.call(blob), 4);
    });
    await test('Blob nested construction uses private bytes despite poisoned public properties', async () => {
        const original = new Blob(['nested'], {type: 'application/test'});
        Object.defineProperties(original, {
            size: {get() { throw new Error('public size'); }},
            type: {get() { throw new Error('public type'); }},
            arrayBuffer: {get() { throw new Error('public arrayBuffer'); }},
            [Symbol.toStringTag]: {value: 'Pretend'},
        });
        Object.setPrototypeOf(original, null);
        equal(await new Blob([original]).text(), 'nested'); equal(type(original), 'application/test');
    });
    await test('Blob arrayBuffer results are independent snapshots', async () => {
        const blob = new Blob(['abc']), first = await blob.arrayBuffer(), second = await blob.arrayBuffer();
        assert(first instanceof ArrayBuffer && second instanceof ArrayBuffer && first !== second);
        new Uint8Array(first)[0] = 90; equal(new Uint8Array(second)[0], 97); equal(await blob.text(), 'abc');
    });
    await test('Blob bytes results are independent Uint8Array snapshots', async () => {
        const blob = new Blob(['abc']), first = await blob.bytes(), second = await blob.bytes();
        assert(first instanceof Uint8Array && first !== second && first.buffer !== second.buffer);
        first[0] = 90; equal(second[0], 97); equal(await blob.text(), 'abc');
    });
    await test('Blob text strips exactly one UTF-8 BOM', async () => {
        equal(await new Blob([new Uint8Array([239, 187, 191, 239, 187, 191, 65])]).text(), '\ufeffA');
    });
    await test('Blob text replaces invalid UTF-8 independently of its MIME', async () => {
        equal(await new Blob([new Uint8Array([0xff, 65])], {type: 'text/plain;charset=iso-8859-1'}).text(), '\ufffdA');
    });
    await test('Blob synchronous getters and slice reject forged receivers', () => {
        for (const value of [{}, Object.create(Blob.prototype), {[Symbol.toStringTag]: 'Blob'}, null]) {
            throwsType(() => size.call(value)); throwsType(() => type(value)); throwsType(() => Blob.prototype.slice.call(value));
        }
    });
    await test('Blob async methods reject forged receivers through Promises', async () => {
        for (const method of ['arrayBuffer', 'bytes', 'text'])
            for (const value of [{}, Object.create(Blob.prototype), null])
                await rejectsType(() => Blob.prototype[method].call(value));
    });
    await test('Blob rejects Proxy receivers without invoking their traps', async () => {
        let traps = 0;
        const proxy = new Proxy(new Blob(['x']), {get() { ++traps; throw new Error('get trap'); },
            getPrototypeOf() { ++traps; throw new Error('prototype trap'); }});
        throwsType(() => size.call(proxy)); throwsType(() => Blob.prototype.slice.call(proxy));
        await rejectsType(() => Blob.prototype.text.call(proxy)); equal(traps, 0);
    });
    await test('Blob slice clamps fractional bounds with ties to even', async () => {
        const blob = new Blob(['0123456789']);
        equal(await blob.slice(1.5, 4.5).text(), '23'); equal(await blob.slice(2.5, 5.5).text(), '2345');
        equal(await blob.slice(-2.5).text(), '89');
    });
    await test('Blob slice handles infinities and NaN without public size reads', async () => {
        const blob = new Blob(['abc']); Object.defineProperty(blob, 'size', {get() { throw new Error('size shadow'); }});
        equal(await Blob.prototype.slice.call(blob, -Infinity, Infinity).text(), 'abc');
        equal(await Blob.prototype.slice.call(blob, NaN, 2).text(), 'ab');
    });
    await test('Blob slice rejects BigInt and Symbol bounds', () => {
        const blob = new Blob(['abc']);
        for (const value of [1n, Symbol('bad')]) {
            throwsType(() => blob.slice(value)); throwsType(() => blob.slice(0, value));
        }
    });
    await test('Blob slice returns a Blob with normalized independent type', () => {
        const file = new File(['abc'], 'name', {type: 'text/plain', lastModified: 0});
        const slice = file.slice(0, 1, 'APPLICATION/TEST');
        assert(slice instanceof Blob && !(slice instanceof File)); equal(slice.type, 'application/test'); equal(slice.size, 1);
        equal(file.slice().type, ''); equal(file.type, 'text/plain');
    });
    await test('Blob interface descriptors expose enumerable IDL members', () => {
        for (const member of ['size', 'type', 'slice', 'arrayBuffer', 'bytes', 'text']) {
            const d = Object.getOwnPropertyDescriptor(Blob.prototype, member);
            assert(d && d.enumerable && d.configurable);
            if (member === 'size' || member === 'type') assert(typeof d.get === 'function' && d.set === undefined);
            else assert(typeof d.value === 'function' && d.writable);
        }
        const tag = Object.getOwnPropertyDescriptor(Blob.prototype, Symbol.toStringTag);
        assert(tag && tag.value === 'Blob' && tag.configurable && !tag.enumerable && !tag.writable);
        equal(Blob.length, 0); equal(Blob.prototype.slice.length, 0);
    });
    await test('Blob and File constructors require new', () => { throwsType(() => Blob()); throwsType(() => File([], 'x')); });
    await test('File requires two arguments before creating an object', () => {
        throwsType(() => new File()); throwsType(() => new File([])); throwsType(() => new File(undefined, 'name'));
    });
    await test('File inherits Blob and exposes File branding', () => {
        const file = new File(['x'], 'name');
        assert(file instanceof File && file instanceof Blob);
        equal(Object.getPrototypeOf(File), Blob); equal(Object.getPrototypeOf(File.prototype), Blob.prototype);
        equal(Object.prototype.toString.call(file), '[object File]'); equal(File.length, 2);
    });
    await test('File name uses USVString and preserves path characters', () => {
        equal(new File([], 'a/\\b\ud800').name, 'a/\\b\ufffd');
        equal(new File([], undefined).name, 'undefined'); equal(new File([], null).name, 'null');
        throwsType(() => new File([], Symbol('bad')));
    });
    await test('File preserves explicit zero and negative lastModified values', () => {
        equal(new File([], 'a', {lastModified: 0}).lastModified, 0);
        equal(new File([], 'a', {lastModified: -17}).lastModified, -17);
        equal(new File([], 'a', {lastModified: -0}).lastModified, 0);
    });
    await test('File converts lastModified through signed 64-bit integer rules', () => {
        equal(new File([], 'a', {lastModified: 12.9}).lastModified, 12);
        equal(new File([], 'a', {lastModified: -12.9}).lastModified, -12);
        equal(new File([], 'a', {lastModified: 18446744073709551616}).lastModified, 0);
        equal(new File([], 'a', {lastModified: 9223372036854775808}).lastModified, -9223372036854775808);
    });
    await test('File maps nonfinite lastModified values to zero', () => {
        for (const lastModified of [NaN, Infinity, -Infinity, null])
            equal(new File([], 'a', {lastModified}).lastModified, 0);
    });
    await test('File rejects BigInt and Symbol lastModified values', () => {
        for (const lastModified of [1n, Symbol('bad')]) throwsType(() => new File([], 'a', {lastModified}));
    });
    await test('File defaults lastModified to construction time', () => {
        const start = Date.now(), file = new File([], 'a'), end = Date.now();
        assert(file.lastModified >= start && file.lastModified <= end);
    });
    await test('File chooses its default timestamp after newTarget prototype allocation', () => {
        let allocationTime;
        const Target = new Proxy(function () {}, {get(target, key, receiver) {
            if (key === 'prototype') {
                // A bounded four-millisecond observable construction step.
                const deadline = Date.now() + 4;
                while (Date.now() < deadline) {}
                allocationTime = Date.now(); return File.prototype;
            }
            return Reflect.get(target, key, receiver);
        }});
        const file = Reflect.construct(File, [[], 'name'], Target);
        assert(file.lastModified >= allocationTime && file.lastModified <= Date.now());
    });
    await test('File conversion order visits bits name inherited dictionary then own member', () => {
        const log = [];
        new File([{toString() { log.push('part'); return 'x'; }}], {toString() { log.push('name'); return 'n'; }}, {
            get lastModified() { log.push('lastModified'); return {valueOf() { log.push('number'); return 0; }}; },
            get type() { log.push('type'); return 'TEXT/PLAIN'; },
            get endings() { log.push('endings'); return 'transparent'; },
        });
        equal(log.join(','), 'part,name,endings,type,lastModified,number');
    });
    await test('File properties are readonly accessors without own public storage', () => {
        const file = new File([], 'name', {lastModified: 0});
        equal(Object.getOwnPropertyNames(file).length, 0);
        for (const member of ['name', 'lastModified']) {
            const d = Object.getOwnPropertyDescriptor(File.prototype, member);
            assert(d && d.enumerable && d.configurable && typeof d.get === 'function' && d.set === undefined);
            assert(!Reflect.set(file, member, 'forged'));
        }
        equal(file.name, 'name'); equal(file.lastModified, 0);
    });
    await test('File getters reject Blob and forged File receivers', () => {
        for (const member of ['name', 'lastModified']) {
            const d = Object.getOwnPropertyDescriptor(File.prototype, member);
            assert(d && typeof d.get === 'function');
            for (const value of [new Blob(), Object.create(File.prototype), {[Symbol.toStringTag]: 'File'}, null])
                throwsType(() => d.get.call(value));
        }
    });
    await test('Blob subclassing preserves custom prototypes and private data', async () => {
        class Derived extends Blob { marker() { return 7; } }
        const blob = new Derived(['derived']);
        assert(blob instanceof Derived && blob instanceof Blob); equal(blob.marker(), 7); equal(await blob.text(), 'derived');
        assert(!(blob.slice() instanceof Derived));
    });
    await test('File subclassing preserves custom prototypes and metadata', async () => {
        class Derived extends File { marker() { return 9; } }
        const file = new Derived(['derived'], 'n', {lastModified: 0});
        assert(file instanceof Derived && file instanceof File && file instanceof Blob);
        equal(file.marker(), 9); equal(file.name, 'n'); equal(file.lastModified, 0); equal(await file.text(), 'derived');
    });
    await test('Blob and File convert arguments before reading newTarget prototype', async () => {
        for (const file of [false, true]) {
            const log = [];
            const Target = new Proxy(function () {}, {get(target, key, receiver) {
                if (key === 'prototype') { log.push('prototype'); return file ? File.prototype : Blob.prototype; }
                return Reflect.get(target, key, receiver);
            }});
            const parts = [{toString() { log.push('part'); return 'x'; }}];
            const options = {get endings() { log.push('endings'); return 'transparent'; }, get type() { log.push('type'); return ''; }};
            if (file) Object.defineProperty(options, 'lastModified', {get() { log.push('lastModified'); return 0; }});
            const args = file ? [parts, {toString() { log.push('name'); return 'n'; }}, options] : [parts, options];
            const result = Reflect.construct(file ? File : Blob, args, Target);
            equal(log.join(','), file ? 'part,name,endings,type,lastModified,prototype' : 'part,endings,type,prototype');
            equal(await text(result), 'x');
        }
    });
    await test('Invalid constructor conversions never inspect newTarget prototype', () => {
        let gets = 0;
        const Target = new Proxy(function () {}, {get(target, key, receiver) {
            if (key === 'prototype') { ++gets; throw new Error('prototype must not run'); }
            return Reflect.get(target, key, receiver);
        }});
        throwsType(() => Reflect.construct(File, [], Target));
        throwsType(() => Reflect.construct(Blob, [[Symbol('bad')]], Target));
        throwsType(() => Reflect.construct(File, [[], Symbol('bad')], Target)); equal(gets, 0);
    });
    await test('Constructor byte snapshot includes mutations by newTarget prototype getter', async () => {
        const bytes = new Uint8Array([65]);
        const Target = new Proxy(function () {}, {get(target, key, receiver) {
            if (key === 'prototype') { bytes[0] = 66; return Blob.prototype; }
            return Reflect.get(target, key, receiver);
        }});
        const result = Reflect.construct(Blob, [[bytes]], Target); bytes[0] = 67;
        equal(await text(result), 'B');
    });
    await test('Blob and File use intrinsic fallback prototypes for primitive newTarget prototypes', () => {
        function Target() {} Target.prototype = null;
        equal(Object.getPrototypeOf(Reflect.construct(Blob, [['x']], Target)), Blob.prototype);
        equal(Object.getPrototypeOf(Reflect.construct(File, [[], 'n'], Target)), File.prototype);
    });
    await test('Response consumes private Blob bytes and MIME despite public shadows', async () => {
        const blob = new Blob(['response'], {type: 'application/test'});
        Object.defineProperties(blob, {type: {get() { throw new Error('type shadow'); }}, size: {value: 0},
            arrayBuffer: {get() { throw new Error('read shadow'); }}});
        Object.setPrototypeOf(blob, null);
        const response = new Response(blob); equal(response.headers.get('content-type'), 'application/test');
        equal(await response.text(), 'response');
    });
    await test('FormData string set remains usable without a filename argument', () => {
        const data = new FormData(); data.append('key', 'old'); data.set('key', 'new'); equal(data.get('key'), 'new');
    });
    await test('FormData explicit file consumes private bytes MIME and name', async () => {
        const file = new File(['payload'], 'source.txt', {type: 'application/test', lastModified: 0});
        Object.defineProperties(file, {type: {get() { throw new Error('type shadow'); }},
            name: {get() { throw new Error('name shadow'); }}, size: {value: 0}});
        Object.setPrototypeOf(file, null);
        const data = new FormData(); data.append('upload', file);
        const response = new Response(data), body = await response.text();
        assert(response.headers.get('content-type').startsWith('multipart/form-data; boundary='));
        assert(body.includes('name="upload"; filename="source.txt"'));
        assert(body.includes('Content-Type: application/test\r\n')); assert(body.includes('\r\npayload\r\n'));
    });
    await test('FormData explicit filename uses private Blob MIME and bytes', async () => {
        const blob = new Blob(['payload'], {type: 'application/test'});
        Object.defineProperty(blob, 'type', {get() { throw new Error('type shadow'); }});
        const data = new FormData(); data.append('upload', blob, 'renamed.txt');
        const body = await new Response(data).text();
        assert(body.includes('filename="renamed.txt"')); assert(body.includes('Content-Type: application/test\r\n'));
        assert(body.includes('\r\npayload\r\n'));
        const file = new File(['metadata'], 'original.txt', {type: 'text/plain', lastModified: 0});
        Object.defineProperties(file, {type: {get() { throw new Error('source type shadow'); }},
            lastModified: {get() { throw new Error('source date shadow'); }}});
        data.append('renamed-file', file, 'new.txt');
        const renamed = data.get('renamed-file');
        equal(renamed.name, 'new.txt'); equal(renamed.lastModified, 0); equal(renamed.type, 'text/plain');
        equal(await renamed.text(), 'metadata');
    });
    await test('Object URLs reject forged Blob-like values before registering them', () => {
        for (const value of [{}, Object.create(Blob.prototype), {_buf: new ArrayBuffer(1), [Symbol.toStringTag]: 'Blob'}])
            throwsType(() => URL.createObjectURL(value));
    });
    await test('Object URLs accept genuine private-branded Blob values', () => {
        const blob = new Blob(['url'], {type: 'application/octet-stream'});
        Object.defineProperties(blob, {size: {get() { throw new Error('size shadow'); }},
            type: {get() { throw new Error('type shadow'); }}});
        Object.setPrototypeOf(blob, null);
        const url = URL.createObjectURL(blob);
        try { assert(typeof url === 'string' && url.startsWith('blob:')); } finally { URL.revokeObjectURL(url); }
    });
    return rows;
}
