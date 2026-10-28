/* Encoding Standard interfaces shared by window and dedicated workers.
 * Label recognition and legacy indexes come from the native immutable tables.
 * Normal UTF-8/UTF-16 streaming retains only the unfinished scalar's state.
 */
function createTextCodecInterfaces(privateState, codec) {
    const U8 = Uint8Array, U16 = Uint16Array, AB = ArrayBuffer;
    const apply = Reflect.apply, isView = AB.isView;
    const typed = Object.getPrototypeOf(U8.prototype);
    const getter = (p, k) => Object.getOwnPropertyDescriptor(p, k).get;
    const tagGet = getter(typed, Symbol.toStringTag), lengthGet = getter(typed, 'length');
    const bufferGet = getter(typed, 'buffer'), offsetGet = getter(typed, 'byteOffset'), byteLengthGet = getter(typed, 'byteLength');
    const dataBufferGet = getter(DataView.prototype, 'buffer'), dataOffsetGet = getter(DataView.prototype, 'byteOffset'), dataLengthGet = getter(DataView.prototype, 'byteLength');
    const abLengthGet = getter(AB.prototype, 'byteLength');
    const sabLengthGet = typeof SharedArrayBuffer === 'function' ? getter(SharedArrayBuffer.prototype, 'byteLength') : null;
    const abResizableGet = Object.getOwnPropertyDescriptor(AB.prototype, 'resizable')?.get;
    const sabGrowableGet = typeof SharedArrayBuffer === 'function' ? Object.getOwnPropertyDescriptor(SharedArrayBuffer.prototype, 'growable')?.get : null;
    const charCode = Function.prototype.call.bind(String.prototype.charCodeAt);
    const fromCharCode = String.fromCharCode;
    const localEncoder = new WeakMap(), localDecoder = new WeakMap();
    const local = kind => kind === 7 ? localEncoder : localDecoder;
    const get = privateState ? (kind, value) => privateState.get(kind, value) : (kind, value) => local(kind).get(value);
    const put = privateState ? (kind, value, state) => privateState.put(kind, value, state) : (kind, value, state) => local(kind).set(value, state);
    const stateOf = (kind, value) => {
        const state = get(kind, value);
        if (state === undefined) throw new TypeError('Invalid TextEncoder or TextDecoder receiver');
        return state;
    };
    const string = value => {
        if (typeof value === 'symbol') throw new TypeError('Cannot convert a Symbol to a string');
        return String(value);
    };
    const dictionary = value => {
        if (value == null) return {};
        if (typeof value !== 'object' && typeof value !== 'function') throw new TypeError('Options must be a dictionary');
        return value;
    };
    const bufferLength = value => {
        try { return apply(abLengthGet, value, []); } catch (_) {}
        if (sabLengthGet) {
            try { return apply(sabLengthGet, value, []); } catch (_) {}
        }
        throw new TypeError('Input must be an ArrayBuffer or ArrayBufferView');
    };
    const fixedBuffer = buffer => {
        let resizable = false, growable = false;
        if (abResizableGet) { try { resizable = apply(abResizableGet, buffer, []); } catch (_) {} }
        if (sabGrowableGet) { try { growable = apply(sabGrowableGet, buffer, []); } catch (_) {} }
        if (resizable || growable) throw new TypeError('Resizable buffers are not allowed by this interface');
    };
    const checkInput = input => {
        if (input === undefined) return;
        if (!isView(input)) { bufferLength(input); fixedBuffer(input); return; }
        const isTyped = apply(tagGet, input, []) !== undefined;
        fixedBuffer(apply(isTyped ? bufferGet : dataBufferGet, input, []));
    };
    const copyInput = input => {
        if (input === undefined) return new U8(0);
        let buffer, offset = 0, length;
        if (isView(input)) {
            const isTyped = apply(tagGet, input, []) !== undefined;
            buffer = apply(isTyped ? bufferGet : dataBufferGet, input, []);
            try {
                offset = apply(isTyped ? offsetGet : dataOffsetGet, input, []);
                length = apply(isTyped ? byteLengthGet : dataLengthGet, input, []);
            } catch (error) {
                // Detached DataView accessors throw; a detached input is empty.
                if (bufferLength(buffer) !== 0) throw error;
                return new U8(0);
            }
        } else { buffer = input; length = bufferLength(buffer); }
        if (!length) return new U8(0);
        // V8 copies shared backing bytes; no racy native pointer is exposed.
        return new U8(new U8(buffer, offset, length));
    };
    const scalar = (source, index) => {
        let cp = charCode(source, index);
        if (cp >= 0xD800 && cp <= 0xDBFF && index + 1 < source.length) {
            const low = charCode(source, index + 1);
            if (low >= 0xDC00 && low <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + low - 0xDC00; }
        }
        if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
        return cp;
    };
    function encodeIntoBytes(source, destination, capacity) {
        let read = 0, written = 0;
        while (read < source.length) {
            const cp = scalar(source, read), size = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
            if (capacity - written < size) break;
            if (size === 1) destination[written++] = cp;
            else if (size === 2) {
                destination[written++] = 0xC0 | (cp >> 6); destination[written++] = 0x80 | (cp & 63);
            } else if (size === 3) {
                destination[written++] = 0xE0 | (cp >> 12); destination[written++] = 0x80 | ((cp >> 6) & 63); destination[written++] = 0x80 | (cp & 63);
            } else {
                destination[written++] = 0xF0 | (cp >> 18); destination[written++] = 0x80 | ((cp >> 12) & 63);
                destination[written++] = 0x80 | ((cp >> 6) & 63); destination[written++] = 0x80 | (cp & 63);
            }
            read += cp > 0xFFFF ? 2 : 1;
        }
        return { read, written };
    }
    class TextEncoder {
        constructor() { put(7, this, { makeBytes: length => new U8(length) }); }
        get encoding() { stateOf(7, this); return 'utf-8'; }
        encode(input = '') {
            const state = stateOf(7, this), source = string(input);
            let size = 0;
            for (let i = 0; i < source.length;) { const cp = scalar(source, i); size += cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4; i += cp > 0xFFFF ? 2 : 1; }
            const bytes = state.makeBytes(size);
            encodeIntoBytes(source, bytes, size);
            return bytes;
        }
        encodeInto(source, destination) {
            stateOf(7, this);
            if (arguments.length < 2) throw new TypeError('encodeInto requires source and destination');
            source = string(source);
            if (apply(tagGet, destination, []) !== 'Uint8Array') throw new TypeError('Destination must be a Uint8Array');
            fixedBuffer(apply(bufferGet, destination, []));
            // Detached Uint8Arrays have zero capacity and remain valid targets.
            return encodeIntoBytes(source, destination, apply(lengthGet, destination, []));
        }
    }
    const reset = state => {
        state.cp = state.need = state.seen = 0; state.low = 0x80; state.high = 0xBF;
        state.byte = state.surrogate = null; state.bomSeen = false; state.queue = new U8(0);
    };
    const serialize = (state, output) => {
        if (!state.ignoreBOM && !state.bomSeen && output.length &&
            (state.encoding === 'utf-8' || state.encoding === 'utf-16le' || state.encoding === 'utf-16be')) {
            state.bomSeen = true;
            if (charCode(output, 0) === 0xFEFF) return output.slice(1);
        }
        return output;
    };
    function decodeUnicode(state, bytes, stream) {
        if (state.queue.length) {
            const combined = new U8(state.queue.length + bytes.length);
            combined.set(state.queue); combined.set(bytes, state.queue.length); bytes = combined;
            state.queue = new U8(0);
        }
        const parts = [], units = new U16(4096); let used = 0, pos = 0;
        const flush = () => { if (used) { parts.push(apply(fromCharCode, null, new U16(units.buffer, 0, used))); used = 0; } };
        const emit = cp => {
            if (used > units.length - 2) flush();
            if (cp <= 0xFFFF) units[used++] = cp;
            else { cp -= 0x10000; units[used++] = 0xD800 + (cp >> 10); units[used++] = 0xDC00 + (cp & 1023); }
        };
        const invalid = (reconsume = null) => {
            if (!state.fatal) { emit(0xFFFD); return; }
            const prefix = reconsume || [], remaining = bytes.length - pos;
            // Only a fatal call's unconsumed queue survives; successful calls
            // retain no input chunks, just the unfinished scalar's small state.
            state.queue = new U8(prefix.length + remaining);
            state.queue.set(prefix); state.queue.set(bytes.subarray(pos), prefix.length);
            throw new TypeError('The encoded data is not valid for ' + state.encoding);
        };
        if (state.encoding === 'utf-8') {
            while (pos < bytes.length) {
                const b = bytes[pos++];
                if (!state.need) {
                    if (b < 0x80) { emit(b); continue; }
                    if (b >= 0xC2 && b <= 0xDF) { state.need = 1; state.cp = b & 31; }
                    else if (b >= 0xE0 && b <= 0xEF) {
                        state.need = 2; state.cp = b & 15;
                        if (b === 0xE0) state.low = 0xA0;
                        if (b === 0xED) state.high = 0x9F;
                    } else if (b >= 0xF0 && b <= 0xF4) {
                        state.need = 3; state.cp = b & 7;
                        if (b === 0xF0) state.low = 0x90;
                        if (b === 0xF4) state.high = 0x8F;
                    } else invalid();
                    continue;
                }
                if (b < state.low || b > state.high) {
                    state.cp = state.need = state.seen = 0; state.low = 0x80; state.high = 0xBF;
                    --pos; invalid(); continue;
                }
                state.low = 0x80; state.high = 0xBF; state.cp = (state.cp << 6) | (b & 63); ++state.seen;
                if (state.seen === state.need) { emit(state.cp); state.cp = state.need = state.seen = 0; }
            }
            if (!stream && state.need) { state.cp = state.need = state.seen = 0; state.low = 0x80; state.high = 0xBF; invalid(); }
        } else {
            const big = state.encoding === 'utf-16be';
            while (pos < bytes.length) {
                const b = bytes[pos++];
                if (state.byte === null) { state.byte = b; continue; }
                const first = state.byte, unit = big ? (first << 8) | b : (b << 8) | first; state.byte = null;
                if (state.surrogate !== null) {
                    const high = state.surrogate; state.surrogate = null;
                    if (unit >= 0xDC00 && unit <= 0xDFFF) { emit(0x10000 + ((high - 0xD800) << 10) + unit - 0xDC00); continue; }
                    invalid([first, b]);
                }
                if (unit >= 0xD800 && unit <= 0xDBFF) state.surrogate = unit;
                else if (unit >= 0xDC00 && unit <= 0xDFFF) invalid();
                else emit(unit);
            }
            if (!stream && (state.byte !== null || state.surrogate !== null)) { state.byte = state.surrogate = null; invalid(); }
        }
        flush(); return serialize(state, parts.join(''));
    }
    const multibyteLegacy = new Set(['gbk', 'gb18030', 'big5', 'euc-jp', 'iso-2022-jp', 'shift_jis', 'euc-kr']);
    class TextDecoder {
        constructor(label = 'utf-8', options = {}) {
            label = string(label); options = dictionary(options);
            const fatal = !!options.fatal, ignoreBOM = !!options.ignoreBOM;
            const encoding = codec.label(label);
            if (encoding === null) throw new RangeError('The encoding label is invalid or unsupported: ' + label);
            const state = { encoding, fatal, ignoreBOM, streaming: false }; reset(state); put(8, this, state);
        }
        get encoding() { return stateOf(8, this).encoding; }
        get fatal() { return stateOf(8, this).fatal; }
        get ignoreBOM() { return stateOf(8, this).ignoreBOM; }
        decode(input = undefined, options = {}) {
            const state = stateOf(8, this);
            checkInput(input); options = dictionary(options); const stream = !!options.stream;
            const bytes = copyInput(input);
            const unicode = state.encoding === 'utf-8' || state.encoding === 'utf-16le' || state.encoding === 'utf-16be';
            if (stream && !unicode && (multibyteLegacy.has(state.encoding) || state.fatal))
                throw new TypeError('Streaming this legacy encoding/error mode is not implemented');
            if (!state.streaming) reset(state);
            const wasStreaming = state.streaming; state.streaming = stream;
            if (unicode && (stream || wasStreaming)) return decodeUnicode(state, bytes, stream);
            return serialize(state, codec.decode(bytes.buffer, state.encoding, state.fatal));
        }
    }
    for (const [Ctor, name] of [[TextEncoder, 'TextEncoder'], [TextDecoder, 'TextDecoder']]) {
        Object.defineProperty(Ctor.prototype, Symbol.toStringTag, { value: name, configurable: true });
        for (const key of Object.getOwnPropertyNames(Ctor.prototype)) {
            if (key === 'constructor') continue;
            const desc = Object.getOwnPropertyDescriptor(Ctor.prototype, key); desc.enumerable = true;
            Object.defineProperty(Ctor.prototype, key, desc);
        }
    }
    return { TextEncoder, TextDecoder };
}
