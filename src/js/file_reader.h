#ifndef LUMEN_JS_FILE_READER_H
#define LUMEN_JS_FILE_READER_H

#include <v8.h>
#include <cstdint>
#include <string>
extern "C" {
#include "../base/encoding.h"
}

inline void jsg_file_reader_range(v8::Isolate *iso) {
    iso->ThrowException(v8::Exception::RangeError(
        v8::String::NewFromUtf8Literal(iso, "FileReader result exceeds the engine string limit")));
}

/* Only the reader's private queue dispatch calls this bridge. Both isolates
 * use kExplicit microtasks. V8 itself prevents recursively running a checkpoint
 * already in progress; exceptions/rejections retain their normal host hooks.
 * No document-owned JsCtx or worker-owned WEnv pointer is dereferenced here.
 */
inline void jsg_file_reader_checkpoint(const v8::FunctionCallbackInfo<v8::Value> &args) {
    if (!args.GetIsolate()->IsExecutionTerminating()) args.GetIsolate()->PerformMicrotaskCheckpoint();
}

/* The JavaScript factory has already copied the private Blob bytes and applied
 * Web IDL argument conversion. This helper reads only primitive strings and a
 * genuine non-shared ArrayBuffer, never a public Blob property or user getter.
 * Keeping the BackingStore reference alive also covers every V8 allocation.
 */
inline void jsg_file_reader_package(const v8::FunctionCallbackInfo<v8::Value> &args) {
    auto iso = args.GetIsolate();
    if (!args[0]->IsArrayBuffer() || !args[1]->IsString() ||
        (!args[2]->IsUndefined() && !args[2]->IsString()) || !args[3]->IsString()) {
        iso->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(iso, "Invalid internal FileReader data")));
        return;
    }
    auto buffer = args[0].As<v8::ArrayBuffer>();
    if (buffer->WasDetached()) {
        iso->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(iso, "Detached internal FileReader data")));
        return;
    }
    auto backing = buffer->GetBackingStore();
    const size_t length = buffer->ByteLength();
    const auto bytes = static_cast<const unsigned char *>(backing->Data());
    const size_t limit = static_cast<size_t>(v8::String::kMaxLength);
    v8::String::Utf8Value format(iso, args[1]);
    if (!*format) return;
    const std::string kind(*format, static_cast<size_t>(format.length()));

    if (kind == "BinaryString") {
        if (length > limit) { jsg_file_reader_range(iso); return; }
        v8::Local<v8::String> result;
        static const uint8_t empty = 0;
        if (v8::String::NewFromOneByte(iso, length ? bytes : &empty,
                v8::NewStringType::kNormal, static_cast<int>(length)).ToLocal(&result))
            args.GetReturnValue().Set(result);
        return;
    }

    v8::String::Utf8Value mime_value(iso, args[3]);
    if (!*mime_value) return;
    std::string mime(*mime_value, static_cast<size_t>(mime_value.length()));
    /* A Blob type is printable ASCII. Keep legacy/public-property mistakes
     * from injecting a non-byte MIME value across this private boundary. */
    for (unsigned char byte : mime) if (byte < 0x20 || byte > 0x7E) { mime.clear(); break; }

    if (kind == "DataURL") {
        if (mime.empty()) mime = "application/octet-stream";
        const size_t prefix_size = 5 + mime.size() + 8; // data: + type + ;base64,
        if (prefix_size > limit || length > limit ||
            (length + 2) / 3 > (limit - prefix_size) / 4) {
            jsg_file_reader_range(iso); return;
        }
        const size_t encoded_size = ((length + 2) / 3) * 4;
        std::string result;
        result.reserve(prefix_size + encoded_size);
        result = "data:"; result += mime; result += ";base64,";
        static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (size_t offset = 0; offset < length; offset += 3) {
            const size_t remaining = length - offset;
            const uint32_t value = (static_cast<uint32_t>(bytes[offset]) << 16) |
                (remaining > 1 ? static_cast<uint32_t>(bytes[offset + 1]) << 8 : 0) |
                (remaining > 2 ? static_cast<uint32_t>(bytes[offset + 2]) : 0);
            result.push_back(alphabet[(value >> 18) & 63]);
            result.push_back(alphabet[(value >> 12) & 63]);
            result.push_back(remaining > 1 ? alphabet[(value >> 6) & 63] : '=');
            result.push_back(remaining > 2 ? alphabet[value & 63] : '=');
        }
        v8::Local<v8::String> string;
        if (v8::String::NewFromOneByte(iso, reinterpret_cast<const uint8_t *>(result.data()),
                v8::NewStringType::kNormal, static_cast<int>(result.size())).ToLocal(&string))
            args.GetReturnValue().Set(string);
        return;
    }

    if (kind != "Text") {
        iso->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(iso, "Unknown internal FileReader format")));
        return;
    }
    WebEncoding encoding = WEB_ENC_UNKNOWN;
    if (args[2]->IsString()) {
        v8::String::Utf8Value label(iso, args[2]);
        if (!*label) return;
        encoding = web_encoding_label_n(*label, static_cast<size_t>(label.length()));
    }
    if (encoding == WEB_ENC_UNKNOWN) encoding = web_encoding_mime_n(mime.data(), mime.size());
    if (encoding == WEB_ENC_UNKNOWN) encoding = WEB_ENC_UTF8;
    SB output; sb_init(&output);
    const bool decoded = web_encoding_decode_n(&output, encoding,
        reinterpret_cast<const char *>(bytes), length, true, limit);
    if (!decoded) { sb_free(&output); jsg_file_reader_range(iso); return; }
    v8::Local<v8::String> result;
    const bool created = v8::String::NewFromUtf8(iso, output.s ? output.s : "",
        v8::NewStringType::kNormal, static_cast<int>(output.n)).ToLocal(&result);
    sb_free(&output);
    if (created) args.GetReturnValue().Set(result);
}

#endif
