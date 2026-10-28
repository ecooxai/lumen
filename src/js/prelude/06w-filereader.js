// One File API reader implementation for window and dedicated-worker realms.
// Native private records recognize real platform objects across same-isolate
// realms without consulting mutable prototypes or public Blob byte properties.
function createFileReaderInterfaces(nativeIDL, host, exposeSync) {
    const getState = nativeIDL.get, putState = nativeIDL.put;
    const register = nativeIDL.register, fallback = nativeIDL.fallback;
    const define = Object.defineProperty, descriptor = Object.getOwnPropertyDescriptor;
    const create = Object.create, setPrototype = Object.setPrototypeOf;
    const apply = Reflect.apply, construct = Reflect.construct;
    const ProxyIntrinsic = Proxy, TypeErrorIntrinsic = TypeError;
    const StringIntrinsic = String, Bytes = Uint8Array;
    const byteLength = descriptor(ArrayBuffer.prototype, 'byteLength').get;
    const bufferOf = descriptor(Object.getPrototypeOf(Bytes.prototype), 'buffer').get;
    const setBytes = Bytes.prototype.set;
    const EventTargetIntrinsic = EventTarget, DOMExceptionIntrinsic = DOMException;
    const addListener = EventTargetIntrinsic.prototype.addEventListener;
    const removeListener = EventTargetIntrinsic.prototype.removeEventListener;
    const schedule = host.schedule, cancel = host.cancel;
    const blobState = host.blobState, packageNative = host.package;
    const fire = host.fire;
    const readerKind = 4, syncKind = 5;
    const events = ['loadstart', 'progress', 'load', 'abort', 'error', 'loadend'];

    const objectValue = value => value !== null &&
        (typeof value === 'object' || typeof value === 'function');
    const state = (kind, object) => {
        const record = getState(kind, object);
        if (record === undefined) throw new TypeErrorIntrinsic('Illegal invocation');
        return record;
    };
    const domString = value => {
        if (typeof value === 'symbol') throw new TypeErrorIntrinsic('Cannot convert a Symbol value to a string');
        return StringIntrinsic(value);
    };
    const convert = (blob, encoding, supplied) => {
        if (!supplied) throw new TypeErrorIntrinsic('At least one argument is required');
        const source = blobState(blob);
        if (source === undefined) throw new TypeErrorIntrinsic('The argument is not a Blob');
        return { source, encoding: encoding === undefined ? undefined : domString(encoding) };
    };
    const copy = source => {
        const size = apply(byteLength, source, []);
        const bytes = new Bytes(size);
        apply(setBytes, bytes, [new Bytes(source)]);
        return apply(bufferOf, bytes, []);
    };
    const active = (record, job) => record.job === job && record.readyState === 1;
    const enqueue = (record, job, callback) => {
        job.timer = schedule(() => {
            job.timer = null;
            if (active(record, job)) callback();
        });
    };
    // Only the queued read-task path requests listener checkpoints. An abort
    // invoked on an author script stack keeps its synchronous dispatch behavior.
    const progress = (object, name, loaded, total, queued = true) =>
        fire(object, name, loaded, total, queued);

    function finish(object, record, job) {
        if (!active(record, job)) return;
        record.readyState = 2;
        record.job = null;
        let failed = job.error;
        if (!failed) {
            try {
                // ArrayBuffer packaging can transfer our private copied buffer
                // directly to the result. No public Blob buffer is returned.
                record.result = job.format === 'ArrayBuffer' ? job.bytes :
                    packageNative(job.bytes, job.format, job.encoding, job.type);
            } catch (error) {
                failed = error;
            }
        }
        job.bytes = null;
        if (failed) {
            record.error = failed;
            record.result = null;
            progress(object, 'error', 0, 0);
        } else {
            progress(object, 'load', job.total, job.total);
        }
        // The private dispatch hook drains the complete native microtask
        // checkpoint after each invoked listener, including nested reactions.
        // Re-check current state only after those callbacks have finished: a
        // load/error listener or one of its reactions may start another read.
        if (record.readyState !== 1)
            progress(object, 'loadend', failed ? 0 : job.total, failed ? 0 : job.total);
    }

    function begin(object, record, converted, format) {
        record.readyState = 1;
        record.result = null;
        record.error = null;
        const source = converted.source;
        const job = { timer: null, bytes: null, total: 0, format,
            encoding: converted.encoding, type: source.type, error: null };
        record.job = job;
        try {
            job.bytes = copy(source.bytes);
            job.total = apply(byteLength, job.bytes, []);
        } catch (error) {
            job.error = new DOMExceptionIntrinsic('The Blob data cannot be read', 'NotReadableError');
        }
        enqueue(record, job, () => {
            if (job.error) { finish(object, record, job); return; }
            progress(object, 'loadstart', 0, job.total);
            if (!active(record, job)) return;
            enqueue(record, job, () => {
                if (job.total) {
                    progress(object, 'progress', job.total, job.total);
                    if (!active(record, job)) return;
                    // The progress event must finish its own task/checkpoint
                    // while readyState is LOADING and result remains null.
                    enqueue(record, job, () => finish(object, record, job));
                } else finish(object, record, job);
            });
        });
    }

    function abort(object, record) {
        record.result = null;
        if (record.readyState !== 1) return;
        const job = record.job;
        record.readyState = 2;
        record.job = null;
        if (job) {
            if (job.timer !== null) cancel(job.timer);
            job.timer = null;
            job.bytes = null;
        }
        // Abort's two events are synchronous. A handler can begin another read,
        // in which case the previous operation must not emit loadend.
        progress(object, 'abort', 0, 0, false);
        if (record.readyState !== 1) progress(object, 'loadend', 0, 0, false);
    }

    function setHandler(object, record, type, value) {
        const handler = record.handlers[type];
        value = objectValue(value) ? value : null;
        handler.value = value;
        if (value === null) {
            if (handler.listener !== null) apply(removeListener, object, [type, handler.listener]);
            handler.listener = null;
        } else if (handler.listener === null) {
            handler.listener = event => {
                const callback = handler.value;
                if (typeof callback === 'function' && apply(callback, object, [event]) === false)
                    event.preventDefault();
            };
            apply(addListener, object, [type, handler.listener]);
        }
    }

    class FileReader extends EventTargetIntrinsic {
        constructor() { super(); throw new TypeErrorIntrinsic('FileReader constructor target is internal'); }
        get readyState() { return state(readerKind, this).readyState; }
        get result() { return state(readerKind, this).result; }
        get error() { return state(readerKind, this).error; }
        readAsArrayBuffer(blob) {
            const record = state(readerKind, this), converted = convert(blob, undefined, arguments.length);
            if (record.readyState === 1) throw new DOMExceptionIntrinsic('A read is already in progress', 'InvalidStateError');
            record.begin(this, record, converted, 'ArrayBuffer');
        }
        readAsBinaryString(blob) {
            const record = state(readerKind, this), converted = convert(blob, undefined, arguments.length);
            if (record.readyState === 1) throw new DOMExceptionIntrinsic('A read is already in progress', 'InvalidStateError');
            record.begin(this, record, converted, 'BinaryString');
        }
        readAsText(blob, encoding = undefined) {
            const record = state(readerKind, this), converted = convert(blob, encoding, arguments.length);
            if (record.readyState === 1) throw new DOMExceptionIntrinsic('A read is already in progress', 'InvalidStateError');
            record.begin(this, record, converted, 'Text');
        }
        readAsDataURL(blob) {
            const record = state(readerKind, this), converted = convert(blob, undefined, arguments.length);
            if (record.readyState === 1) throw new DOMExceptionIntrinsic('A read is already in progress', 'InvalidStateError');
            record.begin(this, record, converted, 'DataURL');
        }
        abort() { const record = state(readerKind, this); record.abort(this, record); }
    }
    const Reader = new ProxyIntrinsic(FileReader, {
        construct(target, args, newTarget) {
            let prototype = newTarget.prototype;
            if (!objectValue(prototype)) prototype = fallback(readerKind, newTarget);
            const object = construct(EventTargetIntrinsic, [], FileReader);
            setPrototype(object, prototype);
            const handlers = create(null);
            for (const type of events) handlers[type] = { value: null, listener: null };
            putState(readerKind, object, { readyState: 0, result: null, error: null,
                handlers, job: null, begin, abort, setHandler });
            return object;
        },
    });
    define(FileReader.prototype, 'constructor', { value: Reader, writable: true, configurable: true });
    register(readerKind, FileReader.prototype);
    for (const name of ['readyState', 'result', 'error', 'readAsArrayBuffer', 'readAsBinaryString',
                        'readAsText', 'readAsDataURL', 'abort']) {
        define(FileReader.prototype, name, { ...descriptor(FileReader.prototype, name), enumerable: true });
    }
    for (const type of events) {
        const name = 'on' + type;
        const accessors = descriptor({
            get [name]() { return state(readerKind, this).handlers[type].value; },
            set [name](value) { const record = state(readerKind, this); record.setHandler(this, record, type, value); },
        }, name);
        define(FileReader.prototype, name, { ...accessors, enumerable: true, configurable: true });
    }
    for (const [name, value] of [['EMPTY', 0], ['LOADING', 1], ['DONE', 2]]) {
        for (const object of [FileReader, FileReader.prototype]) define(object, name, { value, enumerable: true });
    }
    define(FileReader.prototype, Symbol.toStringTag, { value: 'FileReader', configurable: true });

    if (!exposeSync) return { FileReader: Reader };
    const packageSync = (source, format, encoding) => {
        const bytes = copy(source.bytes);
        return format === 'ArrayBuffer' ? bytes : packageNative(bytes, format, encoding, source.type);
    };
    class FileReaderSync {
        constructor() { throw new TypeErrorIntrinsic('FileReaderSync constructor target is internal'); }
        readAsArrayBuffer(blob) {
            const record = state(syncKind, this), converted = convert(blob, undefined, arguments.length);
            return record.package(converted.source, 'ArrayBuffer', undefined);
        }
        readAsBinaryString(blob) {
            const record = state(syncKind, this), converted = convert(blob, undefined, arguments.length);
            return record.package(converted.source, 'BinaryString', undefined);
        }
        readAsText(blob, encoding = undefined) {
            const record = state(syncKind, this), converted = convert(blob, encoding, arguments.length);
            return record.package(converted.source, 'Text', converted.encoding);
        }
        readAsDataURL(blob) {
            const record = state(syncKind, this), converted = convert(blob, undefined, arguments.length);
            return record.package(converted.source, 'DataURL', undefined);
        }
    }
    const Sync = new ProxyIntrinsic(FileReaderSync, {
        construct(target, args, newTarget) {
            let prototype = newTarget.prototype;
            if (!objectValue(prototype)) prototype = fallback(syncKind, newTarget);
            const object = create(prototype);
            putState(syncKind, object, { package: packageSync });
            return object;
        },
    });
    define(FileReaderSync.prototype, 'constructor', { value: Sync, writable: true, configurable: true });
    register(syncKind, FileReaderSync.prototype);
    for (const name of ['readAsArrayBuffer', 'readAsBinaryString', 'readAsText', 'readAsDataURL'])
        define(FileReaderSync.prototype, name, { ...descriptor(FileReaderSync.prototype, name), enumerable: true });
    define(FileReaderSync.prototype, Symbol.toStringTag, { value: 'FileReaderSync', configurable: true });
    return { FileReader: Reader, FileReaderSync: Sync };
}
