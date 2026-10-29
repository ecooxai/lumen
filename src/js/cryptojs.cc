/* WebCrypto primitives (OpenSSL 3) shared by page and worker contexts; the algorithm rules live in shared/crypto.js.
   Asymmetric keys cross the bridge as DER: private keys PKCS#8, public keys SubjectPublicKeyInfo. Symmetric keys are raw
   bytes. A failed operation returns undefined and crypto.js turns it into the right DOMException. */
#include "js_int.h"
#include <openssl/core_names.h>
#include <openssl/decoder.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/encoder.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/param_build.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <vector>

namespace {
typedef v8::FunctionCallbackInfo<v8::Value> FCI;
struct Bytes { const uint8_t *p = (const uint8_t *)""; size_t n = 0; };
Bytes in(v8::Local<v8::Value> v) {
    Bytes b;
    if (v->IsArrayBuffer()) { auto ab = v.As<v8::ArrayBuffer>(); b.n = ab->ByteLength(); if (b.n) b.p = (const uint8_t *)ab->Data(); }
    else if (v->IsArrayBufferView()) { auto vw = v.As<v8::ArrayBufferView>(); b.n = vw->ByteLength(); if (b.n) b.p = (const uint8_t *)vw->Buffer()->Data() + vw->ByteOffset(); }
    return b;
}
v8::Local<v8::Value> out(v8::Isolate *iso, const void *p, size_t n) {
    auto ab = v8::ArrayBuffer::New(iso, n);
    if (n) memcpy(ab->Data(), p, n);
    return ab;
}
v8::Local<v8::Value> out(v8::Isolate *iso, const std::vector<uint8_t> &v) { return out(iso, v.data(), v.size()); }
double num(v8::Isolate *iso, v8::Local<v8::Value> v) { return v->NumberValue(iso->GetCurrentContext()).FromMaybe(0); }
v8::Local<v8::Array> arr(v8::Isolate *iso, std::initializer_list<v8::Local<v8::Value>> xs) {
    auto ctx = iso->GetCurrentContext(); auto r = v8::Array::New(iso, (int)xs.size()); uint32_t i = 0;
    for (auto &x : xs) (void)r->Set(ctx, i++, x);
    return r;
}
/* every native call leaves the OpenSSL error queue empty */
struct Scope { ~Scope() { ERR_clear_error(); } };
struct Key {
    EVP_PKEY *k = nullptr;
    Key() = default;
    explicit Key(EVP_PKEY *x) : k(x) {}
    ~Key() { EVP_PKEY_free(k); }
    Key(const Key &) = delete;
    Key &operator=(const Key &) = delete;
    explicit operator bool() const { return k != nullptr; }
};

const EVP_MD *md_of(const std::string &h) {
    if (h == "SHA-1") return EVP_sha1();
    if (h == "SHA-256") return EVP_sha256();
    if (h == "SHA-384") return EVP_sha384();
    if (h == "SHA-512") return EVP_sha512();
    return nullptr;
}
const char *group_of(const std::string &c) { return c == "P-256" ? "prime256v1" : c == "P-384" ? "secp384r1" : c == "P-521" ? "secp521r1" : nullptr; }
const char *curve_of(EVP_PKEY *k) {
    char g[64] = ""; size_t gl = 0;
    if (!EVP_PKEY_get_utf8_string_param(k, OSSL_PKEY_PARAM_GROUP_NAME, g, sizeof g, &gl)) return nullptr;
    return !strcmp(g, "prime256v1") ? "P-256" : !strcmp(g, "secp384r1") ? "P-384" : !strcmp(g, "secp521r1") ? "P-521" : nullptr;
}
size_t field_bytes(EVP_PKEY *k) { return (size_t)(EVP_PKEY_get_bits(k) + 7) / 8; }
/* the WebCrypto-facing type of a key: RSA, EC, Ed25519, X25519, ML-DSA-44/65/87, ML-KEM-512/768/1024 */
const char *const ML[] = { "ML-DSA-44", "ML-DSA-65", "ML-DSA-87", "ML-KEM-512", "ML-KEM-768", "ML-KEM-1024" };
std::string kind_of(EVP_PKEY *k) {
    if (EVP_PKEY_is_a(k, "RSA")) return "RSA";
    if (EVP_PKEY_is_a(k, "EC")) return "EC";
    if (EVP_PKEY_is_a(k, "ED25519")) return "Ed25519";
    if (EVP_PKEY_is_a(k, "X25519")) return "X25519";
    for (const char *m : ML) if (EVP_PKEY_is_a(k, m)) return m;
    return "";
}
bool is_ml(const std::string &kind) { return kind.compare(0, 3, "ML-") == 0; }
const char *ossl_name(const std::string &kind) {
    if (kind == "Ed25519") return "ED25519";
    if (kind == "X25519") return "X25519";
    for (const char *m : ML) if (kind == m) return m;
    return nullptr;
}

EVP_PKEY *decode(const Bytes &b, bool priv) {
    if (!b.n) return nullptr;
    EVP_PKEY *k = nullptr;
    OSSL_DECODER_CTX *d = OSSL_DECODER_CTX_new_for_pkey(&k, "DER", priv ? "PrivateKeyInfo" : "SubjectPublicKeyInfo", nullptr,
                                                        priv ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY, nullptr, nullptr);
    const unsigned char *p = b.p; size_t n = b.n;
    if (!d || !OSSL_DECODER_from_data(d, &p, &n) || n) { EVP_PKEY_free(k); k = nullptr; }
    OSSL_DECODER_CTX_free(d);
    return k;
}
bool octets(EVP_PKEY *k, const char *name, std::vector<uint8_t> &o) {
    size_t l = 0;
    if (!EVP_PKEY_get_octet_string_param(k, name, nullptr, 0, &l)) return false;
    o.resize(l);
    return EVP_PKEY_get_octet_string_param(k, name, o.data(), l, &l) && (o.resize(l), true);
}
/* ML-DSA and ML-KEM private keys are exported in the seed-only PKCS#8 form, as WebCrypto requires */
bool ml_pkcs8(EVP_PKEY *k, const std::string &kind, std::vector<uint8_t> &o) {
    std::vector<uint8_t> seed;
    if (!octets(k, OSSL_PKEY_PARAM_ML_DSA_SEED, seed) || (seed.size() != 32 && seed.size() != 64)) return false;
    uint8_t oid_last = kind == "ML-DSA-44" ? 0x11 : kind == "ML-DSA-65" ? 0x12 : kind == "ML-DSA-87" ? 0x13 : kind == "ML-KEM-512" ? 1 : kind == "ML-KEM-768" ? 2 : 3;
    uint8_t arc = kind[3] == 'D' ? 3 : 4, sn = (uint8_t)seed.size();
    o = { 0x30, (uint8_t)(20 + sn), 0x02, 0x01, 0x00, 0x30, 0x0b, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, arc, oid_last,
          0x04, (uint8_t)(2 + sn), 0x80, sn };
    o.insert(o.end(), seed.begin(), seed.end());
    return true;
}
bool encode(EVP_PKEY *k, bool priv, std::vector<uint8_t> &o) {
    std::string kind = kind_of(k);
    if (priv && is_ml(kind)) return ml_pkcs8(k, kind, o);
    if (kind == "EC") EVP_PKEY_set_utf8_string_param(k, OSSL_PKEY_PARAM_EC_POINT_CONVERSION_FORMAT, "uncompressed");
    OSSL_ENCODER_CTX *e = OSSL_ENCODER_CTX_new_for_pkey(k, priv ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY, "DER",
                                                        priv ? "PrivateKeyInfo" : "SubjectPublicKeyInfo", nullptr);
    unsigned char *p = nullptr; size_t l = 0; bool ok = e && OSSL_ENCODER_to_data(e, &p, &l);
    OSSL_ENCODER_CTX_free(e);
    if (ok) o.assign(p, p + l);
    OPENSSL_free(p);
    return ok;
}
v8::Local<v8::Value> der(v8::Isolate *iso, EVP_PKEY *k, bool priv) {
    std::vector<uint8_t> o;
    if (!k || !encode(k, priv, o)) return v8::Undefined(iso);
    return out(iso, o);
}
EVP_PKEY *fromdata(const char *type, OSSL_PARAM_BLD *b, int sel) {
    OSSL_PARAM *ps = OSSL_PARAM_BLD_to_param(b);
    EVP_PKEY_CTX *c = EVP_PKEY_CTX_new_from_name(nullptr, type, nullptr); EVP_PKEY *k = nullptr;
    if (!c || !ps || EVP_PKEY_fromdata_init(c) <= 0 || EVP_PKEY_fromdata(c, &k, sel, ps) <= 0) { EVP_PKEY_free(k); k = nullptr; }
    EVP_PKEY_CTX_free(c); OSSL_PARAM_free(ps); OSSL_PARAM_BLD_free(b);
    return k;
}
bool checked(EVP_PKEY *k, bool priv) {
    EVP_PKEY_CTX *c = EVP_PKEY_CTX_new_from_pkey(nullptr, k, nullptr);
    bool ok = c && (priv ? EVP_PKEY_pairwise_check(c) == 1 : EVP_PKEY_public_check(c) == 1);
    EVP_PKEY_CTX_free(c);
    return ok;
}
std::vector<uint8_t> bn_bytes(const BIGNUM *bn, size_t pad = 0) {
    std::vector<uint8_t> o(pad ? pad : (size_t)BN_num_bytes(bn));
    if (pad) BN_bn2binpad(bn, o.data(), (int)pad); else BN_bn2bin(bn, o.data());
    return o;
}

/* cDigest(hash, data) */
void cDigest(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; const EVP_MD *md = md_of(jcstr(iso, a[0])); Bytes d = in(a[1]);
    unsigned char o[EVP_MAX_MD_SIZE]; unsigned ol = 0;
    if (md && EVP_Digest(d.p, d.n, o, &ol, md, nullptr)) a.GetReturnValue().Set(out(iso, o, ol));
}
/* cHmac(hash, key, data) */
void cHmac(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; const EVP_MD *md = md_of(jcstr(iso, a[0])); Bytes k = in(a[1]), d = in(a[2]);
    unsigned char o[EVP_MAX_MD_SIZE]; unsigned ol = 0;
    if (md && HMAC(md, k.p, (int)k.n, d.p, d.n, o, &ol)) a.GetReturnValue().Set(out(iso, o, ol));
}
/* cHkdf(hash, key, salt, info, bytes): RFC 5869 by hand, since OpenSSL's HKDF refuses an empty input key */
void cHkdf(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; const EVP_MD *md = md_of(jcstr(iso, a[0])); Bytes k = in(a[1]), salt = in(a[2]), info = in(a[3]);
    size_t len = (size_t)num(iso, a[4]);
    if (!md) return;
    size_t hl = (size_t)EVP_MD_get_size(md);
    if (len > 255 * hl) return;
    unsigned char prk[EVP_MAX_MD_SIZE], t[EVP_MAX_MD_SIZE]; unsigned pl = 0, tl = 0;
    if (!HMAC(md, salt.p, (int)salt.n, k.p, k.n, prk, &pl)) return;
    std::vector<uint8_t> o; o.reserve(len + hl);
    HMAC_CTX *h = HMAC_CTX_new(); bool ok = h != nullptr;
    for (unsigned char i = 1; ok && o.size() < len; i++) {
        ok = HMAC_Init_ex(h, prk, (int)pl, md, nullptr) && HMAC_Update(h, t, tl) && HMAC_Update(h, info.p, info.n) && HMAC_Update(h, &i, 1) && HMAC_Final(h, t, &tl);
        o.insert(o.end(), t, t + tl);
    }
    HMAC_CTX_free(h);
    if (ok) { o.resize(len); a.GetReturnValue().Set(out(iso, o)); }
}
/* cPbkdf2(hash, password, salt, iterations, bytes) */
void cPbkdf2(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; const EVP_MD *md = md_of(jcstr(iso, a[0])); Bytes pw = in(a[1]), salt = in(a[2]);
    double iter = num(iso, a[3]); size_t len = (size_t)num(iso, a[4]);
    if (!md || iter < 1 || iter > 2147483647.0) return;
    std::vector<uint8_t> o(len);
    if (!len || PKCS5_PBKDF2_HMAC((const char *)pw.p, (int)pw.n, salt.p, (int)salt.n, (int)iter, md, (int)len, o.data())) a.GetReturnValue().Set(out(iso, o));
}
const EVP_CIPHER *aes(const std::string &mode, size_t kn) {
    int i = kn == 16 ? 0 : kn == 24 ? 1 : kn == 32 ? 2 : -1;
    if (i < 0) return nullptr;
    if (mode == "AES-GCM") return (const EVP_CIPHER *[]){ EVP_aes_128_gcm(), EVP_aes_192_gcm(), EVP_aes_256_gcm() }[i];
    if (mode == "AES-CBC") return (const EVP_CIPHER *[]){ EVP_aes_128_cbc(), EVP_aes_192_cbc(), EVP_aes_256_cbc() }[i];
    if (mode == "AES-CTR") return (const EVP_CIPHER *[]){ EVP_aes_128_ctr(), EVP_aes_192_ctr(), EVP_aes_256_ctr() }[i];
    if (mode == "AES-KW") return (const EVP_CIPHER *[]){ EVP_aes_128_wrap(), EVP_aes_192_wrap(), EVP_aes_256_wrap() }[i];
    return nullptr;
}
/* one cipher pass; for AEAD modes the tag is appended on encryption and taken off the end on decryption */
bool cipher(const EVP_CIPHER *ciph, bool enc, const Bytes &k, const Bytes &iv, const Bytes &d, const Bytes &ad, int tag, std::vector<uint8_t> &o) {
    EVP_CIPHER_CTX *x = EVP_CIPHER_CTX_new(); int l1 = 0, l2 = 0; bool ok = false, aead = tag > 0;
    o.assign(d.n + 64, 0);
    do {
        if (!x) break;
        EVP_CIPHER_CTX_set_flags(x, EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);
        if (!EVP_CipherInit_ex(x, ciph, nullptr, nullptr, nullptr, enc)) break;
        if (aead && !EVP_CIPHER_CTX_ctrl(x, EVP_CTRL_AEAD_SET_IVLEN, (int)iv.n, nullptr)) break;
        if (!EVP_CipherInit_ex(x, nullptr, nullptr, k.p, iv.n ? iv.p : nullptr, enc)) break;
        size_t body = d.n;
        if (aead) {
            if (!enc) { if (d.n < (size_t)tag) break; body = d.n - (size_t)tag; if (!EVP_CIPHER_CTX_ctrl(x, EVP_CTRL_AEAD_SET_TAG, tag, (void *)(d.p + body))) break; }
            if (ad.n && !EVP_CipherUpdate(x, nullptr, &l1, ad.p, (int)ad.n)) break;
        }
        if (!EVP_CipherUpdate(x, o.data(), &l1, d.p, (int)body)) break;
        if (!EVP_CipherFinal_ex(x, o.data() + l1, &l2)) break;
        size_t total = (size_t)(l1 + l2);
        if (aead && enc) { if (!EVP_CIPHER_CTX_ctrl(x, EVP_CTRL_AEAD_GET_TAG, tag, o.data() + total)) break; total += (size_t)tag; }
        o.resize(total); ok = true;
    } while (0);
    EVP_CIPHER_CTX_free(x);
    return ok;
}
/* cAes(mode, encrypt, key, iv, data, additionalData, tagBits) for AES-GCM, AES-CBC and AES-KW (iv ignored) */
void cAes(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; std::string mode = jcstr(iso, a[0]); bool enc = a[1]->BooleanValue(iso);
    Bytes k = in(a[2]), iv = in(a[3]), d = in(a[4]), ad = in(a[5]); int tag = mode == "AES-GCM" ? (int)num(iso, a[6]) / 8 : 0;
    const EVP_CIPHER *c = aes(mode, k.n); std::vector<uint8_t> o;
    if (mode == "AES-KW") iv = Bytes();
    if (c && cipher(c, enc, k, iv, d, ad, tag, o)) a.GetReturnValue().Set(out(iso, o));
}
/* cAesCtr(key, counter, lengthBits, data): only the low lengthBits of the counter block count, wrapping to zero */
void cAesCtr(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; Bytes k = in(a[0]), ctr = in(a[1]), d = in(a[3]); int bits = (int)num(iso, a[2]);
    const EVP_CIPHER *c = aes("AES-CTR", k.n);
    if (!c || ctr.n != 16 || bits < 1 || bits > 128) return;
    uint8_t block[16]; memcpy(block, ctr.p, 16);
    std::vector<uint8_t> o; o.reserve(d.n);
    size_t off = 0, blocks = (d.n + 15) / 16;
    /* blocks left before the counter part wraps: 2^bits - value of the low bits (bits >= 64: never within this call) */
    while (off < d.n) {
        size_t room = SIZE_MAX;
        if (bits < 64) {
            uint64_t v = 0;
            for (int i = 8; i < 16; i++) v = (v << 8) | block[i];
            v &= (bits == 64 ? ~0ULL : ((1ULL << bits) - 1));
            room = (size_t)((1ULL << bits) - v);
        }
        size_t n = std::min(d.n - off, room > SIZE_MAX / 16 ? SIZE_MAX : room * 16);
        std::vector<uint8_t> part;
        Bytes iv; iv.p = block; iv.n = 16; Bytes chunk; chunk.p = d.p + off; chunk.n = n;
        if (!cipher(c, true, k, iv, chunk, Bytes(), 0, part)) return;
        o.insert(o.end(), part.begin(), part.end());
        off += n;
        if (off < d.n) {   /* the counter part wrapped: zero it, keep the nonce part */
            for (int b = 0; b < bits; b++) block[15 - b / 8] &= (uint8_t)~(1u << (b % 8));
        }
    }
    (void)blocks;
    a.GetReturnValue().Set(out(iso, o));
}
/* cChaCha(encrypt, key, iv, data, additionalData) */
void cChaCha(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; bool enc = a[0]->BooleanValue(iso); Bytes k = in(a[1]), iv = in(a[2]), d = in(a[3]), ad = in(a[4]);
    std::vector<uint8_t> o;
    if (k.n == 32 && iv.n == 12 && cipher(EVP_chacha20_poly1305(), enc, k, iv, d, ad, 16, o)) a.GetReturnValue().Set(out(iso, o));
}

/* cGen(kind, modulusBits | curve, publicExponent) -> [pkcs8, spki] */
void cGen(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; std::string kind = jcstr(iso, a[0]); EVP_PKEY *k = nullptr;
    if (kind == "RSA") {
        Bytes e = in(a[2]); BIGNUM *bn = BN_bin2bn(e.p, (int)e.n, nullptr);
        EVP_PKEY_CTX *c = EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr);
        if (c && bn && EVP_PKEY_keygen_init(c) > 0 && EVP_PKEY_CTX_set_rsa_keygen_bits(c, (int)num(iso, a[1])) > 0 &&
            EVP_PKEY_CTX_set1_rsa_keygen_pubexp(c, bn) > 0) EVP_PKEY_keygen(c, &k);
        EVP_PKEY_CTX_free(c); BN_free(bn);
    } else if (kind == "EC") {
        const char *g = group_of(jcstr(iso, a[1]));
        if (g) k = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", g);
    } else if (const char *t = ossl_name(kind)) {
        k = EVP_PKEY_Q_keygen(nullptr, nullptr, t);
    }
    Key key(k);
    if (!key) return;
    auto priv = der(iso, k, true), pub = der(iso, k, false);
    if (!priv->IsUndefined() && !pub->IsUndefined()) a.GetReturnValue().Set(arr(iso, { priv, pub }));
}
/* cParse(der, isPrivate) -> [kind, modulusBits | curve | null, publicExponent | null, canonicalDer] */
void cParse(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; bool priv = a[1]->BooleanValue(iso);
    Key k(decode(in(a[0]), priv));
    if (!k) return;
    std::string kind = kind_of(k.k);
    if (kind.empty()) return;
    v8::Local<v8::Value> x = v8::Null(iso), y = v8::Null(iso);
    if (kind == "RSA") {
        BIGNUM *e = nullptr;
        if (!EVP_PKEY_get_bn_param(k.k, OSSL_PKEY_PARAM_RSA_E, &e)) return;
        x = v8::Number::New(iso, EVP_PKEY_get_bits(k.k)); y = out(iso, bn_bytes(e)); BN_free(e);
    } else if (kind == "EC") {
        const char *c = curve_of(k.k);
        if (!c) return;
        x = jstr(iso, c);
    }
    if (priv ? kind != "RSA" && !is_ml(kind) && !checked(k.k, true) : kind == "EC" && !checked(k.k, false)) return;
    auto d = der(iso, k.k, priv);
    if (d->IsUndefined()) return;
    a.GetReturnValue().Set(arr(iso, { jstr(iso, kind.c_str()), x, y, d }));
}
/* cPub(pkcs8) -> spki */
void cPub(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; Key k(decode(in(a[0]), true));
    if (k) a.GetReturnValue().Set(der(iso, k.k, false));
}
/* cRawPub(spki) -> the raw public key: EC uncompressed point, or the octets of OKP and ML keys */
void cRawPub(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; Key k(decode(in(a[0]), false)); std::vector<uint8_t> o;
    if (!k) return;
    if (EVP_PKEY_is_a(k.k, "EC")) EVP_PKEY_set_utf8_string_param(k.k, OSSL_PKEY_PARAM_EC_POINT_CONVERSION_FORMAT, "uncompressed");
    if (octets(k.k, OSSL_PKEY_PARAM_PUB_KEY, o)) a.GetReturnValue().Set(out(iso, o));
}
/* cFromRawPub(kind, curve, raw) -> spki */
void cFromRawPub(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; std::string kind = jcstr(iso, a[0]); Bytes raw = in(a[2]);
    OSSL_PARAM_BLD *b = OSSL_PARAM_BLD_new(); const char *type = ossl_name(kind);
    if (kind == "EC") { const char *g = group_of(jcstr(iso, a[1])); if (!g) { OSSL_PARAM_BLD_free(b); return; } OSSL_PARAM_BLD_push_utf8_string(b, OSSL_PKEY_PARAM_GROUP_NAME, g, 0); type = "EC"; }
    if (!type || !raw.n) { OSSL_PARAM_BLD_free(b); return; }
    OSSL_PARAM_BLD_push_octet_string(b, OSSL_PKEY_PARAM_PUB_KEY, raw.p, raw.n);
    Key k(fromdata(type, b, EVP_PKEY_PUBLIC_KEY));
    if (k && (kind != "EC" || checked(k.k, false))) a.GetReturnValue().Set(der(iso, k.k, false));
}
/* cRawPriv(pkcs8) -> EC: the private scalar padded to the field size; OKP: the private octets; ML: the seed */
void cRawPriv(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; Key k(decode(in(a[0]), true)); std::vector<uint8_t> o;
    if (!k) return;
    if (EVP_PKEY_is_a(k.k, "EC")) {
        BIGNUM *d = nullptr;
        if (EVP_PKEY_get_bn_param(k.k, OSSL_PKEY_PARAM_PRIV_KEY, &d)) { o = bn_bytes(d, field_bytes(k.k)); BN_free(d); a.GetReturnValue().Set(out(iso, o)); }
        return;
    }
    if (octets(k.k, is_ml(kind_of(k.k)) ? OSSL_PKEY_PARAM_ML_DSA_SEED : OSSL_PKEY_PARAM_PRIV_KEY, o)) a.GetReturnValue().Set(out(iso, o));
}
/* cFromRawPriv(kind, curve, raw, rawPublic | null) -> pkcs8, checking that a given public key belongs to it */
void cFromRawPriv(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; std::string kind = jcstr(iso, a[0]); Bytes raw = in(a[2]), pub = in(a[3]);
    bool has_pub = !a[3]->IsNullOrUndefined();
    OSSL_PARAM_BLD *b = OSSL_PARAM_BLD_new(); const char *type = ossl_name(kind); BIGNUM *d = nullptr;
    if (kind == "EC") {
        const char *g = group_of(jcstr(iso, a[1]));
        if (!g || !has_pub || !raw.n) { OSSL_PARAM_BLD_free(b); return; }
        d = BN_bin2bn(raw.p, (int)raw.n, nullptr);
        OSSL_PARAM_BLD_push_utf8_string(b, OSSL_PKEY_PARAM_GROUP_NAME, g, 0);
        OSSL_PARAM_BLD_push_BN(b, OSSL_PKEY_PARAM_PRIV_KEY, d);
        OSSL_PARAM_BLD_push_octet_string(b, OSSL_PKEY_PARAM_PUB_KEY, pub.p, pub.n);
        type = "EC";
    } else if (type && raw.n) {
        OSSL_PARAM_BLD_push_octet_string(b, is_ml(kind) ? OSSL_PKEY_PARAM_ML_DSA_SEED : OSSL_PKEY_PARAM_PRIV_KEY, raw.p, raw.n);
    } else { OSSL_PARAM_BLD_free(b); return; }
    Key k(fromdata(type, b, EVP_PKEY_KEYPAIR)); BN_free(d);
    if (!k) return;
    if (kind == "EC" && (!checked(k.k, true) || !checked(k.k, false))) return;
    if (kind != "EC" && has_pub) {
        std::vector<uint8_t> derived;
        if (!octets(k.k, OSSL_PKEY_PARAM_PUB_KEY, derived) || derived.size() != pub.n || memcmp(derived.data(), pub.p, pub.n)) return;
    }
    a.GetReturnValue().Set(der(iso, k.k, true));
}
/* cRsaGet(der, isPrivate) -> [n, e] or [n, e, d, p, q, dp, dq, qi] */
void cRsaGet(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; bool priv = a[1]->BooleanValue(iso); Key k(decode(in(a[0]), priv));
    if (!k || !EVP_PKEY_is_a(k.k, "RSA")) return;
    static const char *const names[] = { OSSL_PKEY_PARAM_RSA_N, OSSL_PKEY_PARAM_RSA_E, OSSL_PKEY_PARAM_RSA_D, OSSL_PKEY_PARAM_RSA_FACTOR1, OSSL_PKEY_PARAM_RSA_FACTOR2,
                                         OSSL_PKEY_PARAM_RSA_EXPONENT1, OSSL_PKEY_PARAM_RSA_EXPONENT2, OSSL_PKEY_PARAM_RSA_COEFFICIENT1 };
    int n = priv ? 8 : 2; auto ctx = iso->GetCurrentContext(); auto r = v8::Array::New(iso, n);
    for (int i = 0; i < n; i++) {
        BIGNUM *bn = nullptr;
        if (!EVP_PKEY_get_bn_param(k.k, names[i], &bn)) return;
        (void)r->Set(ctx, (uint32_t)i, out(iso, bn_bytes(bn))); BN_free(bn);
    }
    a.GetReturnValue().Set(r);
}
/* BoringSSL's RSA_check_key without primality tests (OpenSSL's key check runs Miller-Rabin on p and q,
   tens of ms per key): n = pq, de = 1 mod p-1 and q-1, dp and dq are d reduced, qi q = 1 mod p */
