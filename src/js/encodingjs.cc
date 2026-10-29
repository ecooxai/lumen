/* Encoding natives shared by page and worker contexts (shared/encoding.js and the form/URL code):
   encLabel(label) -> canonical name | null, encUtf8(string) -> Uint8Array, encInto(string, u8) -> [read, written],
   isBufferSource(v), decInit(name, fatal, stripBOM) -> state, decChunk(state, bytes | null, last) -> string,
   decodeBytes(bytes, label) -> string (BOM sniffing first, as fetch, XHR and documents decode),
   formEncode([name, value, ...], label) -> application/x-www-form-urlencoded in that encoding,
   encodeHtml(string, label) -> ArrayBuffer of the text in that encoding, unmappable code points as &#N;. */
#include "js_int.h"
#include <cctype>
extern "C" {
#include "../base/encoding.h"
}

namespace {
typedef v8::FunctionCallbackInfo<v8::Value> FCI;

bool bytes(v8::Local<v8::Value> v, const uint8_t **p, size_t *n) {
    if (v->IsArrayBuffer()) { auto ab = v.As<v8::ArrayBuffer>(); *p = (const uint8_t *)ab->Data(); *n = ab->ByteLength(); return true; }
    if (v->IsSharedArrayBuffer()) { auto ab = v.As<v8::SharedArrayBuffer>(); *p = (const uint8_t *)ab->Data(); *n = ab->ByteLength(); return true; }
    if (v->IsArrayBufferView()) {
        auto vw = v.As<v8::ArrayBufferView>();
        *n = vw->ByteLength();
        *p = *n ? (const uint8_t *)vw->Buffer()->Data() + vw->ByteOffset() : nullptr;
        return true;
    }
    return false;
}
int label_of(v8::Isolate *iso, v8::Local<v8::Value> v) {
    std::string s = jcstr(iso, v);
    return enc_label(s.data(), s.size());
}
v8::Local<v8::String> utf8_str(v8::Isolate *iso, const char *s, size_t n) {
    return v8::String::NewFromUtf8(iso, s ? s : "", v8::NewStringType::kNormal, (int)n).FromMaybe(v8::String::Empty(iso));
}

void enc_label_fn(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate();
    int e = label_of(iso, a[0]);
    if (e < 0) a.GetReturnValue().SetNull(); else a.GetReturnValue().Set(jstr(iso, enc_name(e)));
}
void utf8_fn(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate();
    v8::Local<v8::String> s;
    if (!a[0]->ToString(iso->GetCurrentContext()).ToLocal(&s)) return;
    size_t n = s->Utf8Length(iso);
    v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(iso, n);
    if (n) s->WriteUtf8(iso, (char *)ab->Data(), n, v8::String::WriteFlags::kReplaceInvalidUtf8);
    a.GetReturnValue().Set(v8::Uint8Array::New(ab, 0, n));
}
void into_fn(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate();
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::Local<v8::String> s;
    if (!a[0]->ToString(ctx).ToLocal(&s) || !a[1]->IsUint8Array()) return;
    auto u8 = a[1].As<v8::Uint8Array>();
    size_t cap = u8->ByteLength(), read = 0, wrote = 0;
    if (cap && s->Length()) wrote = s->WriteUtf8(iso, (char *)u8->Buffer()->Data() + u8->ByteOffset(), cap, v8::String::WriteFlags::kReplaceInvalidUtf8, &read);
    v8::Local<v8::Array> r = v8::Array::New(iso, 2);
    (void)r->Set(ctx, 0, v8::Number::New(iso, (double)read));
    (void)r->Set(ctx, 1, v8::Number::New(iso, (double)wrote));
    a.GetReturnValue().Set(r);
}
void is_source_fn(const FCI &a) {
    a.GetReturnValue().Set(a[0]->IsArrayBuffer() || a[0]->IsSharedArrayBuffer() || a[0]->IsArrayBufferView());
}
void dec_init_fn(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate();
    int e = label_of(iso, a[0]);
    v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(iso, sizeof(EncDecoder));
    enc_decoder_init((EncDecoder *)ab->Data(), e < 0 ? ENCODING_UTF8 : e, a[1]->BooleanValue(iso), a[2]->BooleanValue(iso));
    a.GetReturnValue().Set(v8::Uint8Array::New(ab, 0, sizeof(EncDecoder)));
}
void decode_fn(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate();
    if (!a[0]->IsUint8Array() || a[0].As<v8::Uint8Array>()->ByteLength() != sizeof(EncDecoder)) return;
    auto st = a[0].As<v8::Uint8Array>();
    EncDecoder d;
    memcpy(&d, (char *)st->Buffer()->Data() + st->ByteOffset(), sizeof d);
    const uint8_t *p = nullptr;
    size_t n = 0;
    if (!a[1]->IsNull() && !bytes(a[1], &p, &n)) {
        iso->ThrowException(v8::Exception::TypeError(jstr(iso, "Failed to execute 'decode' on 'TextDecoder': The provided value is not of type '(ArrayBuffer or ArrayBufferView)'.")));
        return;
    }
    std::vector<uint8_t> copy(p, p + n);   /* the input may be a SharedArrayBuffer another thread writes */
    bool last = a[2]->BooleanValue(iso);
    SB out; sb_init(&out);
    bool ok = enc_decode(&d, copy.data(), copy.size(), last, &out);
    memcpy((char *)st->Buffer()->Data() + st->ByteOffset(), &d, sizeof d);
    if (!ok) {
        sb_free(&out);
        iso->ThrowException(v8::Exception::TypeError(jstr(iso, "Failed to execute 'decode' on 'TextDecoder': The encoded data was not valid.")));
        return;
    }
    a.GetReturnValue().Set(utf8_str(iso, out.s, out.n));
    sb_free(&out);
}
void decode_bytes_fn(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate();
    const uint8_t *p = nullptr;
    size_t n = 0;
    if (!bytes(a[0], &p, &n)) { a.GetReturnValue().Set(v8::String::Empty(iso)); return; }
    int e = a.Length() > 1 && !a[1]->IsNullOrUndefined() ? label_of(iso, a[1]) : -1;
    if (e < 0) e = ENCODING_UTF8;
    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) { e = ENCODING_UTF8; p += 3; n -= 3; }
    else if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) { e = ENCODING_UTF16BE; p += 2; n -= 2; }
    else if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) { e = ENCODING_UTF16LE; p += 2; n -= 2; }
    size_t on;
    char *o = enc_decode_all(e, p, n, &on);
    a.GetReturnValue().Set(utf8_str(iso, o, on));
    free(o);
}
bool form_set(unsigned char c) { return !(isalnum(c) || c == '*' || c == '-' || c == '.' || c == '_'); }
void form_encode_fn(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate();
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    if (!a[0]->IsArray()) return;
    int e = a.Length() > 1 && !a[1]->IsNullOrUndefined() ? label_of(iso, a[1]) : -1;
    e = enc_output(e < 0 ? ENCODING_UTF8 : e);
    auto list = a[0].As<v8::Array>();
    SB out; sb_init(&out);
    for (uint32_t i = 0; i + 1 < list->Length(); i += 2) {
        v8::Local<v8::Value> k, v;
        if (!list->Get(ctx, i).ToLocal(&k) || !list->Get(ctx, i + 1).ToLocal(&v)) break;
        std::string ks = jcstr(iso, k), vs = jcstr(iso, v);
        if (i) sb_putc(&out, '&');
        enc_percent_encode(e, ks.data(), ks.size(), form_set, true, &out);
        sb_putc(&out, '=');
        enc_percent_encode(e, vs.data(), vs.size(), form_set, true, &out);
    }
    a.GetReturnValue().Set(utf8_str(iso, out.s, out.n));
    sb_free(&out);
}
void encode_html_fn(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate();
    int e = a.Length() > 1 && !a[1]->IsNullOrUndefined() ? label_of(iso, a[1]) : -1;
    e = enc_output(e < 0 ? ENCODING_UTF8 : e);
    std::string s = jcstr(iso, a[0]);
    size_t n = 0;
    char *out = enc_encode_html(e, s.data(), s.size(), &n);
    auto ab = v8::ArrayBuffer::New(iso, n);
    if (n) memcpy(ab->Data(), out, n);
    free(out);
    a.GetReturnValue().Set(ab);
}
}

void js_encoding_install(v8::Local<v8::Context> ctx, v8::Local<v8::Object> target) {
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    auto fn = [&](const char *name, v8::FunctionCallback f) { (void)target->Set(ctx, jstr(iso, name), v8::Function::New(ctx, f).ToLocalChecked()); };
    fn("encLabel", enc_label_fn);
    fn("encUtf8", utf8_fn);
    fn("encInto", into_fn);
    fn("isBufferSource", is_source_fn);
    fn("decInit", dec_init_fn);
    fn("decChunk", decode_fn);
    fn("decodeBytes", decode_bytes_fn);
    fn("formEncode", form_encode_fn);
    fn("encodeHtml", encode_html_fn);
}
