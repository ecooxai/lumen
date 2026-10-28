// Shared Blob/File construction and immutable data for window and worker realms.
// The host bridge stores a V8 private record on each genuine platform object.
// Public prototypes, toStringTag, _buf/_b properties and Proxy wrappers are
// never used to recognize an instance or obtain its byte sequence.
function createBlobFileInterfaces(nativeIDL, host) {
    const stateOf = host.blobState, encode = host.encode, decode = host.decode;
    const define = Object.defineProperty, descriptor = Object.getOwnPropertyDescriptor;
    const create = Object.create, setPrototype = Object.setPrototypeOf;
    const apply = Reflect.apply, ProxyIntrinsic = Proxy;
    const StringIntrinsic = String, NumberIntrinsic = Number, BigIntIntrinsic = BigInt;
    const TypeErrorIntrinsic = TypeError, RangeErrorIntrinsic = RangeError;
    const PromiseIntrinsic = Promise, Bytes = Uint8Array;
    const byteLength = descriptor(ArrayBuffer.prototype, 'byteLength').get;
    const resizableDescriptor = descriptor(ArrayBuffer.prototype, 'resizable');
    const resizable = resizableDescriptor && resizableDescriptor.get;
    const typedPrototype = Object.getPrototypeOf(Bytes.prototype);
    const typedBuffer = descriptor(typedPrototype, 'buffer').get;
    const typedOffset = descriptor(typedPrototype, 'byteOffset').get;
    const typedLength = descriptor(typedPrototype, 'byteLength').get;
    const dataBuffer = descriptor(DataView.prototype, 'buffer').get;
    const dataOffset = descriptor(DataView.prototype, 'byteOffset').get;
    const dataLength = descriptor(DataView.prototype, 'byteLength').get;
    const sharedLength = typeof SharedArrayBuffer === 'function' ?
        descriptor(SharedArrayBuffer.prototype, 'byteLength').get : null;
    const isView = ArrayBuffer.isView, setBytes = Bytes.prototype.set;
    const charCode = String.prototype.charCodeAt, stringSlice = String.prototype.slice;
    const lower = String.prototype.toLowerCase;
    const floor = Math.floor, trunc = Math.trunc, min = Math.min, max = Math.max;
    const intN = BigInt.asIntN, now = Date.now;
    const blobKind = 6, fileKind = 7;
    const objectValue = value => value !== null &&
        (typeof value === 'object' || typeof value === 'function');

    function domString(value) {
        if (typeof value === 'symbol') throw new TypeErrorIntrinsic('Cannot convert a Symbol value to a string');
        return StringIntrinsic(value);
    }
    function usvString(value) {
        const string = domString(value);
        let result = '';
        for (let index = 0; index < string.length; ++index) {
            const point = apply(charCode, string, [index]);
            if (point >= 0xd800 && point <= 0xdbff) {
                const next = index + 1 < string.length ? apply(charCode, string, [index + 1]) : 0;
                if (next >= 0xdc00 && next <= 0xdfff) {
                    result += apply(stringSlice, string, [index, index + 2]); ++index;
                } else result += '\ufffd';
            } else if (point >= 0xdc00 && point <= 0xdfff) result += '\ufffd';
            else result += apply(stringSlice, string, [index, index + 1]);
        }
        return result;
    }
    function mimeType(value) {
        const string = domString(value);
        for (let index = 0; index < string.length; ++index) {
            const point = apply(charCode, string, [index]);
            if (point < 0x20 || point > 0x7e) return '';
        }
        return apply(lower, string, []);
    }
    function nativeEndings(string) {
        // Both supported Apple platforms use LF as their native line ending.
        let result = '';
        for (let index = 0; index < string.length; ++index) {
            if (apply(charCode, string, [index]) === 13) {
                if (apply(charCode, string, [index + 1]) === 10) ++index;
                result += '\n';
            } else result += apply(stringSlice, string, [index, index + 1]);
        }
        return result;
    }
    function longLong(value) {
        const number = +value;
        if (number !== number || number === 0 || number === Infinity || number === -Infinity) return 0;
        return NumberIntrinsic(intN(64, BigIntIntrinsic(trunc(number))));
    }
    function clampLongLong(value) {
        let number = +value;
        if (number !== number || number === 0) return 0;
        number = min(max(number, -9223372036854775808), 9223372036854775807);
        const integer = floor(number), fraction = number - integer;
        return fraction < 0.5 ? integer : fraction > 0.5 ? integer + 1 : integer % 2 === 0 ? integer : integer + 1;
    }
    function viewRecord(value) {
        let buffer;
        try { buffer = apply(typedBuffer, value, []); }
        catch (_) { buffer = apply(dataBuffer, value, []); return {buffer, offset: dataOffset, length: dataLength}; }
        return {buffer, offset: typedOffset, length: typedLength};
    }
    function rejectResizable(buffer) {
        if (resizable && apply(resizable, buffer, []))
            throw new TypeErrorIntrinsic('A resizable buffer is not a BlobPart');
    }
    function asPart(value) {
        const blob = stateOf(value);
        if (blob !== undefined) return {kind: 2, value: blob};
        if (objectValue(value)) {
            let buffer = false;
            try { apply(byteLength, value, []); buffer = true; } catch (_) {}
            if (buffer) { rejectResizable(value); return {kind: 0, value}; }
            if (isView(value)) {
                const view = viewRecord(value);
                // Shared backing stores are not accepted by BlobPart BufferSource.
                apply(byteLength, view.buffer, []);
                rejectResizable(view.buffer);
                return {kind: 1, value};
            }
            if (sharedLength) {
                let shared = false;
                try { apply(sharedLength, value, []); shared = true; } catch (_) {}
                if (shared) throw new TypeErrorIntrinsic('A shared buffer is not a BlobPart');
            }
        }
        return {kind: 3, value: usvString(value)};
    }
    function convertParts(value, optional) {
        if (value === undefined && optional) return {items: create(null), length: 0};
        if (!objectValue(value)) throw new TypeErrorIntrinsic('Blob parts must be an iterable object');
        const items = create(null);
        let length = 0;
        // for-of performs IteratorClose on a BlobPart conversion exception.
        // A null-prototype accumulator avoids inherited numeric setters.
        for (const item of value) items[length++] = asPart(item);
        return {items, length};
    }
    function convertOptions(value, file) {
        if (value !== undefined && value !== null && !objectValue(value))
            throw new TypeErrorIntrinsic('Blob options must be a dictionary');
        const endingsValue = value == null ? undefined : value.endings;
        const endings = endingsValue === undefined ? 'transparent' : domString(endingsValue);
        if (endings !== 'transparent' && endings !== 'native')
            throw new TypeErrorIntrinsic('Unknown Blob line ending mode');
        const typeValue = value == null ? undefined : value.type;
        const type = typeValue === undefined ? '' : mimeType(typeValue);
        // Web IDL converts inherited dictionary members before File's member.
        const modifiedValue = file && value != null ? value.lastModified : undefined;
        const modified = file && modifiedValue !== undefined ? longLong(modifiedValue) : undefined;
        return {endings, type, modified};
    }
    function copyBuffer(source, start = 0, length = undefined) {
        const size = apply(byteLength, source, []);
        if (length === undefined) length = size - start;
        const output = new Bytes(length);
        apply(setBytes, output, [new Bytes(source, start, length)]);
        return apply(typedBuffer, output, []);
    }
    function processParts(parts, endings) {
        const chunks = create(null);
        let size = 0;
        // Copying occurs after all argument conversions, including options.
        // No author callbacks occur in this byte-processing pass.
        for (let index = 0; index < parts.length; ++index) {
            const part = parts.items[index];
            let bytes;
            if (part.kind === 0) bytes = apply(byteLength, part.value, []) === 0 ? new Bytes(0) : new Bytes(part.value);
            else if (part.kind === 1) {
                const record = viewRecord(part.value);
                // Web IDL copies a detached source as an empty byte sequence.
                // Test the backing store before detached DataView accessors.
                bytes = apply(byteLength, record.buffer, []) === 0 ? new Bytes(0) :
                    new Bytes(record.buffer, apply(record.offset, part.value, []), apply(record.length, part.value, []));
            } else if (part.kind === 2) bytes = new Bytes(part.value.bytes);
            else bytes = new Bytes(encode(endings === 'native' ? nativeEndings(part.value) : part.value));
            const length = apply(typedLength, bytes, []);
            if (size + length > 9007199254740991) throw new RangeErrorIntrinsic('Blob data is too large');
            chunks[index] = bytes; size += length;
        }
        const output = new Bytes(size);
        let offset = 0;
        for (let index = 0; index < parts.length; ++index) {
            const chunk = chunks[index];
            apply(setBytes, output, [chunk, offset]); offset += apply(typedLength, chunk, []);
        }
        return apply(typedBuffer, output, []);
    }
    function requireBlob(object) {
        const record = stateOf(object);
        if (record === undefined) throw new TypeErrorIntrinsic('Illegal invocation');
        return record;
    }
    function requireFile(object) {
        const record = requireBlob(object);
        if (!record.file) throw new TypeErrorIntrinsic('Illegal invocation');
        return record;
    }
    function recordFor(bytes, type, file = false, name = '', modified = 0) {
        return {bytes, type, size: apply(byteLength, bytes, []), file, name,
                lastModified: modified, make: makeSlice, read: readBytes, copy: copyBuffer,
                stream: makeStream, mediaPath: null};
    }
    function makeSlice(source, start, length, type) {
        const object = create(Blob.prototype);
        stateOf(object, recordFor(copyBuffer(source, start, length), type));
        return object;
    }
    function readBytes(record, format) {
        // The Promise and byte result belong to the receiver's creation realm.
        return new PromiseIntrinsic((resolve, reject) => {
            try {
                // The unexposed native decoder only reads this private store.
                // Text reads need no second full-sized byte allocation.
                if (format === 2) { resolve(decode(record.bytes)); return; }
                const bytes = copyBuffer(record.bytes);
                resolve(format === 0 ? bytes : new Bytes(bytes));
            } catch (error) { reject(error); }
        });
    }
    function rejectedReceiver() {
        // A wrong receiver rejects in the borrowing operation's realm.
        return new PromiseIntrinsic((resolve, reject) => reject(new TypeErrorIntrinsic('Illegal invocation')));
    }
    function makeStream(record) {
        if (!host.stream) throw new TypeErrorIntrinsic('ReadableStream is unavailable in this realm');
        return host.stream(new Bytes(copyBuffer(record.bytes)));
    }
    class Blob {
        constructor(parts = undefined) { throw new TypeErrorIntrinsic('Blob construction target is internal'); }
        get size() { return requireBlob(this).size; }
        get type() { return requireBlob(this).type; }
        slice(start = 0, end = undefined, contentType = '') {
            const record = requireBlob(this);
            const first = clampLongLong(start), last = end === undefined ? undefined : clampLongLong(end);
            const type = mimeType(contentType), size = record.size;
            const relativeStart = first < 0 ? max(size + first, 0) : min(first, size);
            const relativeEnd = last === undefined ? size : last < 0 ? max(size + last, 0) : min(last, size);
            return record.make(record.bytes, relativeStart, max(relativeEnd - relativeStart, 0), type);
        }
        arrayBuffer() { const record = stateOf(this); return record === undefined ? rejectedReceiver() : record.read(record, 0); }
        bytes() { const record = stateOf(this); return record === undefined ? rejectedReceiver() : record.read(record, 1); }
        text() { const record = stateOf(this); return record === undefined ? rejectedReceiver() : record.read(record, 2); }
        stream() { const record = requireBlob(this); return record.stream(record); }
    }
    class File extends Blob {
        constructor(fileBits, fileName) { super(); throw new TypeErrorIntrinsic('File construction target is internal'); }
        get name() { return requireFile(this).name; }
        get lastModified() { return requireFile(this).lastModified; }
    }
    const BlobInterface = new ProxyIntrinsic(Blob, {
        construct(target, args, newTarget) {
            const parts = convertParts(args.length ? args[0] : undefined, true);
            const options = convertOptions(args.length > 1 ? args[1] : undefined, false);
            let prototype = newTarget.prototype;
            if (!objectValue(prototype)) prototype = nativeIDL.fallback(blobKind, newTarget);
            const object = create(prototype);
            stateOf(object, recordFor(processParts(parts, options.endings), options.type));
            return object;
        },
    });
    const FileInterface = new ProxyIntrinsic(File, {
        construct(target, args, newTarget) {
            if (args.length < 2) throw new TypeErrorIntrinsic('File requires two arguments');
            const parts = convertParts(args[0], false), name = usvString(args[1]);
            const options = convertOptions(args.length > 2 ? args[2] : undefined, true);
            let prototype = newTarget.prototype;
            if (!objectValue(prototype)) prototype = nativeIDL.fallback(fileKind, newTarget);
            const object = create(prototype);
            const bytes = processParts(parts, options.endings);
            stateOf(object, recordFor(bytes, options.type, true, name,
                options.modified === undefined ? now() : options.modified));
            return object;
        },
    });
    setPrototype(File, BlobInterface);
    define(Blob.prototype, 'constructor', {value: BlobInterface, writable: true, configurable: true});
    define(File.prototype, 'constructor', {value: FileInterface, writable: true, configurable: true});
    nativeIDL.register(blobKind, Blob.prototype); nativeIDL.register(fileKind, File.prototype);
    for (const name of ['size', 'type', 'slice', 'arrayBuffer', 'bytes', 'text', 'stream'])
        define(Blob.prototype, name, {...descriptor(Blob.prototype, name), enumerable: true});
    for (const name of ['name', 'lastModified'])
        define(File.prototype, name, {...descriptor(File.prototype, name), enumerable: true});
    define(Blob.prototype, Symbol.toStringTag, {value: 'Blob', configurable: true});
    define(File.prototype, Symbol.toStringTag, {value: 'File', configurable: true});
    // Worker stream methods are installed only with the real shared stream
    // implementation in the following development cycle, not a fake backend.
    if (!host.stream) delete Blob.prototype.stream;
    return {
        Blob: BlobInterface, File: FileInterface,
        isBlob: value => stateOf(value) !== undefined,
        isFile: value => { const record = stateOf(value); return record !== undefined && record.file; },
        size: value => requireBlob(value).size,
        type: value => requireBlob(value).type,
        name: value => requireFile(value).name,
        modified: value => requireFile(value).lastModified,
        copyBuffer: value => { const record = requireBlob(value); return record.copy(record.bytes); },
        mediaFile(value, path) {
            const record = requireBlob(value);
            if (arguments.length > 1) record.mediaPath = path;
            return record.mediaPath;
        },
        usvString,
    };
}
