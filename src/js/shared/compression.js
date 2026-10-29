/* CompressionStream and DecompressionStream (Compression Streams), shared by the page and worker preludes: transform
   streams over the ZN.zNew and ZN.zPush natives (compressjs.cc) for "gzip", "deflate" and "deflate-raw". Each chunk is
   copied into zlib as it is written, so a buffer detached afterwards does not matter. */
const zIsShared = b => Object.prototype.toString.call(b) === '[object SharedArrayBuffer]';
function zStream(name, format, compress) {
    if (format === undefined) throw new TypeError(`Failed to construct '${name}': 1 argument required, but only 0 present.`);
    const f = `${format}`;
    const z = ZN.zNew(f, compress);
    if (!z) throw new TypeError(`Failed to construct '${name}': Unsupported compression format: '${f}'`);
    const push = (bytes, k) => {
        const [out, err] = ZN.zPush(z, bytes);
        if (out.byteLength) k.enqueue(out);
        if (err) throw new TypeError(err);
    };
    return new TransformStream({
        transform(chunk, k) {
            const ok = ArrayBuffer.isView(chunk) ? !zIsShared(chunk.buffer) : chunk instanceof ArrayBuffer;
            if (!ok) throw new TypeError("The provided value is not of type '(ArrayBuffer or ArrayBufferView)'");
            push(chunk, k);
        },
        flush(k) { push(null, k); },
    });
}
class CompressionStream {
    #ts;
    constructor(format) { this.#ts = zStream('CompressionStream', arguments.length ? format : undefined, true); }
    get readable() { return this.#ts.readable; }
    get writable() { return this.#ts.writable; }
}
class DecompressionStream {
    #ts;
    constructor(format) { this.#ts = zStream('DecompressionStream', arguments.length ? format : undefined, false); }
    get readable() { return this.#ts.readable; }
    get writable() { return this.#ts.writable; }
}
for (const C of [CompressionStream, DecompressionStream]) Object.defineProperty(C.prototype, Symbol.toStringTag, { value: C.name, configurable: true });