bool rsa_consistent(BIGNUM *const *v) {
    BIGNUM *n = v[0], *e = v[1], *d = v[2], *p = v[3], *q = v[4], *dp = v[5], *dq = v[6], *qi = v[7];
    BN_CTX *bc = BN_CTX_new(); if (!bc) return false;
    BN_CTX_start(bc);
    BIGNUM *t = BN_CTX_get(bc), *pm = BN_CTX_get(bc), *qm = BN_CTX_get(bc), *r = BN_CTX_get(bc);
    bool ok = r && BN_mul(t, p, q, bc) && !BN_cmp(t, n) && BN_sub(pm, p, BN_value_one()) && BN_sub(qm, q, BN_value_one()) &&
        BN_mod_mul(r, d, e, pm, bc) && BN_is_one(r) && BN_mod_mul(r, d, e, qm, bc) && BN_is_one(r) &&
        BN_nnmod(r, d, pm, bc) && !BN_cmp(r, dp) && BN_nnmod(r, d, qm, bc) && !BN_cmp(r, dq) &&
        BN_cmp(qi, p) < 0 && BN_mod_mul(r, qi, q, p, bc) && BN_is_one(r);
    BN_CTX_end(bc); BN_CTX_free(bc);
    return ok;
}
/* cRsaSet(n, e[, d, p, q, dp, dq, qi]) -> spki or pkcs8 after a consistency check */
void cRsaSet(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; bool priv = a.Length() >= 8;
    static const char *const names[] = { OSSL_PKEY_PARAM_RSA_N, OSSL_PKEY_PARAM_RSA_E, OSSL_PKEY_PARAM_RSA_D, OSSL_PKEY_PARAM_RSA_FACTOR1, OSSL_PKEY_PARAM_RSA_FACTOR2,
                                         OSSL_PKEY_PARAM_RSA_EXPONENT1, OSSL_PKEY_PARAM_RSA_EXPONENT2, OSSL_PKEY_PARAM_RSA_COEFFICIENT1 };
    OSSL_PARAM_BLD *b = OSSL_PARAM_BLD_new(); std::vector<BIGNUM *> bns;
    for (int i = 0; i < (priv ? 8 : 2); i++) {
        Bytes x = in(a[i]); BIGNUM *bn = BN_bin2bn(x.p, (int)x.n, nullptr); bns.push_back(bn);
        if (!x.n || !bn) { for (auto *z : bns) BN_free(z); OSSL_PARAM_BLD_free(b); return; }
        OSSL_PARAM_BLD_push_BN(b, names[i], bn);
    }
    Key k(fromdata("RSA", b, priv ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY));
    bool ok = k && (!priv || rsa_consistent(bns.data()));
    for (auto *z : bns) BN_free(z);
    if (ok) a.GetReturnValue().Set(der(iso, k.k, priv));
}
/* signatures: ECDSA as fixed-size r || s; RSA-PSS takes its salt length, ML-DSA its context */
bool sign_init(EVP_MD_CTX *m, bool sign, const std::string &alg, const EVP_MD *md, EVP_PKEY *k, const v8::Local<v8::Value> &param, v8::Isolate *iso) {
    EVP_PKEY_CTX *pc = nullptr;
    bool none = alg == "Ed25519" || is_ml(alg);
    OSSL_PARAM ps[2] = { OSSL_PARAM_END, OSSL_PARAM_END };
    Bytes ctxs = in(param);
    if (is_ml(alg) && ctxs.n) ps[0] = OSSL_PARAM_construct_octet_string(OSSL_SIGNATURE_PARAM_CONTEXT_STRING, (void *)ctxs.p, ctxs.n);
    int r = sign ? EVP_DigestSignInit_ex(m, &pc, none ? nullptr : EVP_MD_get0_name(md), nullptr, nullptr, k, ps)
                 : EVP_DigestVerifyInit_ex(m, &pc, none ? nullptr : EVP_MD_get0_name(md), nullptr, nullptr, k, ps);
    if (r <= 0) return false;
    if (alg == "RSA-PSS")
        return EVP_PKEY_CTX_set_rsa_padding(pc, RSA_PKCS1_PSS_PADDING) > 0 && EVP_PKEY_CTX_set_rsa_mgf1_md(pc, md) > 0 &&
               EVP_PKEY_CTX_set_rsa_pss_saltlen(pc, (int)num(iso, param)) > 0;
    if (alg == "RSASSA-PKCS1-v1_5") return EVP_PKEY_CTX_set_rsa_padding(pc, RSA_PKCS1_PADDING) > 0;
    return true;
}
bool kind_matches(EVP_PKEY *k, const std::string &alg) {
    std::string kind = kind_of(k);
    return alg == "ECDSA" ? kind == "EC" : alg.compare(0, 3, "RSA") == 0 ? kind == "RSA" : kind == alg;
}
/* cSign(alg, hash, pkcs8, data, saltLength | context) */
void cSign(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; std::string alg = jcstr(iso, a[0]); const EVP_MD *md = md_of(jcstr(iso, a[1]));
    Key k(decode(in(a[2]), true)); Bytes d = in(a[3]);
    if (!k || !kind_matches(k.k, alg) || (!md && (alg == "ECDSA" || alg.compare(0, 3, "RSA") == 0))) return;
    EVP_MD_CTX *m = EVP_MD_CTX_new(); size_t l = 0; std::vector<uint8_t> sig; bool ok = false;
    if (m && sign_init(m, true, alg, md, k.k, a[4], iso) && EVP_DigestSign(m, nullptr, &l, d.p, d.n) > 0) {
        sig.resize(l);
        ok = EVP_DigestSign(m, sig.data(), &l, d.p, d.n) > 0;
        sig.resize(l);
    }
    EVP_MD_CTX_free(m);
    if (!ok) return;
    if (alg == "ECDSA") {
        const unsigned char *q = sig.data(); ECDSA_SIG *es = d2i_ECDSA_SIG(nullptr, &q, (long)sig.size());
        if (!es) return;
        size_t fb = field_bytes(k.k); const BIGNUM *r, *s2; ECDSA_SIG_get0(es, &r, &s2);
        std::vector<uint8_t> o = bn_bytes(r, fb), so = bn_bytes(s2, fb);
        o.insert(o.end(), so.begin(), so.end()); ECDSA_SIG_free(es);
        sig = o;
    }
    a.GetReturnValue().Set(out(iso, sig));
}
/* cVerify(alg, hash, spki, data, signature, saltLength | context) -> boolean, or undefined if the key can't verify */
void cVerify(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; std::string alg = jcstr(iso, a[0]); const EVP_MD *md = md_of(jcstr(iso, a[1]));
    Key k(decode(in(a[2]), false)); Bytes d = in(a[3]), sig = in(a[4]);
    if (!k || !kind_matches(k.k, alg) || (!md && (alg == "ECDSA" || alg.compare(0, 3, "RSA") == 0))) return;
    std::vector<uint8_t> dsig;
    if (alg == "ECDSA") {
        size_t fb = field_bytes(k.k);
        if (sig.n != 2 * fb) { a.GetReturnValue().Set(false); return; }
        ECDSA_SIG *es = ECDSA_SIG_new();
        ECDSA_SIG_set0(es, BN_bin2bn(sig.p, (int)fb, nullptr), BN_bin2bn(sig.p + fb, (int)fb, nullptr));
        unsigned char *p = nullptr; int dl = i2d_ECDSA_SIG(es, &p); ECDSA_SIG_free(es);
        if (dl <= 0) { a.GetReturnValue().Set(false); return; }
        dsig.assign(p, p + dl); OPENSSL_free(p);
        sig.p = dsig.data(); sig.n = dsig.size();
    }
    EVP_MD_CTX *m = EVP_MD_CTX_new();
    bool ok = m && sign_init(m, false, alg, md, k.k, a[5], iso) && EVP_DigestVerify(m, sig.p, sig.n, d.p, d.n) == 1;
    EVP_MD_CTX_free(m);
    a.GetReturnValue().Set(ok);
}
/* cOaep(encrypt, hash, der, label, data): encrypt with an spki, decrypt with a pkcs8 */
void cOaep(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; bool enc = a[0]->BooleanValue(iso); const EVP_MD *md = md_of(jcstr(iso, a[1]));
    Key k(decode(in(a[2]), !enc)); Bytes label = in(a[3]), d = in(a[4]);
    if (!k || !md || !EVP_PKEY_is_a(k.k, "RSA")) return;
    EVP_PKEY_CTX *c = EVP_PKEY_CTX_new_from_pkey(nullptr, k.k, nullptr); size_t l = 0; std::vector<uint8_t> o; bool ok = false;
    unsigned char *lab = label.n ? (unsigned char *)OPENSSL_memdup(label.p, label.n) : nullptr;
    if (c && (enc ? EVP_PKEY_encrypt_init(c) : EVP_PKEY_decrypt_init(c)) > 0 && EVP_PKEY_CTX_set_rsa_padding(c, RSA_PKCS1_OAEP_PADDING) > 0 &&
        EVP_PKEY_CTX_set_rsa_oaep_md(c, md) > 0 && EVP_PKEY_CTX_set_rsa_mgf1_md(c, md) > 0 &&
        (!lab || EVP_PKEY_CTX_set0_rsa_oaep_label(c, lab, (int)label.n) > 0)) {
        lab = nullptr;   /* owned by the context now */
        if ((enc ? EVP_PKEY_encrypt(c, nullptr, &l, d.p, d.n) : EVP_PKEY_decrypt(c, nullptr, &l, d.p, d.n)) > 0) {
            o.resize(l);
            ok = (enc ? EVP_PKEY_encrypt(c, o.data(), &l, d.p, d.n) : EVP_PKEY_decrypt(c, o.data(), &l, d.p, d.n)) > 0;
            o.resize(l);
        }
    }
    OPENSSL_free(lab); EVP_PKEY_CTX_free(c);
    if (ok) a.GetReturnValue().Set(out(iso, o));
}
/* cDerive(pkcs8, peerSpki): ECDH and X25519 shared secrets */
void cDerive(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; Key k(decode(in(a[0]), true)), peer(decode(in(a[1]), false));
    if (!k || !peer) return;
    EVP_PKEY_CTX *c = EVP_PKEY_CTX_new_from_pkey(nullptr, k.k, nullptr); size_t l = 0; std::vector<uint8_t> o; bool ok = false;
    if (c && EVP_PKEY_derive_init(c) > 0 && EVP_PKEY_derive_set_peer(c, peer.k) > 0 && EVP_PKEY_derive(c, nullptr, &l) > 0) {
        o.resize(l); ok = EVP_PKEY_derive(c, o.data(), &l) > 0; o.resize(l);
    }
    EVP_PKEY_CTX_free(c);
    if (ok) a.GetReturnValue().Set(out(iso, o));
}
/* cEncap(spki) -> [sharedKey, ciphertext]; cDecap(pkcs8, ciphertext) -> sharedKey (ML-KEM) */
void cEncap(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; Key k(decode(in(a[0]), false));
    if (!k) return;
    EVP_PKEY_CTX *c = EVP_PKEY_CTX_new_from_pkey(nullptr, k.k, nullptr); size_t cl = 0, sl = 0; std::vector<uint8_t> ct, sk; bool ok = false;
    if (c && EVP_PKEY_encapsulate_init(c, nullptr) > 0 && EVP_PKEY_encapsulate(c, nullptr, &cl, nullptr, &sl) > 0) {
        ct.resize(cl); sk.resize(sl);
        ok = EVP_PKEY_encapsulate(c, ct.data(), &cl, sk.data(), &sl) > 0;
        ct.resize(cl); sk.resize(sl);
    }
    EVP_PKEY_CTX_free(c);
    if (ok) a.GetReturnValue().Set(arr(iso, { out(iso, sk), out(iso, ct) }));
}
void cDecap(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; Key k(decode(in(a[0]), true)); Bytes ct = in(a[1]);
    if (!k) return;
    EVP_PKEY_CTX *c = EVP_PKEY_CTX_new_from_pkey(nullptr, k.k, nullptr); size_t sl = 0; std::vector<uint8_t> sk; bool ok = false;
    if (c && EVP_PKEY_decapsulate_init(c, nullptr) > 0 && EVP_PKEY_decapsulate(c, nullptr, &sl, ct.p, ct.n) > 0) {
        sk.resize(sl); ok = EVP_PKEY_decapsulate(c, sk.data(), &sl, ct.p, ct.n) > 0; sk.resize(sl);
    }
    EVP_PKEY_CTX_free(c);
    if (ok) a.GetReturnValue().Set(out(iso, sk));
}
/* Hybrid KEMs from draft-irtf-cfrg-concrete-hybrid-kems-03 (MLKEM768-X25519 is X-Wing). The private key is the 32-byte seed;
   SHAKE256(seed) expands to the ML-KEM seed (64 bytes) and the traditional seed. The public key is ek_PQ || ek_T, the
   ciphertext ct_PQ || ct_T, and the shared secret SHA3-256(ss_PQ || ss_T || ct_T || ek_T || label). */
