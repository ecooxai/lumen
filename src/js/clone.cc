#include "js_int.h"
#include "clone.h"
#include <limits>

namespace {
v8::Local<v8::Private> error_key(v8::Isolate *iso) {
    return v8::Private::ForApi(iso, jstr(iso, "lumen:clone-error"));
}
void clone_error(v8::Isolate *iso, v8::Local<v8::String> message) {
    auto ctx = iso->GetCurrentContext();
    v8::Local<v8::Value> ctor;
    v8::Local<v8::Object> error;
    v8::Local<v8::Value> args[] = {message, jstr(iso, "DataCloneError")};
    if (ctx->Global()->GetPrivate(ctx, error_key(iso)).ToLocal(&ctor) && ctor->IsFunction() &&
        ctor.As<v8::Function>()->NewInstance(ctx, 2, args).ToLocal(&error)) iso->ThrowException(error);
    else iso->ThrowException(v8::Exception::Error(message));
}
class Serializer final : public v8::ValueSerializer::Delegate {
    v8::Isolate *iso_;
    JsSerializedValue &out_;
public:
    Serializer(v8::Isolate *iso, JsSerializedValue &out) : iso_(iso), out_(out) {}
    void ThrowDataCloneError(v8::Local<v8::String> message) override { clone_error(iso_, message); }
    v8::Maybe<uint32_t> GetWasmModuleTransferId(v8::Isolate *iso, v8::Local<v8::WasmModuleObject> module) override {
        if (out_.modules.size() >= std::numeric_limits<uint32_t>::max()) {
            clone_error(iso, jstr(iso, "Too many modules in message"));
            return v8::Nothing<uint32_t>();
        }
        uint32_t id = static_cast<uint32_t>(out_.modules.size());
        out_.modules.push_back(module->GetCompiledModule());
        return v8::Just(id);
    }
};
class Deserializer final : public v8::ValueDeserializer::Delegate {
    const JsSerializedValue &input_;
public:
    explicit Deserializer(const JsSerializedValue &input) : input_(input) {}
    v8::MaybeLocal<v8::WasmModuleObject> GetWasmModuleFromId(v8::Isolate *iso, uint32_t id) override {
        if (id >= input_.modules.size()) {
            clone_error(iso, jstr(iso, "Invalid module reference"));
            return {};
        }
        return v8::WasmModuleObject::FromCompiledModule(iso, input_.modules[id]);
    }
};
void register_error(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); auto ctx = iso->GetCurrentContext();
    if (a[0]->IsFunction()) (void)ctx->Global()->SetPrivate(ctx, error_key(iso), a[0]);
}
void clone_module(const v8::FunctionCallbackInfo<v8::Value> &a) {
    if (!a[0]->IsWasmModuleObject()) return;
    auto iso = a.GetIsolate();
    v8::Local<v8::WasmModuleObject> copy;
    if (v8::WasmModuleObject::FromCompiledModule(iso, a[0].As<v8::WasmModuleObject>()->GetCompiledModule()).ToLocal(&copy))
        a.GetReturnValue().Set(copy);
}
void is_module(const v8::FunctionCallbackInfo<v8::Value> &a) { a.GetReturnValue().Set(a[0]->IsWasmModuleObject()); }
}

bool js_serialize(v8::Isolate *iso, v8::Local<v8::Context> ctx, v8::Local<v8::Value> value, JsSerializedValue &out) {
    // Build locally: a failed serialization must release retained native code.
    JsSerializedValue pending;
    Serializer delegate(iso, pending);
    v8::ValueSerializer serializer(iso, &delegate);
    serializer.WriteHeader();
    if (!serializer.WriteValue(ctx, value).FromMaybe(false)) return false;
    auto data = serializer.Release();
    pending.bytes.reset(data.first); pending.size = data.second;
    out = std::move(pending);
    return true;
}
v8::MaybeLocal<v8::Value> js_deserialize(v8::Isolate *iso, v8::Local<v8::Context> ctx, const JsSerializedValue &input) {
    Deserializer delegate(input);
    v8::ValueDeserializer deserializer(iso, input.bytes.get(), input.size, &delegate);
    if (!deserializer.ReadHeader(ctx).FromMaybe(false)) return {};
    return deserializer.ReadValue(ctx);
}
void js_clone_install(v8::Local<v8::Context> ctx, v8::Local<v8::Object> native) {
    auto iso = v8::Isolate::GetCurrent();
    (void)native->Set(ctx, jstr(iso, "cloneError"), v8::Function::New(ctx, register_error).ToLocalChecked());
    (void)native->Set(ctx, jstr(iso, "cloneWasm"), v8::Function::New(ctx, clone_module).ToLocalChecked());
    (void)native->Set(ctx, jstr(iso, "isWasmModule"), v8::Function::New(ctx, is_module).ToLocalChecked());
}
