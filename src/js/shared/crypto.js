/* Web Cryptography API (with the modern-algorithms additions Chrome ships), shared by the page and worker preludes.
   The includer defines CN (the natives from cryptojs.cc) and CRYPTO_SECURE (a function: is this a secure context?).
   Operations run synchronously and settle their promise afterwards; arguments are converted and copied first, so the
   outcome is the same as the spec's "in parallel" steps. Asymmetric key material is held as DER (PKCS#8 / SPKI). */
const { Crypto, CryptoKey, SubtleCrypto, crypto, cryptoKeyClone } = (() => {
const err = (name, msg) => new DOMException(msg, name);
const opError = () => err('OperationError', 'The operation failed for an operation-specific reason');
const dataError = (m = 'The provided data is not a valid key') => err('DataError', m);
const accessError = (m = 'The requested operation is not valid for the provided key') => err('InvalidAccessError', m);
const notSupported = (m = 'Algorithm: Unrecognized name') => err('NotSupportedError', m);
const usageError = () => err('SyntaxError', 'Cannot create a key using the specified key usages.');
const must = r => { if (r === undefined || r === null) throw opError(); return r; };
const isObj = v => v !== null && (typeof v === 'object' || typeof v === 'function');
const lowerAscii = s => s.replace(/[A-Z]/g, c => String.fromCharCode(c.charCodeAt(0) + 32));

/* ---- WebIDL conversions ---- */
const isShared = b => typeof SharedArrayBuffer === 'function' && b instanceof SharedArrayBuffer;
/* a BufferSource argument: a copy of its bytes as a Uint8Array */
const bytesOf = (v, what = 'BufferSource') => {
    if (v instanceof ArrayBuffer) return v.detached ? new Uint8Array(0) : new Uint8Array(v.slice(0));
    if (ArrayBuffer.isView(v) && !isShared(v.buffer)) return v.buffer.detached ? new Uint8Array(0) : new Uint8Array(v.buffer, v.byteOffset, v.byteLength).slice();
    throw new TypeError(`Failed to convert value to '${what}': not an ArrayBuffer or ArrayBufferView`);
};
const isBufferSource = v => v instanceof ArrayBuffer || (ArrayBuffer.isView(v) && !isShared(v.buffer));
const enforce = (v, max) => {
    const x = Number(v);
    if (!Number.isFinite(x)) throw new TypeError('Value is not a finite number');
    const t = Math.trunc(x);
    if (t < 0 || t > max) throw new TypeError(`Value is outside the range 0..${max}`);
    return t;
};
const USAGES = ['encrypt', 'decrypt', 'sign', 'verify', 'deriveKey', 'deriveBits', 'wrapKey', 'unwrapKey', 'encapsulateKey', 'encapsulateBits', 'decapsulateKey', 'decapsulateBits'];
const FORMATS = ['raw', 'spki', 'pkcs8', 'jwk', 'raw-public', 'raw-private', 'raw-seed', 'raw-secret'];
const enumOf = (v, list, what) => { const s = String(v); if (!list.includes(s)) throw new TypeError(`The provided value '${s}' is not a valid enum value of type ${what}.`); return s; };
const usagesOf = v => {
    if (!isObj(v) || typeof v[Symbol.iterator] !== 'function') throw new TypeError("Failed to convert value to 'sequence<KeyUsage>'");
    const out = [];
    for (const u of v) out.push(enumOf(u, USAGES, 'KeyUsage'));
    return out;
};
const argc = (args, n, what) => { if (args.length < n) throw new TypeError(`Failed to execute '${what}' on 'SubtleCrypto': ${n} arguments required, but only ${args.length} present.`); };
const JWK_STR = ['alg', 'crv', 'd', 'dp', 'dq', 'e', 'k', 'kty', 'n', 'p', 'priv', 'pub', 'q', 'qi', 'use', 'x', 'y'];
/* JsonWebKey dictionary conversion (members in lexicographic order) */
const jwkDict = v => {
    if (v !== undefined && v !== null && !isObj(v)) throw new TypeError("Failed to convert value to 'JsonWebKey'");
    const o = {};
    if (v == null) return o;
    for (const k of ['alg', 'crv', 'd', 'dp', 'dq', 'e', 'ext', 'k', 'key_ops', 'kty', 'n', 'oth', 'p', 'priv', 'pub', 'q', 'qi', 'use', 'x', 'y']) {
        const x = v[k];
        if (x === undefined) continue;
        if (k === 'ext') o.ext = !!x;
        else if (k === 'key_ops') { if (!isObj(x) || typeof x[Symbol.iterator] !== 'function') throw new TypeError('key_ops is not a sequence'); o.key_ops = [...x].map(String); }
        else if (k === 'oth') { if (!isObj(x) || typeof x[Symbol.iterator] !== 'function') throw new TypeError('oth is not a sequence'); o.oth = [...x]; }
        else o[k] = String(x);
    }
    return o;
};

/* ---- algorithm registry and normalization ---- */
const SHA = ['SHA-1', 'SHA-256', 'SHA-384', 'SHA-512'];
const HASH_BITS = { 'SHA-1': 160, 'SHA-256': 256, 'SHA-384': 384, 'SHA-512': 512 };
const BLOCK_BITS = { 'SHA-1': 512, 'SHA-256': 512, 'SHA-384': 1024, 'SHA-512': 1024 };
const RSA = ['RSASSA-PKCS1-v1_5', 'RSA-PSS', 'RSA-OAEP'], EC = ['ECDSA', 'ECDH'], AES = ['AES-CTR', 'AES-CBC', 'AES-GCM', 'AES-KW'];
const MLDSA = ['ML-DSA-44', 'ML-DSA-65', 'ML-DSA-87'], MLKEM = ['ML-KEM-512', 'ML-KEM-768', 'ML-KEM-1024'];
const HYBRID = ['MLKEM768-P256', 'MLKEM768-X25519', 'MLKEM1024-P384'];   // hybrid KEMs; keys are a 32-byte seed and ek_PQ || ek_T
const CURVES = ['P-256', 'P-384', 'P-521'];
const CONV = {
    u32: v => enforce(v, 4294967295), u16: v => enforce(v, 65535), u8: v => enforce(v, 255), buf: v => bytesOf(v), str: v => String(v),
    big: v => { if (!(v instanceof Uint8Array) || isShared(v.buffer)) throw new TypeError("Failed to convert value to 'BigInteger'"); return v.slice(); },
    hash: v => normalize('digest', v),
    key: v => { if (!keys.has(v)) throw new TypeError("Failed to convert value to 'CryptoKey'"); return v; },
};
/* members per dictionary type, in WebIDL conversion order; [name, type, required] */
const D = {
    none: [],
    rsaGen: [['modulusLength', 'u32', 1], ['publicExponent', 'big', 1], ['hash', 'hash', 1]],
    hashed: [['hash', 'hash', 1]],
    ecKey: [['namedCurve', 'str', 1]],
    aesGen: [['length', 'u16', 1]],
    hmac: [['hash', 'hash', 1], ['length', 'u32', 0]],
    pss: [['saltLength', 'u32', 1]],
    ctx: [['context', 'buf', 0]],
    oaep: [['label', 'buf', 0]],
    ctr: [['counter', 'buf', 1], ['length', 'u8', 1]],
    cbc: [['iv', 'buf', 1]],
    aead: [['additionalData', 'buf', 0], ['iv', 'buf', 1], ['tagLength', 'u8', 0]],
    ecdh: [['public', 'key', 1]],
    hkdf: [['hash', 'hash', 1], ['info', 'buf', 1], ['salt', 'buf', 1]],
    pbkdf2: [['hash', 'hash', 1], ['iterations', 'u32', 1], ['salt', 'buf', 1]],
};
const REG = { digest: {}, generateKey: {}, importKey: {}, sign: {}, verify: {}, encrypt: {}, decrypt: {}, deriveBits: {}, getKeyLength: {}, wrapKey: {}, unwrapKey: {}, encapsulate: {}, decapsulate: {} };
const reg = (ops, names, dict) => { for (const op of ops.split(' ')) for (const n of names) REG[op][n] = D[dict]; };
reg('digest', SHA, 'none');
reg('generateKey', RSA, 'rsaGen'); reg('importKey', RSA, 'hashed');
reg('sign verify', ['RSASSA-PKCS1-v1_5'], 'none'); reg('sign verify', ['RSA-PSS'], 'pss'); reg('encrypt decrypt', ['RSA-OAEP'], 'oaep');
reg('generateKey importKey', EC, 'ecKey'); reg('sign verify', ['ECDSA'], 'hashed'); reg('deriveBits', ['ECDH', 'X25519'], 'ecdh');
reg('generateKey importKey sign verify', ['Ed25519'], 'none'); reg('generateKey importKey', ['X25519'], 'none');
reg('generateKey', AES, 'aesGen'); reg('importKey', AES, 'none'); reg('getKeyLength', AES, 'aesGen');
reg('encrypt decrypt', ['AES-CTR'], 'ctr'); reg('encrypt decrypt', ['AES-CBC'], 'cbc'); reg('encrypt decrypt', ['AES-GCM', 'ChaCha20-Poly1305'], 'aead');
reg('wrapKey unwrapKey', ['AES-KW'], 'none');
reg('generateKey importKey getKeyLength', ['HMAC'], 'hmac'); reg('sign verify', ['HMAC'], 'none');
reg('importKey getKeyLength', ['HKDF', 'PBKDF2'], 'none'); reg('deriveBits', ['HKDF'], 'hkdf'); reg('deriveBits', ['PBKDF2'], 'pbkdf2');
reg('generateKey importKey', MLDSA, 'none'); reg('sign verify', MLDSA, 'ctx');
reg('generateKey importKey encapsulate decapsulate', MLKEM, 'none'); reg('generateKey importKey encapsulate decapsulate', HYBRID, 'none');
reg('generateKey importKey getKeyLength', ['ChaCha20-Poly1305'], 'none');
function normalize(op, alg) {
    if (!isObj(alg)) return normalize(op, { name: String(alg) });
    const n = alg.name;
    if (n === undefined) throw new TypeError("Failed to normalize algorithm: 'name' is required");
    const want = lowerAscii(String(n)), table = REG[op];
    const name = Object.keys(table).find(k => lowerAscii(k) === want);
    if (!name) throw notSupported(`Algorithm: Unrecognized name ${String(n)}`);
    const out = { name };
    for (const [m, t, req] of table[name]) {
        const v = alg[m];
        if (v === undefined) { if (req) throw new TypeError(`Failed to normalize algorithm: '${m}' is required`); continue; }
        out[m] = CONV[t](v);
    }
    if (out.namedCurve !== undefined && !CURVES.includes(out.namedCurve)) throw notSupported(`Unsupported namedCurve ${out.namedCurve}`);
    return out;
}

/* ---- CryptoKey ---- */
const keys = new WeakMap();
const usageSet = l => USAGES.filter(u => l.includes(u));   // de-duplicated, canonical order
const cloneAlg = a => {
    const o = {};
    for (const k of Object.keys(a)) o[k] = k === 'hash' ? { name: a.hash.name } : k === 'publicExponent' ? new Uint8Array(a.publicExponent) : a[k];
    return o;
};
class CryptoKey {
    constructor() { throw new TypeError('Illegal constructor'); }
    get type() { return slot(this).type; }
    get extractable() { return slot(this).ext; }
    get algorithm() { const s = slot(this); return s.algObj || (s.algObj = cloneAlg(s.alg)); }
    get usages() { const s = slot(this); return s.useObj || (s.useObj = [...s.usages]); }
}
const slot = k => { const s = keys.get(k); if (!s) throw new TypeError('Illegal invocation'); return s; };
/* k's internal slots, and the record of plain values it is serialized as (to workers) */
const keyInit = (k, type, ext, alg, usages, data) => {
    const s = { type, ext: !!ext, alg, usages: usageSet(usages), data };
    keys.set(k, s);
    if (CN.cloneRec) CN.cloneRec(k, { type, ext: s.ext, alg: cloneAlg(alg), usages: [...s.usages], data });
    return k;
};
const makeKey = (type, ext, alg, usages, data) => keyInit(Object.create(CryptoKey.prototype), type, ext, alg, usages, data);
const cryptoKeyClone = k => { const s = slot(k); return makeKey(s.type, s.ext, s.alg, s.usages, s.data); };
/* structured serialization across agents (worker messages): a record of plain values, and back */
const cryptoKeyFromRecord = (k, r) => { keyInit(k, r.type, r.ext, r.alg, r.usages, r.data); };
const keyArg = v => { if (!keys.has(v)) throw new TypeError("Failed to convert value to 'CryptoKey'"); return keys.get(v); };

/* ---- helpers ---- */
const b64u = {
    enc(b) { let s = ''; for (let i = 0; i < b.length; i += 0x8000) s += String.fromCharCode.apply(null, b.subarray(i, i + 0x8000)); return btoa(s).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, ''); },
    dec(s, what) {
        if (typeof s !== 'string' || !/^[A-Za-z0-9_-]*$/.test(s) || s.length % 4 === 1) throw dataError(`The JWK member '${what}' is not valid base64url`);
        s = s.replace(/-/g, '+').replace(/_/g, '/');
        const t = atob(s + '==='.slice((s.length + 3) % 4)), u = new Uint8Array(t.length);
        for (let i = 0; i < t.length; i++) u[i] = t.charCodeAt(i);
        return u;
    },
};
const u8 = ab => new Uint8Array(ab);
const ab = u => u.buffer.byteLength === u.byteLength && u.byteOffset === 0 ? u.buffer : u.slice().buffer;
const concat = (...parts) => { const n = parts.reduce((s, p) => s + p.length, 0), o = new Uint8Array(n); let i = 0; for (const p of parts) { o.set(p, i); i += p.length; } return o; };
const checkUsages = (usages, allowed) => { if (usages.some(u => !allowed.includes(u))) throw usageError(); };
const jwkMember = (jwk, m) => { if (jwk[m] === undefined) throw dataError(`The JWK member '${m}' is missing`); return b64u.dec(jwk[m], m); };
/* the checks every JWK import shares */
const jwkCommon = (jwk, kty, use, usages, extractable) => {
    if (jwk.kty !== kty) throw dataError(`The JWK 'kty' member was not '${kty}'`);
    if (usages.length && jwk.use !== undefined && jwk.use !== use) throw dataError(`The JWK 'use' member was not '${use}'`);
    if (jwk.key_ops !== undefined) {
        if (new Set(jwk.key_ops).size !== jwk.key_ops.length) throw dataError("The JWK 'key_ops' member contains duplicates");
        if (usages.some(u => !jwk.key_ops.includes(u))) throw dataError("The JWK 'key_ops' member does not include the requested usages");
    }
    if (jwk.ext === false && extractable) throw dataError("The JWK 'ext' member was false but the key is requested to be extractable");
};
const jwkOut = (s, fields) => Object.assign(fields, { key_ops: [...s.usages], ext: s.ext });
const hmacJwkAlg = h => 'HS' + h.slice(4);
const rsaJwkAlg = (name, h) => name === 'RSA-OAEP' ? (h === 'SHA-1' ? 'RSA-OAEP' : 'RSA-OAEP-' + h.slice(4)) : (name === 'RSA-PSS' ? 'PS' : 'RS') + h.slice(4);
const aesJwkAlg = (name, bits) => 'A' + bits + name.slice(4);
/* first `bits` bits of a derived secret */
const takeBits = (secret, bits) => {
    if (bits === null) return secret;
    if (bits > secret.length * 8) throw opError();
    const out = secret.slice(0, Math.ceil(bits / 8));
    if (bits % 8) out[out.length - 1] &= 0xff << (8 - bits % 8);
    return out;
};
const pubDer = s => s.data.spki || (s.data.spki = u8(must(CN.cPub(ab(s.data.der)))));
const isMl = n => n.startsWith('ML-');
const kemOrDsaUses = n => MLKEM.includes(n) || HYBRID.includes(n) ? [['encapsulateKey', 'encapsulateBits'], ['decapsulateKey', 'decapsulateBits']] : [['verify'], ['sign']];

/* ---- per-algorithm behaviour ---- */
/* public/private usage sets of the asymmetric algorithms */
const PAIR_USES = {
    'RSASSA-PKCS1-v1_5': [['verify'], ['sign']], 'RSA-PSS': [['verify'], ['sign']], 'RSA-OAEP': [['encrypt', 'wrapKey'], ['decrypt', 'unwrapKey']],
    ECDSA: [['verify'], ['sign']], ECDH: [[], ['deriveKey', 'deriveBits']], Ed25519: [['verify'], ['sign']], X25519: [[], ['deriveKey', 'deriveBits']],
};
for (const n of [...MLDSA, ...MLKEM, ...HYBRID]) PAIR_USES[n] = kemOrDsaUses(n);
const SECRET_USES = { 'AES-CTR': ['encrypt', 'decrypt', 'wrapKey', 'unwrapKey'], 'AES-CBC': ['encrypt', 'decrypt', 'wrapKey', 'unwrapKey'], 'AES-GCM': ['encrypt', 'decrypt', 'wrapKey', 'unwrapKey'],
                      'AES-KW': ['wrapKey', 'unwrapKey'], HMAC: ['sign', 'verify'], HKDF: ['deriveKey', 'deriveBits'], PBKDF2: ['deriveKey', 'deriveBits'],
                      'ChaCha20-Poly1305': ['encrypt', 'decrypt', 'wrapKey', 'unwrapKey'] };
const nativeKind = n => RSA.includes(n) ? 'RSA' : EC.includes(n) ? 'EC' : n;
const okpOrMl = n => n === 'Ed25519' || n === 'X25519' || isMl(n);
const keyAlgOf = (a, extra) => Object.assign({ name: a.name }, extra);

/* parameter checks shared by the operations and SubtleCrypto.supports() */
const AES_LENGTHS = [128, 192, 256], GCM_TAGS = [32, 64, 96, 104, 112, 120, 128];
const rsaGenOk = a => {
    const e = a.publicExponent; let i = 0;
    while (i < e.length && e[i] === 0) i++;
    const ev = e.subarray(i), is = v => ev.length === v.length && v.every((x, j) => ev[j] === x);
    return a.modulusLength >= 256 && a.modulusLength <= 16384 && a.modulusLength % 8 === 0 && (is([3]) || is([1, 0, 1]));
};
const check = {
    generateKey(a) {
        if (AES.includes(a.name) && !AES_LENGTHS.includes(a.length)) throw opError();
        if (a.name === 'HMAC' && a.length === 0) throw opError();
        if (RSA.includes(a.name) && !rsaGenOk(a)) throw opError();
    },
    encrypt(a) {
        if (a.name === 'AES-CBC' && a.iv.length !== 16) throw opError();
        if (a.name === 'AES-CTR' && (a.counter.length !== 16 || a.length === 0 || a.length > 128)) throw opError();
        if (a.name === 'AES-GCM' && ((a.tagLength !== undefined && !GCM_TAGS.includes(a.tagLength)) || a.iv.length === 0)) throw opError();
        if (a.name === 'ChaCha20-Poly1305' && (a.iv.length !== 12 || (a.tagLength !== undefined && a.tagLength !== 128))) throw opError();
    },
    sign(a) { if (a.context && a.context.length > 255) throw opError(); },
    deriveBits(a, length) {
        if (a.name === 'HKDF' || a.name === 'PBKDF2') {
            if (length === null || length % 8) throw opError();
            if (a.name === 'HKDF' && length > 255 * HASH_BITS[a.hash.name]) throw opError();
            if (a.name === 'PBKDF2' && a.iterations === 0) throw opError();
        }
    },
    /* ECDH and X25519: the peer is a public key of the same algorithm (and curve) */
    peer(a, base) {
        const p = keys.get(a.public);
        if (p.type !== 'public') throw accessError('The peer key is not a public key');
        if (p.alg.name !== a.name || (base && base.alg.name !== p.alg.name)) throw accessError('The peer key algorithm does not match');
        if (base && a.name === 'ECDH' && base.alg.namedCurve !== p.alg.namedCurve) throw accessError('The peer key curve does not match');
        return p;
    },
};
check.decrypt = check.encrypt; check.verify = check.sign;
const ecdhBits = curve => ({ 'P-256': 256, 'P-384': 384, 'P-521': 528 })[curve];
/* get key length: the bits to derive for a derived key of this algorithm (null: whatever the KDF yields) */
const keyLength = a => {
    if (AES.includes(a.name)) { if (!AES_LENGTHS.includes(a.length)) throw opError(); return a.length; }
    if (a.name === 'HMAC') { if (a.length === undefined) return BLOCK_BITS[a.hash.name]; if (a.length === 0) throw new TypeError('HMAC length must not be zero'); return a.length; }
    if (a.name === 'ChaCha20-Poly1305') return 256;
    return null;
};

/* ---- generateKey ---- */
function generate(a, ext, usages) {
    const n = a.name;
    if (SECRET_USES[n] && !PAIR_USES[n]) {
        checkUsages(usages, SECRET_USES[n]);
        check.generateKey(a);
        let bits, alg;
        if (AES.includes(n)) { bits = a.length; alg = { name: n, length: bits }; }
        else if (n === 'HMAC') { bits = a.length === undefined ? BLOCK_BITS[a.hash.name] : a.length; alg = { name: n, length: bits, hash: { name: a.hash.name } }; }
        else if (n === 'ChaCha20-Poly1305') { bits = 256; alg = { name: n }; }
        else throw notSupported();
        const raw = new Uint8Array(Math.ceil(bits / 8));
        CN.cRandom(raw.buffer, 0, raw.length);
        if (bits % 8) raw[raw.length - 1] &= 0xff << (8 - bits % 8);
        return makeKey('secret', ext, alg, usages, raw);
    }
    const [pubUses, privUses] = PAIR_USES[n];
    checkUsages(usages, [...pubUses, ...privUses]);
    check.generateKey(a);
    let r, alg = { name: n };
    if (RSA.includes(n)) {
        r = CN.cGen('RSA', a.modulusLength, ab(a.publicExponent));
        alg = { name: n, modulusLength: a.modulusLength, publicExponent: a.publicExponent.slice(), hash: { name: a.hash.name } };
    } else if (EC.includes(n)) { r = CN.cGen('EC', a.namedCurve); alg = { name: n, namedCurve: a.namedCurve }; }
    else if (HYBRID.includes(n)) r = CN.cHybGen(n);
    else r = CN.cGen(n);
    must(r);
    const priv = makeKey('private', ext, alg, usages.filter(u => privUses.includes(u)), { der: u8(r[0]), spki: u8(r[1]) });
    const pub = makeKey('public', true, alg, usages.filter(u => pubUses.includes(u)), { der: u8(r[1]) });
    return { publicKey: pub, privateKey: priv };
}

/* ---- importKey ---- */
const SECRET_FORMATS = ['raw', 'raw-secret', 'jwk'];
function importSecret(fmt, data, a, ext, usages) {
    const n = a.name;
    if (n === 'HKDF' || n === 'PBKDF2') {
        if (fmt !== 'raw' && fmt !== 'raw-secret') throw notSupported(`Format ${fmt} is not supported for ${n}`);
        checkUsages(usages, SECRET_USES[n]);
        if (ext) throw err('SyntaxError', `${n} keys are not extractable`);
        return makeKey('secret', false, { name: n }, usages, data);
    }
    checkUsages(usages, SECRET_USES[n]);
    if (!SECRET_FORMATS.includes(fmt) || (n === 'ChaCha20-Poly1305' && fmt === 'raw')) throw notSupported(`Format ${fmt} is not supported for ${n}`);
    let raw = data;
    if (fmt === 'jwk') {
        jwkCommon(data, 'oct', n === 'HMAC' ? 'sig' : 'enc', usages, ext);
        raw = jwkMember(data, 'k');
        const alg = data.alg;
        if (alg !== undefined) {
            const want = AES.includes(n) ? aesJwkAlg(n, raw.length * 8) : n === 'HMAC' ? hmacJwkAlg(a.hash.name) : 'C20P';
            if (alg !== want) throw dataError(`The JWK 'alg' member was not '${want}'`);
        }
    }
    if (AES.includes(n)) {
        if (![16, 24, 32].includes(raw.length)) throw dataError('AES key data must be 128, 192 or 256 bits');
        return makeKey('secret', ext, { name: n, length: raw.length * 8 }, usages, raw);
    }
    if (n === 'ChaCha20-Poly1305') {
        if (raw.length !== 32) throw dataError('ChaCha20-Poly1305 key data must be 256 bits');
        return makeKey('secret', ext, { name: n }, usages, raw);
    }
    /* HMAC */
    let len = raw.length * 8;
    if (len === 0) throw dataError('HMAC key data must not be empty');
    if (a.length !== undefined) {
        if (a.length > len || a.length <= len - 8) throw dataError('The HMAC length does not match the key data');
        len = a.length;
    }
    return makeKey('secret', ext, { name: n, length: len, hash: { name: a.hash.name } }, usages, raw);
}
const PUBLIC_FORMATS = { RSA: ['spki', 'jwk'], EC: ['spki', 'jwk', 'raw', 'raw-public'], OKP: ['spki', 'jwk', 'raw', 'raw-public'], ML: ['spki', 'jwk', 'raw-public'], HK: ['jwk', 'raw-public'] };
const PRIVATE_FORMATS = { RSA: ['pkcs8', 'jwk'], EC: ['pkcs8', 'jwk'], OKP: ['pkcs8', 'jwk'], ML: ['pkcs8', 'jwk', 'raw-seed'], HK: ['jwk', 'raw-seed'] };
const family = n => RSA.includes(n) ? 'RSA' : EC.includes(n) ? 'EC' : isMl(n) ? 'ML' : HYBRID.includes(n) ? 'HK' : 'OKP';
const OKP_CRV = { Ed25519: 'Ed25519', X25519: 'X25519' };
function importPair(fmt, data, a, ext, usages) {
    const n = a.name, fam = family(n), [pubUses, privUses] = PAIR_USES[n];
    let isPriv;
    if (fmt === 'jwk') isPriv = fam === 'ML' || fam === 'HK' ? data.priv !== undefined : data.d !== undefined;
    else if (PRIVATE_FORMATS[fam].includes(fmt)) isPriv = true;
    else if (PUBLIC_FORMATS[fam].includes(fmt)) isPriv = false;
    else throw notSupported(`Format ${fmt} is not supported for ${n}`);
    checkUsages(usages, isPriv ? privUses : pubUses);
    let der, alg = { name: n };
    if (fam === 'HK') return importHybrid(fmt, data, n, ext, usages, isPriv);
    const parsed = (bytes, priv) => {
        const r = CN.cParse(ab(bytes), priv);
        if (!r || r[0] !== nativeKind(n)) throw dataError();
        return r;
    };
    if (fmt === 'spki' || fmt === 'pkcs8') {
        const r = parsed(data, fmt === 'pkcs8');
        if (fam === 'RSA') alg = { name: n, modulusLength: r[1], publicExponent: u8(r[2]), hash: { name: a.hash.name } };
        if (fam === 'EC') { if (r[1] !== a.namedCurve) throw dataError('The key curve does not match namedCurve'); alg = { name: n, namedCurve: a.namedCurve }; }
        der = u8(r[3]);
    } else if (fmt === 'raw' || fmt === 'raw-public') {
        der = CN.cFromRawPub(nativeKind(n), a.namedCurve || null, ab(data));
        if (!der) throw dataError();
        der = u8(der);
        if (fam === 'EC') alg = { name: n, namedCurve: a.namedCurve };
    } else if (fmt === 'raw-seed') {
        der = CN.cFromRawPriv(n, null, ab(data), null);
        if (!der) throw dataError();
        der = u8(der);
    } else {
        /* jwk */
        if (fam === 'RSA') {
            jwkCommon(data, 'RSA', n === 'RSA-OAEP' ? 'enc' : 'sig', usages, ext);
            if (data.alg !== undefined && data.alg !== rsaJwkAlg(n, a.hash.name)) throw dataError("The JWK 'alg' member does not match the hash");
            const nb = jwkMember(data, 'n'), eb = jwkMember(data, 'e');
            let r;
            if (isPriv) {
                if (data.oth !== undefined) throw notSupported('Multi-prime RSA keys are not supported');
                const parts = ['d', 'p', 'q', 'dp', 'dq', 'qi'].map(m => ab(jwkMember(data, m)));
                r = CN.cRsaSet(ab(nb), ab(eb), ...parts);
            } else r = CN.cRsaSet(ab(nb), ab(eb));
            if (!r) throw dataError();
            der = u8(r);
            const info = parsed(der, isPriv);
            alg = { name: n, modulusLength: info[1], publicExponent: u8(info[2]), hash: { name: a.hash.name } };
        } else if (fam === 'EC') {
            jwkCommon(data, 'EC', n === 'ECDSA' ? 'sig' : 'enc', usages, ext);
            if (data.crv === undefined) throw dataError("The JWK member 'crv' is missing");
            if (data.crv !== a.namedCurve) throw dataError("The JWK 'crv' member does not match namedCurve");
            if (n === 'ECDSA' && data.alg !== undefined && data.alg !== { 'P-256': 'ES256', 'P-384': 'ES384', 'P-521': 'ES512' }[a.namedCurve]) throw dataError("The JWK 'alg' member does not match the curve");
            const size = Math.ceil(ecdhBits(a.namedCurve) / 8), x = jwkMember(data, 'x'), y = jwkMember(data, 'y');
            if (x.length !== size || y.length !== size) throw dataError('The JWK coordinates have the wrong length');
            const point = concat([4], x, y);
            if (isPriv) {
                const d = jwkMember(data, 'd');
                if (d.length !== size) throw dataError("The JWK member 'd' has the wrong length");
                der = CN.cFromRawPriv('EC', a.namedCurve, ab(d), ab(point));
            } else der = CN.cFromRawPub('EC', a.namedCurve, ab(point));
            if (!der) throw dataError();
            der = u8(der);
            alg = { name: n, namedCurve: a.namedCurve };
        } else if (fam === 'OKP') {
            jwkCommon(data, 'OKP', n === 'Ed25519' ? 'sig' : 'enc', usages, ext);
            if (data.crv === undefined) throw dataError("The JWK member 'crv' is missing");
            if (data.crv !== OKP_CRV[n]) throw dataError("The JWK 'crv' member does not match the algorithm");
            if (n === 'Ed25519' && data.alg !== undefined && data.alg !== 'Ed25519' && data.alg !== 'EdDSA') throw dataError("The JWK 'alg' member is not valid");
            const x = jwkMember(data, 'x');
            if (x.length !== 32) throw dataError("The JWK member 'x' has the wrong length");
            if (isPriv) {
                const d = jwkMember(data, 'd');
                if (d.length !== 32) throw dataError("The JWK member 'd' has the wrong length");
                der = CN.cFromRawPriv(n, null, ab(d), ab(x));
            } else der = CN.cFromRawPub(n, null, ab(x));
            if (!der) throw dataError();
            der = u8(der);
        } else {
            jwkCommon(data, 'AKP', MLDSA.includes(n) ? 'sig' : 'enc', usages, ext);
            if (data.alg === undefined) throw dataError("The JWK member 'alg' is missing");
            if (data.alg !== n) throw dataError("The JWK 'alg' member does not match the algorithm");
            const pub = jwkMember(data, 'pub');
            der = isPriv ? CN.cFromRawPriv(n, null, ab(jwkMember(data, 'priv')), ab(pub)) : CN.cFromRawPub(n, null, ab(pub));
            if (!der) throw dataError();
            der = u8(der);
        }
    }
    return makeKey(isPriv ? 'private' : 'public', ext, alg, usages, { der });
}
/* hybrid KEM keys: raw-seed / raw-public, or an AKP JWK whose pub must match the one derived from priv */
function importHybrid(fmt, data, n, ext, usages, isPriv) {
    let seed = null, pub = null;
    if (fmt === 'raw-seed') seed = data;
    else if (fmt === 'raw-public') pub = data;
    else {
        jwkCommon(data, 'AKP', 'enc', usages, ext);
        if (data.alg === undefined) throw dataError("The JWK member 'alg' is missing");
        if (data.alg !== n) throw dataError("The JWK 'alg' member does not match the algorithm");
        pub = jwkMember(data, 'pub');
        if (isPriv) seed = jwkMember(data, 'priv');
    }
    if (seed) {
        if (seed.length !== 32) throw dataError('The seed must be 32 bytes');
        const r = must(CN.cHybGen(n, ab(seed))), ek = u8(r[1]);
        if (pub && (pub.length !== ek.length || pub.some((b, i) => b !== ek[i]))) throw dataError('The public key does not match the seed');
        return makeKey('private', ext, { name: n }, usages, { der: seed.slice(), spki: ek });
    }
    if (!CN.cHybCheck(n, ab(pub))) throw dataError();
    return makeKey('public', ext, { name: n }, usages, { der: pub.slice() });
}
function importAny(fmt, data, a, ext, usages) {
    const k = PAIR_USES[a.name] ? importPair(fmt, data, a, ext, usages) : importSecret(fmt, data, a, ext, usages);
    const s = keys.get(k);
    if ((s.type === 'secret' || s.type === 'private') && usages.length === 0) throw err('SyntaxError', 'Usages cannot be empty when creating a key.');
    return k;
}

/* ---- exportKey ---- */
function exportAny(fmt, key) {
    const s = keys.get(key), n = s.alg.name;
    if (n === 'HKDF' || n === 'PBKDF2') throw notSupported(`${n} keys cannot be exported`);
    if (!s.ext) throw accessError('key is not extractable');
    if (s.type === 'secret') {
        if (fmt === 'raw' && n !== 'ChaCha20-Poly1305' || fmt === 'raw-secret') return ab(s.data.slice());
        if (fmt === 'jwk') {
            const alg = AES.includes(n) ? aesJwkAlg(n, s.data.length * 8) : n === 'HMAC' ? hmacJwkAlg(s.alg.hash.name) : 'C20P';
            return jwkOut(s, { kty: 'oct', k: b64u.enc(s.data), alg });
        }
        throw notSupported(`Format ${fmt} is not supported`);
    }
    const fam = family(n), priv = s.type === 'private';
    const fmts = [...PUBLIC_FORMATS[fam], ...PRIVATE_FORMATS[fam]];
    if (!fmts.includes(fmt)) throw notSupported(`Format ${fmt} is not supported for ${n}`);
    if (fmt !== 'jwk' && PRIVATE_FORMATS[fam].includes(fmt) !== priv) throw accessError(`A ${s.type} key cannot be exported as ${fmt}`);
    if (fam === 'HK') {
        if (fmt !== 'jwk') return ab(s.data.der.slice());
        const o = { kty: 'AKP', alg: n, pub: b64u.enc(priv ? s.data.spki : s.data.der) };
        if (priv) o.priv = b64u.enc(s.data.der);
        return jwkOut(s, o);
    }
    if (fmt === 'spki' || fmt === 'pkcs8') return ab(s.data.der.slice());
    if (fmt === 'raw' || fmt === 'raw-public') return must(CN.cRawPub(ab(s.data.der)));
    if (fmt === 'raw-seed') return must(CN.cRawPriv(ab(s.data.der)));
    /* jwk */
    if (fam === 'RSA') {
        const p = must(CN.cRsaGet(ab(s.data.der), priv)).map(x => b64u.enc(u8(x)));
        const o = { kty: 'RSA', n: p[0], e: p[1] };
        if (priv) Object.assign(o, { d: p[2], p: p[3], q: p[4], dp: p[5], dq: p[6], qi: p[7] });
        o.alg = rsaJwkAlg(n, s.alg.hash.name);
        return jwkOut(s, o);
    }
    const pub = u8(must(CN.cRawPub(ab(priv ? pubDer(s) : s.data.der))));
    if (fam === 'EC') {
        const size = (pub.length - 1) / 2, o = { kty: 'EC', crv: s.alg.namedCurve, x: b64u.enc(pub.subarray(1, 1 + size)), y: b64u.enc(pub.subarray(1 + size)) };
        if (priv) o.d = b64u.enc(u8(must(CN.cRawPriv(ab(s.data.der)))));
        return jwkOut(s, o);
    }
    if (fam === 'OKP') {
        const o = { kty: 'OKP', crv: OKP_CRV[n], x: b64u.enc(pub) };
        if (priv) o.d = b64u.enc(u8(must(CN.cRawPriv(ab(s.data.der)))));
        if (n === 'Ed25519') o.alg = 'Ed25519';
        return jwkOut(s, o);
    }
    const o = { kty: 'AKP', alg: n, pub: b64u.enc(pub) };
    if (priv) o.priv = b64u.enc(u8(must(CN.cRawPriv(ab(s.data.der)))));
    return jwkOut(s, o);
}

/* ---- the operations proper ---- */
const useKey = (key, a, usage) => {
    const s = keys.get(key);
    if (a.name !== s.alg.name) throw accessError(`The key is not a ${a.name} key`);
    if (!s.usages.includes(usage)) throw accessError(`The key does not allow ${usage}`);
    return s;
};
const needType = (s, type) => { if (s.type !== type) throw accessError(`The key is not a ${type} key`); };
function cipher(a, s, data, enc) {
    check.encrypt(a);
    const n = a.name;
    if (n === 'RSA-OAEP') { needType(s, enc ? 'public' : 'private'); return must(CN.cOaep(enc, s.alg.hash.name, ab(s.data.der), ab(a.label || new Uint8Array(0)), ab(data))); }
    if (n === 'AES-CTR') {
        if (a.length < 128 && Math.ceil(data.length / 16) > 2 ** a.length) throw opError();
        return must(CN.cAesCtr(ab(s.data), ab(a.counter), a.length, ab(data)));
    }
    if (n === 'AES-CBC') return must(CN.cAes(n, enc, ab(s.data), ab(a.iv), ab(data), null, 0));
    if (n === 'AES-GCM') {
        const tag = a.tagLength === undefined ? 128 : a.tagLength;
        if (!enc && data.length < tag / 8) throw opError();
        return must(CN.cAes(n, enc, ab(s.data), ab(a.iv), ab(data), ab(a.additionalData || new Uint8Array(0)), tag));
    }
    if (n === 'ChaCha20-Poly1305') {
        if (!enc && data.length < 16) throw opError();
        return must(CN.cChaCha(enc, ab(s.data), ab(a.iv), ab(data), ab(a.additionalData || new Uint8Array(0))));
    }
    throw notSupported();
}
function signOp(a, s, data) {
    check.sign(a);
    const n = a.name;
    if (n === 'HMAC') return must(CN.cHmac(s.alg.hash.name, ab(s.data), ab(data)));
    needType(s, 'private');
    const hash = n === 'ECDSA' ? a.hash.name : s.alg.hash ? s.alg.hash.name : '';
    return must(CN.cSign(n, hash, ab(s.data.der), ab(data), n === 'RSA-PSS' ? a.saltLength : a.context ? ab(a.context) : undefined));
}
function verifyOp(a, s, sig, data) {
    check.sign(a);
    const n = a.name;
    if (n === 'HMAC') {
        const mac = u8(must(CN.cHmac(s.alg.hash.name, ab(s.data), ab(data))));
        if (mac.length !== sig.length) return false;
        let diff = 0;
        for (let i = 0; i < mac.length; i++) diff |= mac[i] ^ sig[i];
        return diff === 0;
    }
    needType(s, 'public');
    const hash = n === 'ECDSA' ? a.hash.name : s.alg.hash ? s.alg.hash.name : '';
    const r = CN.cVerify(n, hash, ab(s.data.der), ab(data), ab(sig), n === 'RSA-PSS' ? a.saltLength : a.context ? ab(a.context) : undefined);
    if (r === undefined) throw opError();
    return r;
}
function deriveBitsOp(a, s, length) {
    const n = a.name;
    if (n === 'ECDH' || n === 'X25519') {
        needType(s, 'private');
        const p = check.peer(a, s);
        const secret = u8(must(CN.cDerive(ab(s.data.der), ab(p.data.der))));
        if (n === 'X25519' && secret.every(b => b === 0)) throw opError();
        return takeBits(secret, length);
    }
    check.deriveBits(a, length);
    if (length === 0) return new Uint8Array(0);
    if (n === 'HKDF') return u8(must(CN.cHkdf(a.hash.name, ab(s.data), ab(a.salt), ab(a.info), length / 8)));
    return u8(must(CN.cPbkdf2(a.hash.name, ab(s.data), ab(a.salt), a.iterations, length / 8)));
}
const AESKW_OK = d => d.length >= 16 && d.length % 8 === 0;
function wrapOp(a, s, bytes, wrap) {
    if (a.name === 'AES-KW') {
        if (wrap && !AESKW_OK(bytes)) throw opError();
        return must(CN.cAes('AES-KW', wrap, ab(s.data), null, ab(bytes), null, 0));
    }
    return cipher(a, s, bytes, wrap);
}
const lengthArg = v => v === undefined || v === null ? null : enforce(v, 4294967295);

const kemEncap = s => HYBRID.includes(s.alg.name) ? CN.cHybEncap(s.alg.name, ab(s.data.der)) : CN.cEncap(ab(s.data.der));
const kemDecap = (s, ct) => HYBRID.includes(s.alg.name) ? CN.cHybDecap(s.alg.name, ab(s.data.der), ab(ct)) : CN.cDecap(ab(s.data.der), ab(ct));
const SC = {
    digest(algorithm, data) {
        argc(arguments, 2, 'digest');
        const a = normalize('digest', algorithm), d = bytesOf(data);
        return must(CN.cDigest(a.name, ab(d)));
    },
    generateKey(algorithm, extractable, keyUsages) {
        argc(arguments, 3, 'generateKey');
        const ext = !!extractable, usages = usagesOf(keyUsages), a = normalize('generateKey', algorithm);
        const r = generate(a, ext, usages);
        const priv = keys.get(r instanceof CryptoKey ? r : r.privateKey);
        if (priv.usages.length === 0) throw err('SyntaxError', 'Usages cannot be empty when creating a key.');
        return r;
    },
    importKey(format, keyData, algorithm, extractable, keyUsages) {
        argc(arguments, 5, 'importKey');
        const fmt = enumOf(format, FORMATS, 'KeyFormat');
        const jwk = isBufferSource(keyData) ? null : jwkDict(keyData);
        const ext = !!extractable, usages = usagesOf(keyUsages), a = normalize('importKey', algorithm);
        let data;
        if (fmt === 'jwk') { if (!jwk) throw new TypeError('Key data must be a JsonWebKey for the jwk format'); data = jwk; }
        else { if (jwk) throw new TypeError('Key data must be a BufferSource for this format'); data = bytesOf(keyData); }
        return importAny(fmt, data, a, ext, usages);
    },
    exportKey(format, key) {
        argc(arguments, 2, 'exportKey');
        const fmt = enumOf(format, FORMATS, 'KeyFormat');
        keyArg(key);
        return exportAny(fmt, key);
    },
    encrypt(algorithm, key, data) { argc(arguments, 3, 'encrypt'); keyArg(key); const a = normalize('encrypt', algorithm), d = bytesOf(data); return cipher(a, useKey(key, a, 'encrypt'), d, true); },
    decrypt(algorithm, key, data) { argc(arguments, 3, 'decrypt'); keyArg(key); const a = normalize('decrypt', algorithm), d = bytesOf(data); return cipher(a, useKey(key, a, 'decrypt'), d, false); },
    sign(algorithm, key, data) { argc(arguments, 3, 'sign'); keyArg(key); const a = normalize('sign', algorithm), d = bytesOf(data); return signOp(a, useKey(key, a, 'sign'), d); },
    verify(algorithm, key, signature, data) {
        argc(arguments, 4, 'verify'); keyArg(key);
        const a = normalize('verify', algorithm), sig = bytesOf(signature), d = bytesOf(data);
        return verifyOp(a, useKey(key, a, 'verify'), sig, d);
    },
    deriveBits(algorithm, baseKey, length = null) {
        argc(arguments, 2, 'deriveBits'); keyArg(baseKey);
        const len = lengthArg(length), a = normalize('deriveBits', algorithm);
        return ab(deriveBitsOp(a, useKey(baseKey, a, 'deriveBits'), len));
    },
    deriveKey(algorithm, baseKey, derivedKeyType, extractable, keyUsages) {
        argc(arguments, 5, 'deriveKey'); keyArg(baseKey);
        const ext = !!extractable, usages = usagesOf(keyUsages);
        const a = normalize('deriveBits', algorithm), imp = normalize('importKey', derivedKeyType), lenAlg = normalize('getKeyLength', derivedKeyType);
        const s = useKey(baseKey, a, 'deriveKey');
        const bits = deriveBitsOp(a, s, keyLength(lenAlg));
        return importAny(imp.name === 'ChaCha20-Poly1305' ? 'raw-secret' : 'raw', bits, imp, ext, usages);
    },
    wrapKey(format, key, wrappingKey, wrapAlgorithm) {
        argc(arguments, 4, 'wrapKey');
        const fmt = enumOf(format, FORMATS, 'KeyFormat');
        keyArg(key); keyArg(wrappingKey);
        let a;
        try { a = normalize('wrapKey', wrapAlgorithm); } catch (e) { a = normalize('encrypt', wrapAlgorithm); }
        const ws = useKey(wrappingKey, a, 'wrapKey');
        const exported = exportAny(fmt, key);
        const bytes = fmt === 'jwk' ? new TextEncoder().encode(JSON.stringify(exported)) : u8(exported);
        return wrapOp(a, ws, bytes, true);
    },
    unwrapKey(format, wrappedKey, unwrappingKey, unwrapAlgorithm, unwrappedKeyAlgorithm, extractable, keyUsages) {
        argc(arguments, 7, 'unwrapKey');
        const fmt = enumOf(format, FORMATS, 'KeyFormat');
        keyArg(unwrappingKey);
        const ext = !!extractable, usages = usagesOf(keyUsages);
        let a;
        try { a = normalize('unwrapKey', unwrapAlgorithm); } catch (e) { a = normalize('decrypt', unwrapAlgorithm); }
        const imp = normalize('importKey', unwrappedKeyAlgorithm), wrapped = bytesOf(wrappedKey);
        const s = useKey(unwrappingKey, a, 'unwrapKey');
        const bytes = u8(wrapOp(a, s, wrapped, false));
        let data = bytes;
        if (fmt === 'jwk') {
            let parsed;
            try { parsed = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes)); } catch (e) { throw dataError('The unwrapped key is not valid JSON'); }
            if (!isObj(parsed)) throw dataError('The unwrapped key is not a JSON object');
            data = jwkDict(parsed);
            if (data.kty === undefined) throw dataError("The JWK member 'kty' is missing");
        }
        return importAny(fmt, data, imp, ext, usages);
    },
    encapsulateBits(encapsulationAlgorithm, encapsulationKey) {
        argc(arguments, 2, 'encapsulateBits'); keyArg(encapsulationKey);
        const a = normalize('encapsulate', encapsulationAlgorithm), s = useKey(encapsulationKey, a, 'encapsulateBits');
        needType(s, 'public');
        const r = must(kemEncap(s));
        return { sharedKey: r[0], ciphertext: r[1] };
    },
    encapsulateKey(encapsulationAlgorithm, encapsulationKey, sharedKeyAlgorithm, extractable, keyUsages) {
        argc(arguments, 5, 'encapsulateKey'); keyArg(encapsulationKey);
        const ext = !!extractable, usages = usagesOf(keyUsages);
        const a = normalize('encapsulate', encapsulationAlgorithm), imp = normalize('importKey', sharedKeyAlgorithm);
        const s = useKey(encapsulationKey, a, 'encapsulateKey');
        needType(s, 'public');
        const r = must(kemEncap(s));
        return { sharedKey: importAny('raw-secret', u8(r[0]), imp, ext, usages), ciphertext: r[1] };
    },
    decapsulateBits(decapsulationAlgorithm, decapsulationKey, ciphertext) {
        argc(arguments, 3, 'decapsulateBits'); keyArg(decapsulationKey);
        const a = normalize('decapsulate', decapsulationAlgorithm), ct = bytesOf(ciphertext), s = useKey(decapsulationKey, a, 'decapsulateBits');
        needType(s, 'private');
        return must(kemDecap(s, ct));
    },
    decapsulateKey(decapsulationAlgorithm, decapsulationKey, ciphertext, sharedKeyAlgorithm, extractable, keyUsages) {
        argc(arguments, 6, 'decapsulateKey'); keyArg(decapsulationKey);
        const ext = !!extractable, usages = usagesOf(keyUsages);
        const a = normalize('decapsulate', decapsulationAlgorithm), ct = bytesOf(ciphertext), imp = normalize('importKey', sharedKeyAlgorithm);
        const s = useKey(decapsulationKey, a, 'decapsulateKey');
        needType(s, 'private');
        return importAny('raw-secret', u8(must(kemDecap(s, ct))), imp, ext, usages);
    },
    getPublicKey(key, keyUsages) {
        argc(arguments, 2, 'getPublicKey');
        const s = keyArg(key), usages = usagesOf(keyUsages);
        if (!PAIR_USES[s.alg.name]) throw notSupported(`${s.alg.name} keys have no public key`);
        if (s.type !== 'private') throw accessError('The key is not a private key');
        checkUsages(usages, PAIR_USES[s.alg.name][0]);
        return makeKey('public', true, s.alg, usages, { der: pubDer(s) });
    },
};
/* SubtleCrypto.supports(operation, algorithm[, length | additionalAlgorithm]) */
const OPS = ['encrypt', 'decrypt', 'sign', 'verify', 'digest', 'generateKey', 'deriveKey', 'deriveBits', 'importKey', 'exportKey', 'wrapKey', 'unwrapKey',
             'encapsulateKey', 'encapsulateBits', 'decapsulateKey', 'decapsulateBits', 'getPublicKey'];
