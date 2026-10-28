#ifndef LUMEN_JS_TEXT_CODEC_H
#define LUMEN_JS_TEXT_CODEC_H

#include <v8.h>
#include <string>
extern "C" {
#include "../base/encoding.h"
}

/* These bridges never access public properties or retain native decoder
 * allocations. The shared JS factory owns private state in the V8 heap. */
inline void jsg_text_encoding(const v8::FunctionCallbackInfo<v8::Value> &args) {
    auto iso = args.GetIsolate();
    if (!args[0]->IsString()) return;
    v8::String::Utf8Value label(iso, args[0]);
    if (!*label) return;
    const WebEncoding encoding = web_encoding_label_n(*label, static_cast<size_t>(label.length()));
    if (encoding == WEB_ENC_UNKNOWN || encoding == WEB_ENC_REPLACEMENT) {
        args.GetReturnValue().SetNull();
        return;
    }
    std::string name(web_encoding_name(encoding));
    for (char &ch : name) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    v8::Local<v8::String> value;
    if (v8::String::NewFromUtf8(iso, name.data(), v8::NewStringType::kNormal,
            static_cast<int>(name.size())).ToLocal(&value)) args.GetReturnValue().Set(value);
}

inline void jsg_text_decode(const v8::FunctionCallbackInfo<v8::Value> &args) {
    auto iso = args.GetIsolate();
    if (!args[0]->IsArrayBuffer() || !args[1]->IsString() || !args[2]->IsBoolean()) {
        iso->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(iso, "Invalid internal text decoder data")));
        return;
    }
    auto buffer = args[0].As<v8::ArrayBuffer>();
    if (buffer->WasDetached()) {
        iso->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(iso, "Detached internal text decoder data")));
        return;
    }
    v8::String::Utf8Value label(iso, args[1]);
    if (!*label) return;
    const WebEncoding encoding = web_encoding_label_n(*label, static_cast<size_t>(label.length()));
    if (encoding == WEB_ENC_UNKNOWN || encoding == WEB_ENC_REPLACEMENT) {
        iso->ThrowException(v8::Exception::RangeError(
            v8::String::NewFromUtf8Literal(iso, "Invalid internal text decoder encoding")));
        return;
    }
    auto backing = buffer->GetBackingStore();
    SB output; sb_init(&output);
    bool malformed = false;
    const bool decoded = web_encoding_decode_checked_n(&output, encoding,
        static_cast<const char *>(backing->Data()), buffer->ByteLength(),
        false, static_cast<size_t>(v8::String::kMaxLength), args[2]->IsTrue(), &malformed);
    if (!decoded) {
        sb_free(&output);
        if (malformed && args[2]->IsTrue()) iso->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(iso, "The encoded data is not valid")));
        else iso->ThrowException(v8::Exception::RangeError(
            v8::String::NewFromUtf8Literal(iso, "Decoded text exceeds the engine string limit")));
        return;
    }
    v8::Local<v8::String> result;
    const bool created = v8::String::NewFromUtf8(iso, output.s ? output.s : "",
        v8::NewStringType::kNormal, static_cast<int>(output.n)).ToLocal(&result);
    sb_free(&output);
    if (created) args.GetReturnValue().Set(result);
}

#endif
