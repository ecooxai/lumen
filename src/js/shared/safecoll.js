/* Map and Set whose methods are captured at startup, so pages that patch Map.prototype/Set.prototype cannot break the prelude */
const OMap = Map, OSet = Set;   /* the real constructors, for objects handed to pages */
const [SMap, SSet] = [Map, Set].map(B => {
    const P = B.prototype, C = class extends B {}, size = Object.getOwnPropertyDescriptor(P, 'size').get;
    for (const k of [...Object.getOwnPropertyNames(P), Symbol.iterator]) {
        if (k === 'constructor' || k === 'size') continue;
        const f = P[k];
        if (typeof f === 'function') Object.defineProperty(C.prototype, k, { value: { [k.toString()](...a) { return f.apply(this, a); } }[k.toString()], writable: true, configurable: true });
    }
    Object.defineProperty(C.prototype, 'size', { get() { return size.call(this); }, configurable: true });
    return C;
});