const KEM_SECRET_BITS = 256;
function supports(op, algorithm, extra) {
    if (!OPS.includes(op)) return false;
    try {
        if (op === 'exportKey' || op === 'getPublicKey') {
            const n = algName(algorithm);
            return op === 'getPublicKey' ? !!PAIR_USES[n] : n !== 'HKDF' && n !== 'PBKDF2';
        }
        if (op === 'wrapKey' || op === 'unwrapKey') {
            let a;
            try { a = normalize(op, algorithm); } catch (e) { a = normalize(op === 'wrapKey' ? 'encrypt' : 'decrypt', algorithm); check.encrypt(a); }
            if (extra !== undefined) {
                if (op === 'unwrapKey') normalize('importKey', extra);
                else { const n = algName(extra); if (n === 'HKDF' || n === 'PBKDF2') return false; }
            }
            return true;
        }
        if (op === 'encapsulateKey' || op === 'decapsulateKey' || op === 'encapsulateBits' || op === 'decapsulateBits') {
            normalize(op.startsWith('en') ? 'encapsulate' : 'decapsulate', algorithm);
            if (op === 'encapsulateKey' || op === 'decapsulateKey') {
                const imp = normalize('importKey', extra);
                if (!SECRET_USES[imp.name] || PAIR_USES[imp.name] || imp.name === 'HKDF' || imp.name === 'PBKDF2') return imp.name === 'HKDF' || imp.name === 'PBKDF2';
                if (AES.includes(imp.name)) return KEM_SECRET_BITS === 256;
                if (imp.name === 'HMAC') return imp.length === undefined || imp.length === KEM_SECRET_BITS;
            }
            return true;
        }
        if (op === 'deriveKey') {
            const a = normalize('deriveBits', algorithm), imp = normalize('importKey', extra), lenAlg = normalize('getKeyLength', extra);
            const len = keyLength(lenAlg);
            if (imp.name === 'HMAC' && imp.length === 0) return false;
            return derivable(a, len);
        }
        const a = normalize(op, algorithm);
        if (op === 'generateKey') { check.generateKey(a); return true; }
        if (op === 'importKey') { if (a.name === 'HMAC' && a.length === 0) return false; return true; }
        if (op === 'encrypt' || op === 'decrypt') { check.encrypt(a); return true; }
        if (op === 'sign' || op === 'verify') { check.sign(a); return true; }
        if (op === 'deriveBits') return derivable(a, extra === undefined || extra === null ? null : enforce(extra, 4294967295));
        return true;
    } catch (e) { return false; }
}
/* the registered name of an algorithm known to importKey, ignoring its parameters */
function algName(alg) {
    const n = isObj(alg) ? alg.name : alg;
    if (n === undefined) throw new TypeError("'name' is required");
    const want = lowerAscii(String(n)), name = Object.keys(REG.importKey).find(k => lowerAscii(k) === want);
    if (!name) throw notSupported();
    return name;
}
function derivable(a, len) {
    if (a.name === 'ECDH' || a.name === 'X25519') {
        const p = check.peer(a, null);
        const max = a.name === 'X25519' ? 256 : ecdhBits(p.alg.namedCurve);
        return len === null || len <= max;
    }
    check.deriveBits(a, len);
    return true;
}

