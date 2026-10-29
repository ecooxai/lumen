/* TextEncoder, TextDecoder and their streams per the Encoding standard, shared by the page and worker preludes. The
   decoders run in C (ENCN.decInit / ENCN.decChunk keep a decoder's state in a small Uint8Array). */
const encDict = (o, what) => {
    if (o == null) return {};
    if (typeof o !== 'object' && typeof o !== 'function') throw new TypeError(`Failed to execute '${what}': The provided value is not of type '${what === 'decode' ? 'TextDecodeOptions' : 'TextDecoderOptions'}'.`);
    return o;
};
class TextEncoder {
    #brand = true;
    get encoding() { if (!(#brand in this)) throw new TypeError('Illegal invocation'); return 'utf-8'; }
    encode(input = '') { if (!(#brand in this)) throw new TypeError('Illegal invocation'); return ENCN.encUtf8(`${input}`); }
    encodeInto(source, destination) {
        if (!(#brand in this)) throw new TypeError('Illegal invocation');
        if (arguments.length < 2) throw new TypeError(`Failed to execute 'encodeInto' on 'TextEncoder': 2 arguments required, but only ${arguments.length} present.`);
        source = `${source}`;
        if (!(destination instanceof Uint8Array)) throw new TypeError("Failed to execute 'encodeInto' on 'TextEncoder': parameter 2 is not of type 'Uint8Array'.");
        const r = ENCN.encInto(source, destination);
        return { read: r[0], written: r[1] };
    }
}
class TextDecoder {
    #enc; #fatal; #ignoreBOM; #st = null; #streaming = false;
    constructor(label = 'utf-8', options = undefined) {
        label = `${label}`;
        const o = encDict(options, 'TextDecoder'), fatal = !!o.fatal, ignoreBOM = !!o.ignoreBOM;
        const name = ENCN.encLabel(label);
        if (name === null || name === 'replacement') throw new RangeError(`Failed to construct 'TextDecoder': The encoding label provided ('${label}') is invalid.`);
        this.#enc = name; this.#fatal = fatal; this.#ignoreBOM = ignoreBOM;
    }
    get encoding() { return this.#enc.toLowerCase(); }
    get fatal() { return this.#fatal; }
    get ignoreBOM() { return this.#ignoreBOM; }
    decode(input = undefined, options = undefined) {
        const enc = this.#enc;
        if (input !== undefined && input !== null && !ENCN.isBufferSource(input)) throw new TypeError("Failed to execute 'decode' on 'TextDecoder': The provided value is not of type '(ArrayBuffer or ArrayBufferView)'.");
        const stream = !!encDict(options, 'decode').stream;
        if (!this.#streaming || !this.#st) this.#st = ENCN.decInit(enc, this.#fatal, !this.#ignoreBOM);
        this.#streaming = stream;
        try { return ENCN.decChunk(this.#st, input ?? null, !stream); }
        catch (e) { this.#streaming = false; throw e; }
    }
}
class TextEncoderStream {
    #ts;
    constructor() {
        let pending = '';
        const enc = new TextEncoder();
        this.#ts = new TransformStream({
            transform(chunk, k) {
                let s = pending + `${chunk}`;
                pending = '';
                const last = s.charCodeAt(s.length - 1);
                if (last >= 0xD800 && last <= 0xDBFF) { pending = s[s.length - 1]; s = s.slice(0, -1); }
                if (s) k.enqueue(enc.encode(s));
            },
            flush(k) { if (pending) k.enqueue(new Uint8Array([0xEF, 0xBF, 0xBD])); },
        });
    }
    get encoding() { return 'utf-8'; }
    get readable() { return this.#ts.readable; }
    get writable() { return this.#ts.writable; }
}
class TextDecoderStream {
    #ts; #d;
    constructor(label = 'utf-8', options = undefined) {
        const d = this.#d = new TextDecoder(label, options);
        this.#ts = new TransformStream({
            transform(chunk, k) {
                if (!ENCN.isBufferSource(chunk)) throw new TypeError("Failed to execute 'decode' on 'TextDecoder': The provided value is not of type '(ArrayBuffer or ArrayBufferView)'.");
                const s = d.decode(chunk, { stream: true });
                if (s) k.enqueue(s);
            },
            flush(k) { const s = d.decode(); if (s) k.enqueue(s); },
        });
    }
    get encoding() { return this.#d.encoding; }
    get fatal() { return this.#d.fatal; }
    get ignoreBOM() { return this.#d.ignoreBOM; }
    get readable() { return this.#ts.readable; }
    get writable() { return this.#ts.writable; }
}
for (const C of [TextEncoder, TextDecoder, TextEncoderStream, TextDecoderStream])
    Object.defineProperty(C.prototype, Symbol.toStringTag, { value: C.name, configurable: true });
