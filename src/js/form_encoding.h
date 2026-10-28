/* Included in windows.cc's private namespace after form_navigation.h.
 * HTML form encodings share the immutable Encoding Standard tables and the
 * existing per-request form byte bound. The output belongs to this call;
 * neither a DOM wrapper nor an author-owned buffer is retained.
 */
bool form_encoding_space(unsigned char byte) {
    return byte == 0x09 || byte == 0x0A || byte == 0x0C || byte == 0x0D || byte == 0x20;
}

void n_formEncoding(const FCI &args) {
    v8::Isolate *iso = args.GetIsolate();
    args.GetReturnValue().SetNull();
    Node *form = junwrap(args[0]);
    JsCtx *source = form_owner(form);
    if (!source || (source->sandbox_flags & SB_FORMS)) return;
    WebEncoding encoding = form->doc->encoding;
    size_t length = 0;
    const char *labels = node_attr_n(form, "accept-charset", &length);
    if (labels) {
        encoding = WEB_ENC_UTF8;
        for (size_t at = 0; at < length;) {
            while (at < length && form_encoding_space((unsigned char)labels[at])) ++at;
            const size_t begin = at;
            while (at < length && !form_encoding_space((unsigned char)labels[at])) ++at;
            if (at == begin) break;
            const WebEncoding candidate = web_encoding_label_n(labels + begin, at - begin);
            if (candidate != WEB_ENC_UNKNOWN) { encoding = candidate; break; }
        }
    }
    encoding = web_encoding_output(encoding);
    if (encoding == WEB_ENC_UNKNOWN) encoding = WEB_ENC_UTF8;
    args.GetReturnValue().Set(jstr(iso, web_encoding_name(encoding)));
}

struct FormEncodingSink {
    std::string bytes;
    unsigned mode; /* 0: body bytes; 1: multipart parameter; 2: URL-encoded component. */
    bool overflow = false;
};

bool form_encoding_append(FormEncodingSink *sink, const char *bytes, size_t length) {
    if (length > form_max_bytes - sink->bytes.size()) {
        sink->overflow = true;
        return false;
    }
    sink->bytes.append(bytes, length);
    return true;
}

bool form_encoding_byte(void *opaque, unsigned char byte) {
    auto *sink = static_cast<FormEncodingSink *>(opaque);
    const bool parameter_escape = sink->mode == 1 && (byte == 0x0A || byte == 0x0D || byte == 0x22);
    const bool url_literal = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
        (byte >= '0' && byte <= '9') || byte == '*' || byte == '-' || byte == '.' || byte == '_';
    if (sink->mode == 2 && byte == 0x20) return form_encoding_append(sink, "+", 1);
    if (parameter_escape || (sink->mode == 2 && !url_literal)) {
        static const char digits[] = "0123456789ABCDEF";
        const char escaped[] = {'%', digits[byte >> 4], digits[byte & 15]};
        return form_encoding_append(sink, escaped, sizeof escaped);
    }
    const char raw = (char)byte;
    return form_encoding_append(sink, &raw, 1);
}

bool form_encoding_error(void *opaque, uint32_t point) {
    /* HTML encoding's error mode writes an ASCII decimal character reference.
     * Send those bytes through the same escaping sink. In particular URL form
     * data must percent-encode the resulting ampersand, hash and semicolon. */
    char reference[16];
    const int length = snprintf(reference, sizeof reference, "&#%u;", (unsigned)point);
    if (length < 1 || (size_t)length >= sizeof reference) return false;
    for (int index = 0; index < length; ++index)
        if (!form_encoding_byte(opaque, (unsigned char)reference[index])) return false;
    return true;
}

void n_formUtf8Length(const FCI &args) {
    v8::Isolate *iso = args.GetIsolate();
    if (!args[0]->IsString()) {
        iso->ThrowException(v8::Exception::TypeError(jstr(iso, "Invalid internal form string")));
        return;
    }
    // Count before a JS aggregate is joined; this allocates no encoded buffer.
    args.GetReturnValue().Set(static_cast<double>(args[0].As<v8::String>()->Utf8Length(iso)));
}

void n_formEncode(const FCI &args) {
    v8::Isolate *iso = args.GetIsolate();
    auto fail = [&](bool range, const char *message) {
        iso->ThrowException(range ? v8::Exception::RangeError(jstr(iso, message)) :
                                    v8::Exception::TypeError(jstr(iso, message)));
    };
    if (!args[0]->IsString() || !args[1]->IsString() || !args[2]->IsInt32()) {
        fail(false, "Invalid internal form encoding arguments"); return;
    }
    const int mode = args[2].As<v8::Int32>()->Value();
    if (mode < 0 || mode > 2) { fail(false, "Unknown internal form encoding mode"); return; }
    v8::String::Utf8Value label(iso, args[1]);
    const WebEncoding encoding = web_encoding_label_n(*label ? *label : "", (size_t)label.length());
    if (encoding == WEB_ENC_UNKNOWN) { fail(false, "Unknown internal form encoding"); return; }
    auto input = args[0].As<v8::String>();
    const size_t length = input->Utf8Length(iso);
    if (length > form_max_bytes) {
        fail(true, "Form input exceeds the request byte limit"); return;
    }
    std::string utf8(length, '\0');
    // Both selected Mac and iPad V8 15 SDKs use explicit positive write flags.
    // Omitting kNullTerminate preserves embedded NULs and writes only payload.
    const size_t written = input->WriteUtf8(iso, utf8.data(), length,
        v8::String::WriteFlags::kReplaceInvalidUtf8);
    if (written != length) { fail(false, "Invalid internal form string"); return; }
    FormEncodingSink sink{{}, (unsigned)mode};
    sink.bytes.reserve(utf8.size());
    if (!web_encoding_encode_n(encoding, utf8.data(), utf8.size(),
                                form_encoding_byte, form_encoding_error, &sink)) {
        fail(sink.overflow, sink.overflow ? "Encoded form exceeds the request byte limit" :
                                         "Unable to encode the form value");
        return;
    }
    if (mode == 2) {
        /* Percent-encoded components contain only ASCII and need no decoding. */
        args.GetReturnValue().Set(jstr(iso, sink.bytes.data(), (int)sink.bytes.size()));
    } else {
        auto buffer = v8::ArrayBuffer::New(iso, sink.bytes.size());
        if (!sink.bytes.empty()) memcpy(buffer->Data(), sink.bytes.data(), sink.bytes.size());
        args.GetReturnValue().Set(buffer);
    }
}