/* ---- interface objects ---- */
const idlMethod = (proto, name, fn, length) => {
    const f = { [name](...args) { try { return Promise.resolve(fn.apply(this, args)); } catch (e) { return Promise.reject(e); } } }[name];
    Object.defineProperty(f, 'length', { value: length });
    Object.defineProperty(proto, name, { value: f, writable: true, enumerable: true, configurable: true });
};
class SubtleCrypto { constructor() { throw new TypeError('Illegal constructor'); } }
const subtleBrand = new WeakSet();
const LENGTHS = { digest: 2, generateKey: 3, importKey: 5, exportKey: 2, encrypt: 3, decrypt: 3, sign: 3, verify: 4, deriveBits: 2, deriveKey: 5, wrapKey: 4, unwrapKey: 7,
                  encapsulateBits: 2, encapsulateKey: 5, decapsulateBits: 3, decapsulateKey: 6, getPublicKey: 2 };
for (const k of Object.keys(LENGTHS)) idlMethod(SubtleCrypto.prototype, k, function (...args) {
    if (!subtleBrand.has(this)) throw new TypeError('Illegal invocation');
    return SC[k].apply(null, args);
}, LENGTHS[k]);
Object.defineProperty(SubtleCrypto, 'supports', { value: { supports(operation, algorithm, lengthOrAdditionalAlgorithm = undefined) {
    if (arguments.length < 2) throw new TypeError("Failed to execute 'supports' on 'SubtleCrypto': 2 arguments required.");
    return supports(String(operation), algorithm, lengthOrAdditionalAlgorithm);
} }.supports, writable: true, enumerable: true, configurable: true });
Object.defineProperty(SubtleCrypto.prototype, Symbol.toStringTag, { value: 'SubtleCrypto', configurable: true });
for (const k of ['type', 'extractable', 'algorithm', 'usages']) Object.defineProperty(CryptoKey.prototype, k, Object.assign(Object.getOwnPropertyDescriptor(CryptoKey.prototype, k), { enumerable: true }));
Object.defineProperty(CryptoKey.prototype, Symbol.toStringTag, { value: 'CryptoKey', configurable: true });
const subtle = Object.create(SubtleCrypto.prototype);
subtleBrand.add(subtle);

