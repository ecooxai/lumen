#ifndef LUMEN_HEADER_BYTES_H
#define LUMEN_HEADER_BYTES_H

#include <string>
#include <cstring>
#include <v8.h>

// Fetch and XHR expose HTTP fields as ByteStrings. Their JS entry points
// validate the 0..255 range before these internal bridges are called; validate
// again before enqueue so no override can truncate a code unit into CR/LF/NUL.
// UTF-8 transcoding here would change the bytes sent over the network.
static inline bool jheader_bytes(v8::Isolate *iso, v8::Local<v8::Value> value,
                                 bool name, std::string &result) {
    auto invalid = [&]() {
        iso->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(iso, "Invalid HTTP header bytes")));
        return false;
    };
    if (!value->IsString()) return invalid();
    v8::String::Value units(iso, value);
    if (!*units || (name && units.length() == 0)) return invalid();
    result.resize((size_t)units.length());
    for (int i = 0; i < units.length(); ++i) {
        uint16_t c = (*units)[i];
        if (c > 255 || c == 0 || c == '\r' || c == '\n') return invalid();
        if (name && !((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                      (c >= 'a' && c <= 'z') || std::strchr("!#$%&'*+-.^_`|~", (int)c))) return invalid();
        result[(size_t)i] = (char)c;
    }
    return true;
}

static inline v8::Local<v8::String> jheader_string(v8::Isolate *iso, const char *value) {
    return v8::String::NewFromOneByte(iso, reinterpret_cast<const uint8_t *>(value ? value : ""),
                                    v8::NewStringType::kNormal).ToLocalChecked();
}

#endif
