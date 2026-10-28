#ifndef LUMEN_JS_CLONE_BRAND_H
#define LUMEN_JS_CLONE_BRAND_H

#include "idl_state.h"

/* Reuse the real Web IDL private state. Public constructor/prototype/tag
 * mutations cannot add or remove a platform instance's serialization rules.
 * The state belongs to the object and is collected with it; no native registry
 * retains removed documents or otherwise unreachable platform objects.
 */
inline v8::Maybe<bool> jsg_is_uncloneable(v8::Isolate *iso,
                                        v8::Local<v8::Context> context,
                                        v8::Local<v8::Object> object) {
    if (object->IsProxy()) return v8::Just(true);
    const int kinds[] = {JSG_IDL_URL, JSG_IDL_URL_PARAMS, JSG_IDL_FILE_READER, JSG_IDL_FILE_READER_SYNC, JSG_IDL_TEXT_ENCODER_STREAM, JSG_IDL_TEXT_DECODER_STREAM};
    for (int kind : kinds) {
        v8::Local<v8::Value> state;
        if (!object->GetPrivate(context, jsg_idl_state_key(iso, kind)).ToLocal(&state))
            return v8::Nothing<bool>();
        if (!state->IsUndefined()) return v8::Just(true);
    }
    return v8::Just(false);
}

inline v8::Local<v8::Private> jsg_clone_exception_key(v8::Isolate *iso) {
    return v8::Private::ForApi(iso,
        v8::String::NewFromUtf8Literal(iso, "lumen.clone.exception-factory.v2"));
}

/* The prelude passes an ordinary captured factory, not the public global or a
 * cast of the interface's callable Proxy. It creates the original realm's
 * genuine DOMException through that realm's reviewed Web IDL constructor.
 */
inline void jsg_init_clone_intrinsic(const v8::FunctionCallbackInfo<v8::Value> &args) {
    auto iso = args.GetIsolate();
    if (args.Length() != 1 || !args[0]->IsFunction() || args[0]->IsProxy()) {
        jsg_idl_type_error(iso, "Expected the internal clone exception factory");
        return;
    }
    auto context = iso->GetCurrentContext();
    auto key = jsg_clone_exception_key(iso);
    v8::Local<v8::Value> previous;
    if (!context->Global()->GetPrivate(context, key).ToLocal(&previous)) return;
    if (!previous->IsUndefined()) {
        jsg_idl_type_error(iso, "The clone exception factory is already installed");
        return;
    }
    auto stored = context->Global()->SetPrivate(context, key, args[0]);
    if (stored.IsJust() && !stored.FromJust())
        jsg_idl_type_error(iso, "Cannot retain the clone exception factory");
}

inline void jsg_clone_unsupported(const v8::FunctionCallbackInfo<v8::Value> &args) {
    auto iso = args.GetIsolate();
    if (!args[0]->IsObject()) { args.GetReturnValue().Set(false); return; }
    auto blocked = jsg_is_uncloneable(iso, iso->GetCurrentContext(), args[0].As<v8::Object>());
    if (blocked.IsJust()) args.GetReturnValue().Set(blocked.FromJust());
}

inline void jsg_throw_clone_exception(v8::Isolate *iso,
                                      v8::Local<v8::String> message,
                                      const char *name = "DataCloneError") {
    auto context = iso->GetCurrentContext();
    v8::Local<v8::Value> factory;
    if (!context->Global()->GetPrivate(context, jsg_clone_exception_key(iso)).ToLocal(&factory)) return;
    if (!factory->IsFunction() || factory->IsProxy()) {
        jsg_idl_type_error(iso, "Structured clone exception factory is unavailable");
        return;
    }
    v8::Local<v8::Value> arguments[] = {
        message.IsEmpty() ? v8::String::NewFromUtf8Literal(iso, "This value cannot be cloned.") : message,
        v8::String::NewFromUtf8(iso, name).ToLocalChecked(),
    };
    v8::Local<v8::Value> exception;
    if (!factory.As<v8::Function>()->Call(context, v8::Undefined(iso), 2, arguments).ToLocal(&exception)) return;
    if (!exception->IsObject()) {
        jsg_idl_type_error(iso, "Structured clone exception factory returned an invalid value");
        return;
    }
    iso->ThrowException(exception);
}

/* Preserve V8's ordinary serialization and default internal-field host-object
 * rejection. Worker delegates retain their existing WASM/SAB side data and
 * window delegates retain their byte limits, transfer and queue ownership.
 */
class JsgCloneDelegate : public v8::ValueSerializer::Delegate {
    v8::Isolate *iso_;
public:
    explicit JsgCloneDelegate(v8::Isolate *iso) : iso_(iso) {}
    void ThrowDataCloneError(v8::Local<v8::String> message) override {
        jsg_throw_clone_exception(iso_, message);
    }
    bool HasCustomHostObject(v8::Isolate *) override { return true; }
    v8::Maybe<bool> IsHostObject(v8::Isolate *iso, v8::Local<v8::Object> object) override {
        if (object->InternalFieldCount()) return v8::Just(true);
        return jsg_is_uncloneable(iso, iso->GetCurrentContext(), object);
    }
    v8::Maybe<bool> WriteHostObject(v8::Isolate *, v8::Local<v8::Object>) override {
        ThrowDataCloneError({});
        return v8::Nothing<bool>();
    }
};

#endif