const INTEGER_ARRAYS = ['Int8Array', 'Uint8Array', 'Uint8ClampedArray', 'Int16Array', 'Uint16Array', 'Int32Array', 'Uint32Array', 'BigInt64Array', 'BigUint64Array'];
class Crypto {
    constructor() { throw new TypeError('Illegal constructor'); }
    getRandomValues(array) {
        if (this !== cryptoObj) throw new TypeError('Illegal invocation');
        if (arguments.length < 1) throw new TypeError("Failed to execute 'getRandomValues' on 'Crypto': 1 argument required, but only 0 present.");
        if (!ArrayBuffer.isView(array)) throw new TypeError("Failed to execute 'getRandomValues' on 'Crypto': parameter 1 is not of type 'ArrayBufferView'.");
        if (!INTEGER_ARRAYS.includes(Object.prototype.toString.call(array).slice(8, -1))) throw err('TypeMismatchError', "Failed to execute 'getRandomValues' on 'Crypto': The provided ArrayBufferView is of type '" + Object.prototype.toString.call(array).slice(8, -1) + "', which is not an integer array type.");
        if (array.byteLength > 65536) throw new QuotaExceededError(`Failed to execute 'getRandomValues' on 'Crypto': The ArrayBufferView's byte length (${array.byteLength}) exceeds the number of bytes of entropy available via this API (65536).`);
        if (isShared(array.buffer)) { const t = new Uint8Array(array.byteLength); CN.cRandom(t.buffer, 0, t.length); new Uint8Array(array.buffer, array.byteOffset, array.byteLength).set(t); }
        else CN.cRandom(array.buffer, array.byteOffset, array.byteLength);
        return array;
    }
    randomUUID() {
        if (this !== cryptoObj) throw new TypeError('Illegal invocation');
        const b = new Uint8Array(16);
        CN.cRandom(b.buffer, 0, 16);
        b[6] = (b[6] & 15) | 64; b[8] = (b[8] & 63) | 128;
        const h = [...b].map(x => x.toString(16).padStart(2, '0')).join('');
        return `${h.slice(0, 8)}-${h.slice(8, 12)}-${h.slice(12, 16)}-${h.slice(16, 20)}-${h.slice(20)}`;
    }
}
const cryptoObj = Object.create(Crypto.prototype);
for (const k of ['getRandomValues', 'randomUUID']) Object.defineProperty(Crypto.prototype, k, { enumerable: true });
if (CRYPTO_SECURE()) Object.defineProperty(Crypto.prototype, 'subtle', {
    get: Object.getOwnPropertyDescriptor({ get subtle() { if (this !== cryptoObj) throw new TypeError('Illegal invocation'); return subtle; } }, 'subtle').get,
    enumerable: true, configurable: true });
Object.defineProperty(Crypto.prototype, Symbol.toStringTag, { value: 'Crypto', configurable: true });
if (CN.cloneHost) CN.cloneHost(CryptoKey.prototype, cryptoKeyFromRecord, 1);
return { Crypto, CryptoKey, SubtleCrypto, crypto: cryptoObj, cryptoKeyClone };
})();
