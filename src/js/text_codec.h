#ifndef LUMEN_JS_TEXT_CODEC_H
#define LUMEN_JS_TEXT_CODEC_H

#include <v8.h>
#include <string>
#include <cstring>
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

/* The opaque finite state stays in a private, GC-owned ArrayBuffer. Native
 * decoding retains no JS handles or input pointers between calls. Errors
 * return the consumed offset so the JS algorithm can retain exactly the
 * unconsumed input queue before throwing in the method's realm. */
inline void jsg_text_decode_stream(const v8::FunctionCallbackInfo<v8::Value> &args,
                                 WebEncoding encoding) {
    auto iso = args.GetIsolate();
    if (!args[3]->IsBoolean() || (!args[4]->IsNull() && !args[4]->IsArrayBuffer())) {
        iso->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(iso, "Invalid internal incremental decoder state")));
        return;
    }
    WebEncodingDecoder state{};
    v8::Local<v8::ArrayBuffer> state_buffer;
    if (args[4]->IsNull()) state_buffer = v8::ArrayBuffer::New(iso, sizeof state);
    else {
        state_buffer = args[4].As<v8::ArrayBuffer>();
        if (state_buffer->WasDetached() || state_buffer->ByteLength() != sizeof state) {
            iso->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(iso, "Invalid internal incremental decoder storage")));
            return;
        }
        memcpy(&state, state_buffer->Data(), sizeof state);
    }
    auto input = args[0].As<v8::ArrayBuffer>();
    auto backing = input->GetBackingStore();
    size_t consumed = 0;
    bool malformed = false;
    SB output; sb_init(&output);
    const bool decoded = web_encoding_decode_stream_n(&output, encoding, &state,
        static_cast<const char *>(backing->Data()), input->ByteLength(), args[3]->IsTrue(),
        static_cast<size_t>(v8::String::kMaxLength), args[2]->IsTrue(), &consumed, &malformed);
    memcpy(state_buffer->Data(), &state, sizeof state);
    const bool fatal_error = !decoded && malformed && args[2]->IsTrue();
    if (!decoded && !fatal_error) {
        sb_free(&output);
        iso->ThrowException(v8::Exception::RangeError(
            v8::String::NewFromUtf8Literal(iso, "Incremental decoded text exceeds its storage contract")));
        return;
    }
    v8::Local<v8::Value> text = v8::Undefined(iso);
    if (!fatal_error) {
        v8::Local<v8::String> value;
        if (!v8::String::NewFromUtf8(iso, output.s ? output.s : "",
                v8::NewStringType::kNormal, static_cast<int>(output.n)).ToLocal(&value)) {
            sb_free(&output); return;
        }
        text = value;
    }
    sb_free(&output);
    v8::Local<v8::Value> result[] = { text, state_buffer,
        v8::Number::New(iso, static_cast<double>(consumed)), v8::Boolean::New(iso, fatal_error) };
    args.GetReturnValue().Set(v8::Array::New(iso, result, 4));
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
    if (args.Length() > 3) { jsg_text_decode_stream(args, encoding); return; }
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
