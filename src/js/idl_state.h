#ifndef LUMEN_JS_IDL_STATE_H
#define LUMEN_JS_IDL_STATE_H

#include <v8.h>

/* Internal Web IDL state is shared by all realms in an isolate. V8 private
 * properties are neither public properties nor prototype-inherited brands.
 * No callback below reads a user property to recognize an instance, and Proxy
 * wrappers are rejected without running traps. The only callers are the
 * unexposed N/W closures used while installing the platform interfaces.
 */
enum JsgIDLKind { JSG_IDL_URL = 1, JSG_IDL_DOM_EXCEPTION = 2, JSG_IDL_URL_PARAMS = 3, JSG_IDL_FILE_READER = 4, JSG_IDL_FILE_READER_SYNC = 5, JSG_IDL_BLOB = 6, JSG_IDL_FILE = 7, JSG_IDL_TEXT_ENCODER_STREAM = 8, JSG_IDL_TEXT_DECODER_STREAM = 9 };

inline void jsg_idl_type_error(v8::Isolate *iso, const char *message) {
    iso->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(iso, message).ToLocalChecked()));
}

inline int jsg_idl_kind(const v8::FunctionCallbackInfo<v8::Value> &args) {
    if (!args[0]->IsInt32()) {
        jsg_idl_type_error(args.GetIsolate(), "Invalid internal Web IDL interface");
        return 0;
    }
    const int kind = args[0].As<v8::Int32>()->Value();
    if (kind < JSG_IDL_URL || kind > JSG_IDL_TEXT_DECODER_STREAM) {
        jsg_idl_type_error(args.GetIsolate(), "Invalid internal Web IDL interface");
        return 0;
    }
    return kind;
}

inline v8::Local<v8::Private> jsg_idl_state_key(v8::Isolate *iso, int kind) {
    switch (kind) {
        case JSG_IDL_URL:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.URL.state.v1"));
        case JSG_IDL_DOM_EXCEPTION:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.DOMException.state.v1"));
        case JSG_IDL_FILE_READER:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.FileReader.state.v1"));
        case JSG_IDL_FILE_READER_SYNC:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.FileReaderSync.state.v1"));
        case JSG_IDL_BLOB:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.Blob.state.v1"));
        case JSG_IDL_FILE:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.File.state.v1"));
        case JSG_IDL_TEXT_ENCODER_STREAM:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.TextEncoderStream.state.v1"));
        case JSG_IDL_TEXT_DECODER_STREAM:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.TextDecoderStream.state.v1"));
        default:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.URLSearchParams.state.v1"));
    }
}

inline v8::Local<v8::Private> jsg_idl_prototype_key(v8::Isolate *iso, int kind) {
    switch (kind) {
        case JSG_IDL_URL:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.URL.prototype.v1"));
        case JSG_IDL_DOM_EXCEPTION:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.DOMException.prototype.v1"));
        case JSG_IDL_FILE_READER:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.FileReader.prototype.v1"));
        case JSG_IDL_FILE_READER_SYNC:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.FileReaderSync.prototype.v1"));
        case JSG_IDL_BLOB:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.Blob.prototype.v1"));
        case JSG_IDL_FILE:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.File.prototype.v1"));
        case JSG_IDL_TEXT_ENCODER_STREAM:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.TextEncoderStream.prototype.v1"));
        case JSG_IDL_TEXT_DECODER_STREAM:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.TextDecoderStream.prototype.v1"));
        default:
            return v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.idl.URLSearchParams.prototype.v1"));
    }
}

inline void jsg_idl_get(const v8::FunctionCallbackInfo<v8::Value> &args) {
    auto iso = args.GetIsolate();
    const int kind = jsg_idl_kind(args);
    if (!kind || !args[1]->IsObject() || args[1]->IsProxy()) return;
    v8::Local<v8::Value> state;
    if (args[1].As<v8::Object>()->GetPrivate(iso->GetCurrentContext(), jsg_idl_state_key(iso, kind)).ToLocal(&state))
        args.GetReturnValue().Set(state);
}

inline void jsg_idl_put(const v8::FunctionCallbackInfo<v8::Value> &args) {
    auto iso = args.GetIsolate();
    const int kind = jsg_idl_kind(args);
    if (!kind) return;
    if (!args[1]->IsObject() || args[1]->IsProxy() || !args[2]->IsObject() || args[2]->IsProxy()) {
        jsg_idl_type_error(iso, "Invalid internal Web IDL state");
        return;
    }
    auto stored = args[1].As<v8::Object>()->SetPrivate(iso->GetCurrentContext(), jsg_idl_state_key(iso, kind), args[2]);
    if (stored.IsJust() && !stored.FromJust())
        jsg_idl_type_error(iso, "Cannot retain internal Web IDL state");
}

inline void jsg_idl_register(const v8::FunctionCallbackInfo<v8::Value> &args) {
    auto iso = args.GetIsolate();
    const int kind = jsg_idl_kind(args);
    if (!kind) return;
    if (!args[1]->IsObject() || args[1]->IsProxy()) {
        jsg_idl_type_error(iso, "Invalid internal Web IDL prototype");
        return;
    }
    auto context = iso->GetCurrentContext();
    auto stored = context->Global()->SetPrivate(context, jsg_idl_prototype_key(iso, kind), args[1]);
    if (stored.IsJust() && !stored.FromJust())
        jsg_idl_type_error(iso, "Cannot retain internal Web IDL prototype");
}

/* Web IDL invokes GetFunctionRealm only after its single observable prototype
 * Get returned a primitive. ECMAScript follows bound and Proxy targets, rather
 * than the allocation realm of the outer wrapper. These V8 introspection APIs
 * do not invoke traps or read the public .prototype property again.
 */
inline void jsg_idl_fallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    auto iso = args.GetIsolate();
    const int kind = jsg_idl_kind(args);
    if (!kind) return;
    v8::Local<v8::Value> target = args[1];
    for (;;) {
        if (target->IsProxy()) {
            auto proxy = target.As<v8::Proxy>();
            if (proxy->IsRevoked()) {
                jsg_idl_type_error(iso, "Cannot determine the realm of a revoked Proxy");
                return;
            }
            target = proxy->GetTarget();
            continue;
        }
        if (!target->IsFunction()) {
            jsg_idl_type_error(iso, "Expected an interface construction target");
            return;
        }
        auto bound = target.As<v8::Function>()->GetBoundFunction();
        if (bound->IsUndefined()) break;
        target = bound;
    }
    v8::Local<v8::Context> realm;
    if (!target.As<v8::Object>()->GetCreationContext(iso).ToLocal(&realm)) {
        jsg_idl_type_error(iso, "Interface construction target has no realm");
        return;
    }
    v8::Local<v8::Value> prototype;
    if (!realm->Global()->GetPrivate(realm, jsg_idl_prototype_key(iso, kind)).ToLocal(&prototype)) return;
    if (!prototype->IsObject() || prototype->IsProxy()) {
        jsg_idl_type_error(iso, "The target realm has no installed interface prototype");
        return;
    }
    args.GetReturnValue().Set(prototype);
}

#endif
