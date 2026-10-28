// The window and dedicated worker use the same Web IDL URL binding. Native
// parsing, component mutation and each realm's existing object-URL store remain
// the source of URL records and blob ownership.
function createURLInterface(parseNative, setNative, paramsOps, objectURLs, nativeIDL) {
    const define = Object.defineProperty;
    const descriptor = Object.getOwnPropertyDescriptor, create = Object.create;
    const TypeErrorIntrinsic = TypeError, ProxyIntrinsic = Proxy;
    const getState = nativeIDL.get, putState = nativeIDL.put;
    const registerPrototype = nativeIDL.register, fallbackPrototype = nativeIDL.fallback;
    const kind = 1;
    const keys = ['href', 'protocol', 'username', 'password', 'host', 'hostname', 'port', 'pathname', 'search', 'hash', 'origin'];
    const usv = paramsOps.usv;
    const state = object => {
        const value = getState(kind, object);
        if (value === undefined) throw new TypeErrorIntrinsic('Illegal invocation');
        return value;
    };
    const put = (object, parts) => putState(kind, object, { parts, params: null, paramsOps, setNative });
    const required = count => {
        if (count < 1) throw new TypeErrorIntrinsic('At least one argument is required');
    };
    const parseArguments = (input, base) => {
        // Web IDL converts arguments in order before the native URL algorithm.
        input = usv(input);
        base = base === undefined ? null : usv(base);
        return parseNative(input, base);
    };
    const update = (object, index, input) => {
        const current = state(object);
        const value = usv(input);
        // A conversion may re-enter this URL, so read its current record only
        // after conversion. Native host/port partial commits are retained.
        const record = index === 0 ? parseNative(value, null) : setNative(current.parts[0], index, value);
        if (!record) {
            if (index === 0) throw new TypeErrorIntrinsic('Invalid URL');
            return;
        }
        current.parts = record;
        if (current.params !== null && (index === 0 || index === 8)) {
            current.paramsOps.replace(current.params, record[8]);
        }
    };
    class URL {
        constructor(input, base = undefined) {
            // Construction is handled by the native-style construct trap below.
            // The target stays a constructor with the required public arity.
            throw new TypeErrorIntrinsic('URL constructor target is internal');
        }
        get searchParams() {
            const current = state(this);
            if (current.params === null) {
                const params = new current.paramsOps.URLSearchParams(current.parts[8]);
                current.paramsOps.link(params, query => {
                    const record = current.setNative(current.parts[0], 8, query);
                    if (record) current.parts = record;
                });
                current.params = params;
            }
            return current.params;
        }
        toString() { return state(this).parts[0]; }
        toJSON() { return state(this).parts[0]; }
        static parse(input, base = undefined) {
            required(arguments.length);
            const parts = parseArguments(input, base);
            if (!parts) return null;
            const object = create(URL.prototype);
            put(object, parts);
            return object;
        }
        static canParse(input, base = undefined) {
            required(arguments.length);
            return parseArguments(input, base) !== null;
        }
        static createObjectURL(blob) {
            required(arguments.length);
            return objectURLs.create(blob);
        }
        static revokeObjectURL(input) {
            required(arguments.length);
            objectURLs.revoke(usv(input));
        }
    }
    const Constructor = new ProxyIntrinsic(URL, {
        construct(target, args, newTarget) {
            required(args.length);
            // Interface argument conversions precede the single prototype Get.
            const input = usv(args[0]);
            const base = args[1] === undefined ? null : usv(args[1]);
            let prototype = newTarget.prototype;
            if ((typeof prototype !== 'object' || prototype === null) && typeof prototype !== 'function') {
                prototype = fallbackPrototype(kind, newTarget);
            }
            const object = create(prototype);
            const parts = parseNative(input, base);
            if (!parts) throw new TypeErrorIntrinsic("Failed to construct 'URL': Invalid URL");
            put(object, parts);
            return object;
        },
    });
    define(URL.prototype, 'constructor', { value: Constructor, writable: true, configurable: true });
    registerPrototype(kind, URL.prototype);
    for (let index = 0; index < keys.length; index++) {
        const key = keys[index];
        // Genuine accessor functions have the specified names/arities and no
        // [[Construct]] or own prototype, unlike ordinary function expressions.
        const functions = descriptor({
            get [key]() { return state(this).parts[index]; },
            set [key](value) { update(this, index, value); },
        }, key);
        define(URL.prototype, key, { get: functions.get,
            set: key === 'origin' ? undefined : functions.set,
            enumerable: true, configurable: true });
    }
    for (const key of ['searchParams', 'toString', 'toJSON']) {
        define(URL.prototype, key, { ...descriptor(URL.prototype, key), enumerable: true });
    }
    for (const key of ['parse', 'canParse', 'createObjectURL', 'revokeObjectURL']) {
        define(URL, key, { ...descriptor(URL, key), enumerable: true });
    }
    define(URL.prototype, Symbol.toStringTag, { value: 'URL', configurable: true });
    return { URL: Constructor, keys };
}
