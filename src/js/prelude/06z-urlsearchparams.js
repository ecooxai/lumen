// One implementation is included in the window and dedicated-worker preludes.
// The factory and its operations stay inside those preludes, never on globalThis.
function createURLSearchParams(nativeIDL) {
    const apply = Reflect.apply, ownKeys = Reflect.ownKeys;
    const descriptor = Object.getOwnPropertyDescriptor, define = Object.defineProperty;
    const create = Object.create, prototypeOf = Object.getPrototypeOf;
    const StringValue = String, scalarString = String.prototype.toWellFormed;
    const TypeErrorIntrinsic = TypeError;
    const codeAt = String.prototype.charCodeAt, fromCodePoint = String.fromCodePoint;
    const weakGet = WeakMap.prototype.get, weakSet = WeakMap.prototype.set;
    const mapGet = Map.prototype.get, mapSet = Map.prototype.set, arraySort = Array.prototype.sort;
    const ByteArray = Uint8Array, byteSubarray = Uint8Array.prototype.subarray;
    const iteratorSlots = new WeakMap();
    const getState = nativeIDL.get, putState = nativeIDL.put;
    const kind = 3;
    const isObject = value => value !== null && (typeof value === 'object' || typeof value === 'function');
    const getSlot = (map, key) => apply(weakGet, map, [key]);
    const putSlot = (map, key, value) => apply(weakSet, map, [key, value]);
    const charCode = (value, index) => apply(codeAt, value, [index]);

    function usv(value) {
        if (typeof value === 'symbol') throw new TypeErrorIntrinsic('Cannot convert a Symbol value to a string');
        return apply(scalarString, StringValue(value), []);
    }
    function state(object) {
        const value = getState(kind, object);
        if (!value) throw new TypeErrorIntrinsic('Illegal invocation');
        return value;
    }
    function required(count, minimum) {
        if (count < minimum) throw new TypeErrorIntrinsic('Not enough arguments');
    }
    function method(object, key) {
        const value = object[key];
        if (value == null) return undefined;
        if (typeof value !== 'function') throw new TypeErrorIntrinsic('Iterator method is not callable');
        return value;
    }
    function sequence(object, iteratorMethod, convert) {
        const iterator = apply(iteratorMethod, object, []);
        if (!isObject(iterator)) throw new TypeErrorIntrinsic('Iterator is not an object');
        const next = iterator.next, values = [];
        for (;;) {
            const step = apply(next, iterator, []);
            if (!isObject(step)) throw new TypeErrorIntrinsic('Iterator result is not an object');
            if (step.done) return values;
            // Web IDL sequence conversion propagates conversion failures. It
            // does not use JavaScript destructuring or validate pair sizes yet.
            values[values.length] = convert(step.value);
        }
    }
    function pairSequence(value) {
        if (!isObject(value)) throw new TypeErrorIntrinsic('Expected a sequence object');
        const iteratorMethod = method(value, Symbol.iterator);
        if (!iteratorMethod) throw new TypeErrorIntrinsic('Expected an iterable sequence');
        return sequence(value, iteratorMethod, usv);
    }

    function utf8Bytes(value) {
        let length = 0;
        for (let index = 0; index < value.length; index++) {
            const point = charCode(value, index);
            if (point < 0x80) length++;
            else if (point < 0x800) length += 2;
            else if (point >= 0xd800 && point <= 0xdbff &&
                     charCode(value, index + 1) >= 0xdc00 && charCode(value, index + 1) <= 0xdfff) {
                length += 4; index++;
            } else length += 3;
        }
        const bytes = new ByteArray(length);
        let offset = 0;
        for (let index = 0; index < value.length; index++) {
            let point = charCode(value, index);
            if (point >= 0xd800 && point <= 0xdbff) {
                const low = charCode(value, index + 1);
                if (low >= 0xdc00 && low <= 0xdfff) {
                    point = 0x10000 + ((point - 0xd800) << 10) + low - 0xdc00;
                    index++;
                } else point = 0xfffd;
            } else if (point >= 0xdc00 && point <= 0xdfff) point = 0xfffd;
            if (point < 0x80) bytes[offset++] = point;
            else if (point < 0x800) {
                bytes[offset++] = 0xc0 | (point >> 6);
                bytes[offset++] = 0x80 | (point & 63);
            } else if (point < 0x10000) {
                bytes[offset++] = 0xe0 | (point >> 12);
                bytes[offset++] = 0x80 | ((point >> 6) & 63);
                bytes[offset++] = 0x80 | (point & 63);
            } else {
                bytes[offset++] = 0xf0 | (point >> 18);
                bytes[offset++] = 0x80 | ((point >> 12) & 63);
                bytes[offset++] = 0x80 | ((point >> 6) & 63);
                bytes[offset++] = 0x80 | (point & 63);
            }
        }
        return bytes;
    }
    function utf8Decode(bytes) {
        let output = '', needed = 0, seen = 0, point = 0, lower = 0x80, upper = 0xbf;
        for (let index = 0; index < bytes.length; index++) {
            const byte = bytes[index];
            if (!needed) {
                if (byte <= 0x7f) output += fromCodePoint(byte);
                else if (byte >= 0xc2 && byte <= 0xdf) { needed = 1; point = byte & 31; }
                else if (byte >= 0xe0 && byte <= 0xef) {
                    needed = 2; point = byte & 15;
                    if (byte === 0xe0) lower = 0xa0;
                    if (byte === 0xed) upper = 0x9f;
                } else if (byte >= 0xf0 && byte <= 0xf4) {
                    needed = 3; point = byte & 7;
                    if (byte === 0xf0) lower = 0x90;
                    if (byte === 0xf4) upper = 0x8f;
                } else output += '\ufffd';
            } else if (byte < lower || byte > upper) {
                output += '\ufffd';
                needed = seen = point = 0; lower = 0x80; upper = 0xbf;
                index--; // Reconsume the offending byte as the next lead byte.
            } else {
                lower = 0x80; upper = 0xbf;
                point = (point << 6) | (byte & 63);
                if (++seen === needed) {
                    output += fromCodePoint(point);
                    needed = seen = point = 0;
                }
            }
        }
        if (needed) output += '\ufffd';
        // This is UTF-8 decode without BOM. U+FEFF is query data, not a marker.
        return output;
    }
    const hex = byte => byte >= 48 && byte <= 57 ? byte - 48 :
        byte >= 65 && byte <= 70 ? byte - 55 : byte >= 97 && byte <= 102 ? byte - 87 : -1;
    function decodePart(bytes, start, end) {
        const decoded = new ByteArray(end - start);
        let offset = 0;
        for (let index = start; index < end; index++) {
            let byte = bytes[index];
            if (byte === 43) byte = 32;
            else if (byte === 37 && index + 2 < end) {
                const high = hex(bytes[index + 1]), low = hex(bytes[index + 2]);
                if (high >= 0 && low >= 0) { byte = high * 16 + low; index += 2; }
            }
            decoded[offset++] = byte;
        }
        return utf8Decode(apply(byteSubarray, decoded, [0, offset]));
    }
    function parseBytes(bytes) {
        const list = [];
        let start = 0;
        for (let end = 0; end <= bytes.length; end++) {
            if (end !== bytes.length && bytes[end] !== 38) continue;
            if (end > start) {
                let equals = start;
                while (equals < end && bytes[equals] !== 61) equals++;
                list[list.length] = [decodePart(bytes, start, equals),
                    decodePart(bytes, equals < end ? equals + 1 : end, end)];
            }
            start = end + 1;
        }
        return list;
    }
    function parseString(value, stripQuestion = false) {
        const bytes = utf8Bytes(value);
        return parseBytes(stripQuestion && bytes[0] === 63 ? apply(byteSubarray, bytes, [1]) : bytes);
    }
    const digits = '0123456789ABCDEF';
    function encodePart(value) {
        const bytes = utf8Bytes(value);
        let output = '';
        for (let index = 0; index < bytes.length; index++) {
            const byte = bytes[index];
            if (byte === 32) output += '+';
            else if ((byte >= 65 && byte <= 90) || (byte >= 97 && byte <= 122) ||
                     (byte >= 48 && byte <= 57) || byte === 42 || byte === 45 || byte === 46 || byte === 95) {
                output += fromCodePoint(byte);
            } else output += '%' + digits[byte >> 4] + digits[byte & 15];
        }
        return output;
    }
    function serialize(list) {
        let value = '';
        for (let index = 0; index < list.length; index++) {
            if (index) value += '&';
            value += encodePart(list[index][0]) + '=' + encodePart(list[index][1]);
        }
        return value;
    }
    function update(value) {
        if (value.onChange) value.onChange(serialize(value.list));
    }

    const iteratorPrototype = create(prototypeOf(prototypeOf([][Symbol.iterator]())));
    define(iteratorPrototype, 'next', { value: { next() {
        const iterator = getSlot(iteratorSlots, this);
        if (!iterator) throw new TypeErrorIntrinsic('Illegal invocation');
        const list = state(iterator.target).list;
        if (iterator.index >= list.length) return { value: undefined, done: true };
        const pair = list[iterator.index++];
        return { value: iterator.kind === 0 ? pair[0] : iterator.kind === 1 ? pair[1] : [pair[0], pair[1]], done: false };
    } }.next, writable: true, configurable: true, enumerable: true });
    define(iteratorPrototype, Symbol.toStringTag, { value: 'URLSearchParams Iterator', configurable: true });
    function iterator(target, kind) {
        state(target);
        const value = create(iteratorPrototype);
        putSlot(iteratorSlots, value, { target, kind, index: 0 });
        return value;
    }

    class URLSearchParams {
        constructor(init = '') {
            let list;
            if (isObject(init)) {
                const iteratorMethod = method(init, Symbol.iterator);
                if (iteratorMethod !== undefined) {
                    list = sequence(init, iteratorMethod, pairSequence);
                    // Union/sequence conversion finishes before the URL
                    // constructor's pair-size validation begins.
                    for (let index = 0; index < list.length; index++) {
                        if (list[index].length !== 2) throw new TypeErrorIntrinsic('Each query pair must contain exactly two values');
                    }
                } else {
                    list = [];
                    const keys = ownKeys(init), positions = new Map();
                    for (let index = 0; index < keys.length; index++) {
                        const key = keys[index], own = descriptor(init, key);
                        if (!own || !own.enumerable) continue;
                        const name = usv(key), value = usv(init[key]);
                        const prior = apply(mapGet, positions, [name]);
                        if (prior === undefined) {
                            apply(mapSet, positions, [name, list.length]);
                            list[list.length] = [name, value];
                        } else list[prior][1] = value;
                    }
                }
            } else list = parseString(usv(init), true);
            putState(kind, this, { list, onChange: null });
        }
        get size() { return state(this).list.length; }
        append(name, value) {
            const current = state(this); required(arguments.length, 2);
            name = usv(name); value = usv(value);
            current.list[current.list.length] = [name, value]; update(current);
        }
        delete(name, value = undefined) {
            const current = state(this); required(arguments.length, 1);
            name = usv(name); if (value !== undefined) value = usv(value);
            const list = current.list, kept = [];
            for (let index = 0; index < list.length; index++) {
                const pair = list[index];
                if (pair[0] !== name || (value !== undefined && pair[1] !== value)) kept[kept.length] = pair;
            }
            current.list = kept; update(current);
        }
        get(name) {
            const current = state(this); required(arguments.length, 1); name = usv(name);
            for (let index = 0; index < current.list.length; index++) {
                if (current.list[index][0] === name) return current.list[index][1];
            }
            return null;
        }
        getAll(name) {
            const current = state(this); required(arguments.length, 1); name = usv(name);
            const values = [];
            for (let index = 0; index < current.list.length; index++) {
                if (current.list[index][0] === name) values[values.length] = current.list[index][1];
            }
            return values;
        }
        has(name, value = undefined) {
            const current = state(this); required(arguments.length, 1);
            name = usv(name); if (value !== undefined) value = usv(value);
            for (let index = 0; index < current.list.length; index++) {
                const pair = current.list[index];
                if (pair[0] === name && (value === undefined || pair[1] === value)) return true;
            }
            return false;
        }
        set(name, value) {
            const current = state(this); required(arguments.length, 2);
            name = usv(name); value = usv(value);
            const kept = [];
            let found = false;
            for (let index = 0; index < current.list.length; index++) {
                const pair = current.list[index];
                if (pair[0] !== name) kept[kept.length] = pair;
                else if (!found) { kept[kept.length] = [name, value]; found = true; }
            }
            if (!found) kept[kept.length] = [name, value];
            current.list = kept; update(current);
        }
        sort() {
            const current = state(this);
            apply(arraySort, current.list, [(left, right) => left[0] < right[0] ? -1 : left[0] > right[0] ? 1 : 0]);
            update(current);
        }
        forEach(callback, thisArg = undefined) {
            const current = state(this); required(arguments.length, 1);
            if (typeof callback !== 'function') throw new TypeErrorIntrinsic('Callback is not callable');
            for (let index = 0; index < current.list.length; index++) {
                const pair = current.list[index];
                apply(callback, thisArg, [pair[1], pair[0], this]);
            }
        }
        keys() { return iterator(this, 0); }
        values() { return iterator(this, 1); }
        entries() { return iterator(this, 2); }
        toString() { return serialize(state(this).list); }
    }
    for (const name of ['size', 'append', 'delete', 'get', 'getAll', 'has', 'set', 'sort', 'forEach', 'keys', 'values', 'entries', 'toString']) {
        define(URLSearchParams.prototype, name, { ...descriptor(URLSearchParams.prototype, name), enumerable: true });
    }
    define(URLSearchParams.prototype, Symbol.iterator, {
        value: URLSearchParams.prototype.entries, writable: true, configurable: true,
    });
    define(URLSearchParams.prototype, Symbol.toStringTag, { value: 'URLSearchParams', configurable: true });
    nativeIDL.register(kind, URLSearchParams.prototype);
    return {
        URLSearchParams, usv, utf8Bytes, utf8Decode, parseBytes, parseString, serialize,
        isURLSearchParams: value => getState(kind, value) !== undefined,
        serializeParams: value => serialize(state(value).list),
        replace(value, search) { state(value).list = parseString(search, true); },
        link(value, callback) { state(value).onChange = callback; },
    };
}
