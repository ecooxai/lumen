/* Compression natives shared by page and worker contexts (shared/compression.js): zNew(format, compress) -> stream
   for "gzip", "deflate" or "deflate-raw", or null for another format; zPush(stream, bytes | null) -> [Uint8Array of
   output, error message or null], null bytes ending the input. A decompressing stream reports corrupt data, data
   after the end and an end that cuts the data short, after the output that came before them. zlib's state is freed
   with its stream object. */
#include "js_int.h"
#include <zlib.h>
#include <vector>

namespace {
typedef v8::FunctionCallbackInfo<v8::Value> FCI;
struct ZStream {
    z_stream z = {};
    bool compress = false, ended = false;
    v8::Global<v8::Object> self;
};
const int kZMagic = 0x5a4c4942;

void zfree(ZStream *s) {
    if (s->compress) deflateEnd(&s->z); else inflateEnd(&s->z);
    delete s;
}
void zweak(const v8::WeakCallbackInfo<ZStream> &info) {
    ZStream *s = info.GetParameter();
    s->self.Reset();
    zfree(s);
}
ZStream *zget(v8::Local<v8::Value> v) {
    if (!v->IsObject()) return nullptr;
    v8::Local<v8::Object> o = v.As<v8::Object>();
    if (o->InternalFieldCount() < 2 || o->GetAlignedPointerFromInternalField(0, kTag) != &kZMagic) return nullptr;
    return static_cast<ZStream *>(o->GetAlignedPointerFromInternalField(1, kTag));
}
void throw_type(v8::Isolate *iso, const char *m) { iso->ThrowException(v8::Exception::TypeError(jstr(iso, m))); }

void z_new_fn(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate();
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    std::string f = jcstr(iso, a[0]);
    int bits = f == "gzip" ? 31 : f == "deflate" ? 15 : f == "deflate-raw" ? -15 : 0;
    if (!bits) { a.GetReturnValue().SetNull(); return; }
    auto *s = new ZStream();
    s->compress = a[1]->BooleanValue(iso);
    int r = s->compress ? deflateInit2(&s->z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, bits, 8, Z_DEFAULT_STRATEGY) : inflateInit2(&s->z, bits);
    if (r != Z_OK) { delete s; throw_type(iso, "Failed to initialize compression"); return; }
    v8::Local<v8::ObjectTemplate> t = v8::ObjectTemplate::New(iso);
    t->SetInternalFieldCount(2);
    v8::Local<v8::Object> o;
    if (!t->NewInstance(ctx).ToLocal(&o)) { zfree(s); return; }
    o->SetAlignedPointerInInternalField(0, const_cast<int *>(&kZMagic), kTag);
    o->SetAlignedPointerInInternalField(1, s, kTag);
    s->self.Reset(iso, o);
    s->self.SetWeak(s, zweak, v8::WeakCallbackType::kParameter);
    a.GetReturnValue().Set(o);
}
void z_push_fn(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate();
    ZStream *s = zget(a[0]);
    if (!s) { throw_type(iso, "Illegal invocation"); return; }
    const uint8_t *p = nullptr;
    size_t n = 0;
    bool finish = a[1]->IsNullOrUndefined();
    if (!finish) {
        if (a[1]->IsArrayBufferView()) {
            auto vw = a[1].As<v8::ArrayBufferView>();
            n = vw->ByteLength();
            if (n) p = (const uint8_t *)vw->Buffer()->Data() + vw->ByteOffset();
        } else if (a[1]->IsArrayBuffer()) {
            auto ab = a[1].As<v8::ArrayBuffer>();
            n = ab->ByteLength(); p = (const uint8_t *)ab->Data();
        } else { throw_type(iso, "The chunk is not a BufferSource"); return; }
        if (a[1]->IsArrayBufferView() && v8::Local<v8::Value>(a[1].As<v8::ArrayBufferView>()->Buffer())->IsSharedArrayBuffer()) { throw_type(iso, "The chunk is not a BufferSource"); return; }
    }
    std::vector<uint8_t> out;
    uint8_t buf[16384];
    const char *err = nullptr;
    s->z.next_in = const_cast<Bytef *>(p); s->z.avail_in = (uInt)n;
    if (s->compress) {
        if (s->ended) err = "The stream has ended";
        else for (;;) {
            s->z.next_out = buf; s->z.avail_out = sizeof buf;
            int r = deflate(&s->z, finish ? Z_FINISH : Z_NO_FLUSH);
            if (r == Z_STREAM_ERROR) { err = "Compression failed"; break; }
            out.insert(out.end(), buf, buf + (sizeof buf - s->z.avail_out));
            if (r == Z_STREAM_END) { s->ended = true; break; }
            if (s->z.avail_out != 0) break;
        }
    } else if (finish) {
        if (!s->ended) err = "The compressed data was cut short";
    } else if (n && s->ended) {
        err = "Junk found after end of compressed data.";
    } else {
        for (;;) {
            s->z.next_out = buf; s->z.avail_out = sizeof buf;
            int r = inflate(&s->z, Z_NO_FLUSH);
            if (r == Z_NEED_DICT || r == Z_DATA_ERROR || r == Z_MEM_ERROR || r == Z_STREAM_ERROR) { err = "The compressed data is corrupt"; break; }
            out.insert(out.end(), buf, buf + (sizeof buf - s->z.avail_out));
            if (r == Z_STREAM_END) {
                s->ended = true;
                if (s->z.avail_in > 0) err = "Junk found after end of compressed data.";
                break;
            }
            if (s->z.avail_in == 0 && s->z.avail_out != 0) break;
            if (r == Z_BUF_ERROR) break;
        }
    }
    s->z.next_in = nullptr; s->z.avail_in = 0;
    v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(iso, out.size());
    if (!out.empty()) memcpy(ab->Data(), out.data(), out.size());
    v8::Local<v8::Value> pair[2] = {v8::Uint8Array::New(ab, 0, out.size()), err ? v8::Local<v8::Value>(jstr(iso, err)) : v8::Null(iso)};
    a.GetReturnValue().Set(v8::Array::New(iso, pair, 2));
}
}

void js_compress_install(v8::Local<v8::Context> ctx, v8::Local<v8::Object> target) {
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    (void)target->Set(ctx, jstr(iso, "zNew"), v8::Function::New(ctx, z_new_fn).ToLocalChecked());
    (void)target->Set(ctx, jstr(iso, "zPush"), v8::Function::New(ctx, z_push_fn).ToLocalChecked());
}