struct Hybrid { const char *name, *ml; size_t ek_pq, ct_pq; int nid; size_t tseed, nscalar, nelem; const char *label; };
const Hybrid HYBRIDS[] = {
    { "MLKEM768-P256", "ML-KEM-768", 1184, 1088, NID_X9_62_prime256v1, 128, 32, 65, "MLKEM768-P256" },
    { "MLKEM768-X25519", "ML-KEM-768", 1184, 1088, NID_X25519, 32, 32, 32, "\\.//^\\" },
    { "MLKEM1024-P384", "ML-KEM-1024", 1568, 1568, NID_secp384r1, 48, 48, 97, "MLKEM1024-P384" },
};
const Hybrid *hybrid_of(const std::string &n) { for (auto &h : HYBRIDS) if (n == h.name) return &h; return nullptr; }
typedef std::vector<uint8_t> Buf;
/* RandomScalar(seed) by rejection sampling for the P curves; the identity for X25519 */
bool hyb_scalar(const Hybrid &h, const uint8_t *seed, Buf &sk) {
    if (h.nid == NID_X25519) { sk.assign(seed, seed + 32); return true; }
    EC_GROUP *g = EC_GROUP_new_by_curve_name(h.nid); BIGNUM *k = BN_new(); bool ok = false;
    for (size_t at = 0; g && k && at + h.nscalar <= h.tseed && !ok; at += h.nscalar)
        ok = BN_bin2bn(seed + at, (int)h.nscalar, k) && !BN_is_zero(k) && BN_cmp(k, EC_GROUP_get0_order(g)) < 0;
    if (ok) { sk.resize(h.nscalar); ok = BN_bn2binpad(k, sk.data(), (int)h.nscalar) == (int)h.nscalar; }
    BN_clear_free(k); EC_GROUP_free(g);
    return ok;
}
/* Exp(p, sk): with p null the generator (the public element), else ElementToSharedSecret(p^sk); false for a bad element */
bool hyb_exp(const Hybrid &h, const Buf &sk, const uint8_t *p, size_t pn, Buf &o) {
    if (h.nid == NID_X25519) {
        Key a(EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, sk.data(), 32));
        if (!a) return false;
        o.resize(32); size_t l = 32;
        if (!p) return EVP_PKEY_get_raw_public_key(a.k, o.data(), &l) > 0;
        if (pn != 32) return false;
        Key b(EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, p, 32));
        EVP_PKEY_CTX *c = b ? EVP_PKEY_CTX_new(a.k, nullptr) : nullptr;
        bool ok = c && EVP_PKEY_derive_init(c) > 0 && EVP_PKEY_derive_set_peer_ex(c, b.k, 0) > 0 && EVP_PKEY_derive(c, o.data(), &l) > 0 && l == 32;
        EVP_PKEY_CTX_free(c);
        return ok;
    }
    EC_GROUP *g = EC_GROUP_new_by_curve_name(h.nid); BN_CTX *bc = BN_CTX_new();
    BIGNUM *k = BN_bin2bn(sk.data(), (int)sk.size(), nullptr), *x = BN_new();
    EC_POINT *in = g ? EC_POINT_new(g) : nullptr, *r = g ? EC_POINT_new(g) : nullptr;
    bool ok = g && bc && k && x && in && r;
    if (ok && p) ok = pn == h.nelem && p[0] == 4 && EC_POINT_oct2point(g, in, p, pn, bc) > 0 && !EC_POINT_is_at_infinity(g, in);
    if (ok) ok = (p ? EC_POINT_mul(g, r, nullptr, in, k, bc) : EC_POINT_mul(g, r, k, nullptr, nullptr, bc)) > 0 && !EC_POINT_is_at_infinity(g, r);
    if (ok && p) { o.resize(h.nscalar); ok = EC_POINT_get_affine_coordinates(g, r, x, nullptr, bc) > 0 && BN_bn2binpad(x, o.data(), (int)h.nscalar) > 0; }
    else if (ok) { o.resize(h.nelem); ok = EC_POINT_point2oct(g, r, POINT_CONVERSION_UNCOMPRESSED, o.data(), o.size(), bc) == h.nelem; }
    EC_POINT_free(in); EC_POINT_free(r); BN_clear_free(k); BN_free(x); BN_CTX_free(bc); EC_GROUP_free(g);
    return ok;
}
/* DeriveKeyPair(seed) -> the ML-KEM key, the traditional scalar and the public key */
bool hyb_derive(const Hybrid &h, const uint8_t *seed, EVP_PKEY **pq, Buf &skt, Buf &ek) {
    Buf ex(64 + h.tseed);
    EVP_MD_CTX *m = EVP_MD_CTX_new();
    bool ok = m && EVP_DigestInit_ex(m, EVP_shake256(), nullptr) > 0 && EVP_DigestUpdate(m, seed, 32) > 0 && EVP_DigestFinalXOF(m, ex.data(), ex.size()) > 0;
    EVP_MD_CTX_free(m);
    if (!ok) return false;
    EVP_PKEY_CTX *c = EVP_PKEY_CTX_new_from_name(nullptr, h.ml, nullptr);
    OSSL_PARAM ps[2] = { OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_ML_KEM_SEED, ex.data(), 64), OSSL_PARAM_END };
    ok = c && EVP_PKEY_keygen_init(c) > 0 && EVP_PKEY_CTX_set_params(c, ps) > 0 && EVP_PKEY_keygen(c, pq) > 0;
    EVP_PKEY_CTX_free(c);
    Buf ekt;
    ok = ok && octets(*pq, OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY, ek) && ek.size() == h.ek_pq && hyb_scalar(h, ex.data() + 64, skt) && hyb_exp(h, skt, nullptr, 0, ekt);
    OPENSSL_cleanse(ex.data(), ex.size());
    if (ok) ek.insert(ek.end(), ekt.begin(), ekt.end());
    return ok;
}
EVP_PKEY *hyb_pq_public(const Hybrid &h, const uint8_t *ek) {
    OSSL_PARAM_BLD *b = OSSL_PARAM_BLD_new();
    if (!b) return nullptr;
    OSSL_PARAM_BLD_push_octet_string(b, OSSL_PKEY_PARAM_PUB_KEY, ek, h.ek_pq);
    return fromdata(h.ml, b, EVP_PKEY_PUBLIC_KEY);
}
Buf hyb_combine(const Hybrid &h, const Buf &sspq, const Buf &sst, const uint8_t *ctt, const uint8_t *ekt) {
    Buf o(32); unsigned l = 32;
    EVP_MD_CTX *m = EVP_MD_CTX_new();
    bool ok = m && EVP_DigestInit_ex(m, EVP_sha3_256(), nullptr) > 0 && EVP_DigestUpdate(m, sspq.data(), sspq.size()) > 0 &&
              EVP_DigestUpdate(m, sst.data(), sst.size()) > 0 && EVP_DigestUpdate(m, ctt, h.nelem) > 0 && EVP_DigestUpdate(m, ekt, h.nelem) > 0 &&
              EVP_DigestUpdate(m, h.label, strlen(h.label)) > 0 && EVP_DigestFinal_ex(m, o.data(), &l) > 0;
    EVP_MD_CTX_free(m);
    if (!ok) o.clear();
    return o;
}
/* cHybGen(name, seed | undefined) -> [seed, publicKey] */
void cHybGen(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; const Hybrid *h = hybrid_of(jcstr(iso, a[0])); Bytes in_seed = in(a[1]);
    if (!h || (in_seed.n && in_seed.n != 32)) return;
    uint8_t seed[32];
    if (in_seed.n) memcpy(seed, in_seed.p, 32); else if (RAND_bytes(seed, 32) <= 0) return;
    EVP_PKEY *pq = nullptr; Buf skt, ek;
    bool ok = hyb_derive(*h, seed, &pq, skt, ek);
    EVP_PKEY_free(pq); OPENSSL_cleanse(skt.data(), skt.size());
    if (ok) a.GetReturnValue().Set(arr(iso, { out(iso, seed, 32), out(iso, ek) }));
    OPENSSL_cleanse(seed, 32);
}
/* cHybCheck(name, publicKey) -> whether both component public keys are valid */
void cHybCheck(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; const Hybrid *h = hybrid_of(jcstr(iso, a[0])); Bytes ek = in(a[1]);
    bool ok = h && ek.n == h->ek_pq + h->nelem;
    if (ok) { Key pq(hyb_pq_public(*h, ek.p)); ok = !!pq; }
    if (ok && h->nid != NID_X25519) { Buf one(h->nscalar, 0), o; one.back() = 1; ok = hyb_exp(*h, one, ek.p + h->ek_pq, h->nelem, o); }
    a.GetReturnValue().Set(ok);
}
/* cHybEncap(name, publicKey) -> [sharedKey, ciphertext] */
void cHybEncap(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; const Hybrid *h = hybrid_of(jcstr(iso, a[0])); Bytes ek = in(a[1]);
    if (!h || ek.n != h->ek_pq + h->nelem) return;
    Key pq(hyb_pq_public(*h, ek.p));
    if (!pq) return;
    EVP_PKEY_CTX *c = EVP_PKEY_CTX_new_from_pkey(nullptr, pq.k, nullptr); size_t cl = h->ct_pq, sl = 32; Buf ct(cl), sspq(sl);
    bool ok = c && EVP_PKEY_encapsulate_init(c, nullptr) > 0 && EVP_PKEY_encapsulate(c, ct.data(), &cl, sspq.data(), &sl) > 0 && cl == h->ct_pq && sl == 32;
    EVP_PKEY_CTX_free(c);
    Buf tseed(h->tseed), eph, ctt, sst;
    ok = ok && RAND_bytes(tseed.data(), (int)tseed.size()) > 0 && hyb_scalar(*h, tseed.data(), eph) && hyb_exp(*h, eph, nullptr, 0, ctt) &&
         hyb_exp(*h, eph, ek.p + h->ek_pq, h->nelem, sst);
    Buf ss = ok ? hyb_combine(*h, sspq, sst, ctt.data(), ek.p + h->ek_pq) : Buf();
    OPENSSL_cleanse(tseed.data(), tseed.size()); OPENSSL_cleanse(eph.data(), eph.size());
    if (ss.empty()) return;
    ct.insert(ct.end(), ctt.begin(), ctt.end());
    a.GetReturnValue().Set(arr(iso, { out(iso, ss), out(iso, ct) }));
}
/* cHybDecap(name, seed, ciphertext) -> sharedKey */
void cHybDecap(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s; const Hybrid *h = hybrid_of(jcstr(iso, a[0])); Bytes seed = in(a[1]), ct = in(a[2]);
    if (!h || seed.n != 32 || ct.n != h->ct_pq + h->nelem) return;
    EVP_PKEY *pqk = nullptr; Buf skt, ek;
    bool ok = hyb_derive(*h, seed.p, &pqk, skt, ek);
    Key pq(pqk);
    EVP_PKEY_CTX *c = ok ? EVP_PKEY_CTX_new_from_pkey(nullptr, pq.k, nullptr) : nullptr; size_t sl = 32; Buf sspq(sl), sst;
    ok = ok && c && EVP_PKEY_decapsulate_init(c, nullptr) > 0 && EVP_PKEY_decapsulate(c, sspq.data(), &sl, ct.p, h->ct_pq) > 0 && sl == 32 &&
         hyb_exp(*h, skt, ct.p + h->ct_pq, h->nelem, sst);
    EVP_PKEY_CTX_free(c); OPENSSL_cleanse(skt.data(), skt.size());
    Buf ss = ok ? hyb_combine(*h, sspq, sst, ct.p + h->ct_pq, ek.data() + h->ek_pq) : Buf();
    if (!ss.empty()) a.GetReturnValue().Set(out(iso, ss));
}
/* cRandom(arrayBuffer, offset, length) fills the bytes in place */
void cRandom(const FCI &a) {
    auto iso = a.GetIsolate(); Scope s;
    if (!a[0]->IsArrayBuffer()) return;
    auto ab = a[0].As<v8::ArrayBuffer>(); size_t off = (size_t)num(iso, a[1]), n = (size_t)num(iso, a[2]);
    if (off > ab->ByteLength() || n > ab->ByteLength() - off) return;
    if (n) RAND_bytes((uint8_t *)ab->Data() + off, (int)n);
}
}

