#ifndef LUMEN_JS_BLOB_STATE_H
#define LUMEN_JS_BLOB_STATE_H

#include <v8.h>

/* The same private Blob record is available to real platform methods in every
 * realm of an isolate. No registry or document-owner pointer is retained by
 * this bridge. Worker and window wrappers use identical recognition rules.
 */
inline void jsg_blob_state(const v8::FunctionCallbackInfo<v8::Value> &args) {
    auto iso = args.GetIsolate();
    auto context = iso->GetCurrentContext();
    if (!args[0]->IsObject() || args[0]->IsProxy()) return;
    auto object = args[0].As<v8::Object>();
    auto key = v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "lumen.Blob.state.v1"));
    v8::Local<v8::Value> state;
    if (!object->GetPrivate(context, key).ToLocal(&state)) return;
    if (args.Length() > 1) {
        if (!state->IsUndefined() || !args[1]->IsObject() || args[1]->IsProxy()) {
            iso->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(iso, "Invalid Blob state initialization")));
            return;
        }
        if (!object->SetPrivate(context, key, args[1]).FromMaybe(false)) return;
        state = args[1];
    }
    args.GetReturnValue().Set(state);
}

#endif
