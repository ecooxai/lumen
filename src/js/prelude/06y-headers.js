// Fetch header-list algorithms shared by the window and dedicated-worker APIs.
// Window state uses isolate-private slots so methods also accept another realm's
// genuine Headers object. A worker has one realm and uses an isolated WeakMap.
function createHeadersInterface(bridge = {}) {
    const states = new WeakMap();
    const apply = Reflect.apply, ownKeys = Reflect.ownKeys;
    const descriptor = Object.getOwnPropertyDescriptor, define = Object.defineProperty;
    const mapGet = Function.call.bind(Map.prototype.get), mapSet = Function.call.bind(Map.prototype.set);
    const setHas = Function.call.bind(Set.prototype.has);
    const mapHas = Function.call.bind(Map.prototype.has), mapDelete = Function.call.bind(Map.prototype.delete);
    const mapKeys = Function.call.bind(Map.prototype.keys), mapEntries = Function.call.bind(Map.prototype.entries);
    const arraySort = Function.call.bind(Array.prototype.sort), arrayJoin = Function.call.bind(Array.prototype.join);
    const push = (array, value) => define(array, array.length, { value, writable: true, enumerable: true, configurable: true });
    const read = bridge.getState || (object => states.get(object));
    const write = bridge.setState || ((object, state) => states.set(object, state));
    const object = value => value !== null && (typeof value === 'object' || typeof value === 'function');
    function state(value) {
        const result = object(value) && read(value);
        if (!result || result.brand !== 'headers') throw new TypeError('Illegal invocation');
        return result;
    }
    function argumentsRequired(count, minimum) {
        if (count < minimum) throw new TypeError('Not enough arguments');
    }
    function bytes(value) {
        if (typeof value === 'symbol') throw new TypeError('A Symbol cannot be converted to a ByteString');
        const result = String(value);
        if (/[^\x00-\xFF]/.test(result)) throw new TypeError('Value is not a ByteString');
        return result;
    }
    function name(value) {
        if (!/^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/.test(value)) throw new TypeError('Invalid header name');
        return value.toLowerCase();
    }
    const normalize = value => value.replace(/^[\t\n\r ]+|[\t\n\r ]+$/g, '');
    function sequence(value, convert, method) {
        if (!object(value)) throw new TypeError('Expected a sequence object');
        if (method === undefined) method = value[Symbol.iterator];
        if (typeof method !== 'function') throw new TypeError('Expected an iterable');
        const result = [], iterable = { [Symbol.iterator]() { return apply(method, value, []); } };
        for (const item of iterable) push(result, convert(item));
        return result;
    }
    function convertInit(init) {
        if (init === undefined) return [];
        if (!object(init)) throw new TypeError('Headers initializer must be an object');
        const method = init[Symbol.iterator];
        if (method !== undefined && method !== null) return sequence(init, item => sequence(item, bytes), method);
        const result = [];
        for (const key of ownKeys(init)) {
            const property = descriptor(init, key);
            if (property && property.enumerable) {
                const convertedKey = bytes(key), convertedValue = bytes(init[key]);
                push(result, [convertedKey, convertedValue]);
            }
        }
        return result;
    }
    const forbidden = new Set(['accept-charset', 'accept-encoding', 'access-control-request-headers',
        'access-control-request-method', 'connection', 'content-length', 'cookie', 'cookie2', 'date',
        'dnt', 'expect', 'host', 'keep-alive', 'origin', 'referer', 'set-cookie', 'te', 'trailer',
        'transfer-encoding', 'upgrade', 'via']);
    function forbiddenRequest(key, value) {
        if (setHas(forbidden, key) || /^(proxy-|sec-)/.test(key)) return true;
        if (!/^(x-http-method|x-http-method-override|x-method-override)$/.test(key)) return false;
        // Fetch get/decode/split retains quoted strings, including commas and
        // escaped quotes, as one value; only an unquoted forbidden method wins.
        let start = 0, quoted = false;
        for (let index = 0; index <= value.length; index++) {
            const ch = value[index];
            if (quoted && ch === '\\') { index++; continue; }
            if (ch === '"') { quoted = !quoted; continue; }
            if (index === value.length || (ch === ',' && !quoted)) {
                const token = value.slice(start, index).replace(/^[\t ]+|[\t ]+$/g, '');
                if (/^(CONNECT|TRACE|TRACK)$/i.test(token)) return true;
                start = index + 1;
            }
        }
        return false;
    }
    const safeName = key => /^(accept|accept-language|content-language|content-type)$/.test(key);
    function safeHeader(key, value) {
        if (value.length > 128 || !safeName(key)) return false;
        if (key === 'accept-language' || key === 'content-language') return /^[0-9A-Za-z *,\-.;=]*$/.test(value);
        if (/[\x00-\x08\x0A-\x1F"():<>?@\[\\\]{}\x7F]/.test(value)) return false;
        if (key === 'accept') return true;
        const essence = value.split(';', 1)[0].replace(/^[\t ]+|[\t ]+$/g, '').toLowerCase();
        return essence === 'application/x-www-form-urlencoded' || essence === 'multipart/form-data' || essence === 'text/plain';
    }
    function validate(current, key, value) {
        name(key);
        if (/[\x00\r\n]/.test(value)) throw new TypeError('Invalid header value');
        if (current.guard === 'immutable') throw new TypeError('Headers are immutable');
        if (current.guard === 'request' && forbiddenRequest(key, value)) return false;
        if (current.guard === 'response' && (key === 'set-cookie' || key === 'set-cookie2')) return false;
        return true;
    }
    function get(current, key) {
        const values = mapGet(current.list, key);
        return values === undefined ? null : arrayJoin(values, ', ');
    }
    function append(current, key, value) {
        key = name(key); value = normalize(value);
        if (!validate(current, key, value)) return;
        const previous = get(current, key);
        if (current.guard === 'request-no-cors' && !safeHeader(key, previous === null ? value : previous + ', ' + value)) return;
        if (!mapHas(current.list, key)) mapSet(current.list, key, []);
        push(mapGet(current.list, key), value);
        if (current.guard === 'request-no-cors') mapDelete(current.list, 'range');
    }
    function fill(target, init) {
        const converted = convertInit(init), current = state(target);
        for (const pair of converted) {
            if (pair.length !== 2) throw new TypeError('Header sequence must contain exactly two items');
            append(current, pair[0], pair[1]);
        }
    }
    function pairs(current) {
        const names = [];
        for (const key of mapKeys(current.list)) push(names, key);
        arraySort(names);
        const result = [];
        for (const key of names) {
            if (key === 'set-cookie') for (const value of mapGet(current.list, key)) push(result, [key, value]);
            else push(result, [key, get(current, key)]);
        }
        return result;
    }
    class HeaderIterator {
        next() {
            const it = object(this) && read(this);
            if (!it || it.brand !== 'headers-iterator') throw new TypeError('Illegal invocation');
            const values = pairs(state(it.target));
            if (it.index >= values.length) return { value: undefined, done: true };
            const pair = values[it.index++];
            return { value: it.kind === 'key' ? pair[0] : it.kind === 'value' ? pair[1] : pair, done: false };
        }
    }
    Object.setPrototypeOf(HeaderIterator.prototype, Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]())));
    define(HeaderIterator.prototype, Symbol.toStringTag, { value: 'Headers Iterator', configurable: true });
    define(HeaderIterator.prototype, 'next', { ...descriptor(HeaderIterator.prototype, 'next'), enumerable: true });
    function iterator(target, kind) {
        state(target);
        const it = new HeaderIterator(); write(it, { brand: 'headers-iterator', target, kind, index: 0 });
        return it;
    }
    class Headers {
        constructor(init = undefined) { write(this, { brand: 'headers', list: new Map(), guard: 'none' }); fill(this, init); }
        append(key, value) {
            const current = state(this); argumentsRequired(arguments.length, 2);
            key = bytes(key); value = bytes(value); append(current, key, value);
        }
        set(key, value) {
            const current = state(this); argumentsRequired(arguments.length, 2);
            key = bytes(key); value = bytes(value); key = name(key); value = normalize(value);
            if (!validate(current, key, value) || (current.guard === 'request-no-cors' && !safeHeader(key, value))) return;
            mapSet(current.list, key, [value]);
            if (current.guard === 'request-no-cors') mapDelete(current.list, 'range');
        }
        get(key) { const current = state(this); argumentsRequired(arguments.length, 1); return get(current, name(bytes(key))); }
        has(key) { const current = state(this); argumentsRequired(arguments.length, 1); return mapHas(current.list, name(bytes(key))); }
        delete(key) {
            const current = state(this); argumentsRequired(arguments.length, 1); key = name(bytes(key));
            if (!validate(current, key, '')) return;
            if (current.guard === 'request-no-cors' && !safeName(key) && key !== 'range') return;
            if (!mapHas(current.list, key)) return;
            mapDelete(current.list, key);
            if (current.guard === 'request-no-cors') mapDelete(current.list, 'range');
        }
        getSetCookie() {
            const result = [], values = mapGet(state(this).list, 'set-cookie');
            if (values) for (const value of values) push(result, value);
            return result;
        }
        entries() { return iterator(this, 'entry'); }
        keys() { return iterator(this, 'key'); }
        values() { return iterator(this, 'value'); }
        forEach(callback, thisArg = undefined) {
            state(this); argumentsRequired(arguments.length, 1);
            if (typeof callback !== 'function') throw new TypeError('Callback must be callable');
            for (const [key, value] of iterator(this, 'entry')) apply(callback, thisArg, [value, key, this]);
        }
        _flat() { return flat(this); }
    }
    define(Headers.prototype, Symbol.iterator, { value: Headers.prototype.entries, writable: true, configurable: true });
    define(Headers.prototype, Symbol.toStringTag, { value: 'Headers', configurable: true });
    for (const key of Object.getOwnPropertyNames(Headers.prototype)) if (key !== 'constructor' && key !== '_flat') {
        define(Headers.prototype, key, { ...descriptor(Headers.prototype, key), enumerable: true });
    }
    function flat(target) {
        const out = [];
        for (const [key, values] of mapEntries(state(target).list)) for (const value of values) { push(out, key); push(out, value); }
        return out;
    }
    function create(init, guard) {
        const result = new Headers(); state(result).guard = guard; fill(result, init); return result;
    }
    function clone(source, guard = undefined) {
        const current = state(source), result = new Headers(), copied = state(result);
        copied.guard = guard === undefined ? current.guard : guard;
        for (const [key, values] of mapEntries(current.list)) {
            const list = []; for (const value of values) push(list, value); mapSet(copied.list, key, list);
        }
        return result;
    }
    return { Headers, create, clone, fill, flat, guard: target => state(target).guard,
        setGuard(target, guard) { state(target).guard = guard; } };
}