void js_crypto_install(v8::Local<v8::Context> ctx, v8::Local<v8::Object> target) {
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    const struct { const char *name; v8::FunctionCallback fn; } fns[] = {
        { "cDigest", cDigest }, { "cHmac", cHmac }, { "cHkdf", cHkdf }, { "cPbkdf2", cPbkdf2 }, { "cAes", cAes }, { "cAesCtr", cAesCtr },
        { "cChaCha", cChaCha }, { "cGen", cGen }, { "cParse", cParse }, { "cPub", cPub }, { "cRawPub", cRawPub }, { "cFromRawPub", cFromRawPub },
        { "cRawPriv", cRawPriv }, { "cFromRawPriv", cFromRawPriv }, { "cRsaGet", cRsaGet }, { "cRsaSet", cRsaSet }, { "cSign", cSign },
        { "cVerify", cVerify }, { "cOaep", cOaep }, { "cDerive", cDerive }, { "cEncap", cEncap }, { "cDecap", cDecap }, { "cRandom", cRandom },
        { "cHybGen", cHybGen }, { "cHybCheck", cHybCheck }, { "cHybEncap", cHybEncap }, { "cHybDecap", cHybDecap },
    };
    for (auto &f : fns) (void)target->Set(ctx, jstr(iso, f.name), v8::Function::New(ctx, f.fn).ToLocalChecked());
}
