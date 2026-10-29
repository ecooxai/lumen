#include "js_int.h"
#include "clone.h"
#include <limits>

namespace {
v8::Local<v8::Private> error_key(v8::Isolate *iso) {
    return v8::Private::ForApi(iso, jstr(iso, "lumen:clone-error"));
}
/* Platform objects implemented in the prelude (CryptoKey): [prototype, fromRecord]. Such an object carries
   its record of plain values (cloneRec) and travels as a nested serialization of it. V8 forbids script in
   the serializer's callbacks, so the record is set up front and fromRecord runs after deserialization. */
v8::Local<v8::Private> host_key(v8::Isolate *iso) {
    return v8::Private::ForApi(iso, jstr(iso, "lumen:clone-host"));
}
v8::Local<v8::Array> host_types(v8::Isolate *iso) {
    auto ctx = iso->GetCurrentContext();
    v8::Local<v8::Value> v;
    if (ctx->Global()->GetPrivate(ctx, host_key(iso)).ToLocal(&v) && v->IsArray()) return v.As<v8::Array>();
    return v8::Array::New(iso);
}
v8::Local<v8::Private> rec_key(v8::Isolate *iso) {
    return v8::Private::ForApi(iso, jstr(iso, "lumen:clone-rec"));
}
/* The JS DOMException interface needs unforgeable state shared by all realms
   of an isolate. Prelude-local WeakMaps would reject a genuine foreign-realm
   exception, and public name/message properties can be shadowed by authors. */
v8::Local<v8::Private> domexception_key(v8::Isolate *iso) {
    return v8::Private::ForApi(iso, jstr(iso, "lumen:domexception-state"));
}
v8::Local<v8::Private> domexception_stack_key(v8::Isolate *iso) {
    return v8::Private::ForApi(iso, jstr(iso, "lumen:domexception-stack"));
}
v8::Local<v8::Private> domexception_stack_error_key(v8::Isolate *iso) {
    return v8::Private::ForApi(iso, jstr(iso, "lumen:domexception-stack-error-constructor"));
}
v8::MaybeLocal<v8::Value> domexception_stack_value(v8::Isolate *iso, v8::Local<v8::Context> ctx, v8::Local<v8::Object> object) {
    v8::Local<v8::Value> stored;
    if (!object->GetPrivate(ctx, domexception_stack_key(iso)).ToLocal(&stored)) return {};
    // A constructor snapshot owns the original native stack accessor and data.
    // A deserialized exception stores its already-formatted stack string here.
    if (stored->IsObject()) {
        // Error.prepareStackTrace is an author extension. Do not invoke it as
        // an incidental clone side effect. Preserve the ordinary native stack,
        // but omit optional stack data when a custom formatter/accessor exists.
        v8::Local<v8::Value> ctor;
        if (!object->GetPrivate(ctx, domexception_stack_error_key(iso)).ToLocal(&ctor)) return {};
        if (!ctor->IsFunction()) return v8::Undefined(iso);
        for (auto current = ctor; current->IsObject(); current = current.As<v8::Object>()->GetPrototype()) {
            if (current->IsProxy()) return v8::Undefined(iso);
            v8::Local<v8::Value> descriptor;
            if (!current.As<v8::Object>()->GetOwnPropertyDescriptor(ctx, jstr(iso, "prepareStackTrace")).ToLocal(&descriptor)) return {};
            if (!descriptor->IsObject()) continue;
            auto record = descriptor.As<v8::Object>();
            if (!record->HasOwnProperty(ctx, jstr(iso, "value")).FromMaybe(false)) return v8::Undefined(iso);
            v8::Local<v8::Value> formatter;
            if (!record->Get(ctx, jstr(iso, "value")).ToLocal(&formatter)) return {};
            if (formatter->IsObject()) return v8::Undefined(iso);
            break;
        }
        auto captured = stored.As<v8::Object>();
        bool labeled;
        if (!captured->HasOwnProperty(ctx, jstr(iso, "message")).To(&labeled)) return {};
        if (!labeled) {
            // The unexposed snapshot needs labels only when its stack is used.
            // Read associated data, never the original object's public getters.
            v8::Local<v8::Value> state, message, name;
            if (!object->GetPrivate(ctx, domexception_key(iso)).ToLocal(&state) || !state->IsArray() ||
                !state.As<v8::Array>()->Get(ctx, 0).ToLocal(&message) ||
                !state.As<v8::Array>()->Get(ctx, 1).ToLocal(&name)) return {};
            if (!captured->DefineOwnProperty(ctx, jstr(iso, "message"), message, v8::DontEnum).FromMaybe(false) ||
                !captured->DefineOwnProperty(ctx, jstr(iso, "name"), name, v8::DontEnum).FromMaybe(false)) return {};
        }
        v8::Local<v8::Value> stack;
        if (!captured->Get(ctx, jstr(iso, "stack")).ToLocal(&stack)) return {};
        // Optional stack data is a string or absent, never an author object.
        return stack->IsString() ? stack : v8::Local<v8::Value>(v8::Undefined(iso));
    }
    return stored;
}
void domexception_stack(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); auto ctx = iso->GetCurrentContext();
    if (!a.Length() || !a[0]->IsObject()) return;
    auto object = a[0].As<v8::Object>();
    if (a.Length() > 1) {
        if (a[1]->IsString() || a[1]->IsUndefined()) (void)object->SetPrivate(ctx, domexception_stack_key(iso), a[1]);
        return;
    }
    v8::Local<v8::Value> stack;
    if (domexception_stack_value(iso, ctx, object).ToLocal(&stack)) a.GetReturnValue().Set(stack);
}
void domexception_state(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate();
    auto ctx = iso->GetCurrentContext();
    if (!a.Length() || !a[0]->IsObject()) return;
    auto obj = a[0].As<v8::Object>();
    if (a.Length() >= 3) {
        if (!a[1]->IsString() || !a[2]->IsString()) {
            iso->ThrowException(v8::Exception::TypeError(jstr(iso, "Invalid DOMException state")));
            return;
        }
        const bool from_clone = a.Length() > 4;
        if (from_clone && !a[4]->IsString() && !a[4]->IsUndefined()) {
            iso->ThrowException(v8::Exception::TypeError(jstr(iso, "Invalid DOMException stack")));
            return;
        }
        // Snapshot the pristine Error before adding our private state. A clone
        // already has associated stack data and needs no discarded snapshot.
        v8::Local<v8::Object> captured;
        if (!from_clone) captured = obj->Clone();
        v8::Local<v8::Value> fields[] = { a[1], a[2] };
        auto record = v8::Array::New(iso, fields, 2);
        if (!obj->SetPrivate(ctx, domexception_key(iso), record).FromMaybe(false)) return;
        if (from_clone) {
            if (obj->SetPrivate(ctx, domexception_stack_key(iso), a[4]).FromMaybe(false)) a.GetReturnValue().Set(record);
            return;
        }
        if (!obj->SetPrivate(ctx, domexception_stack_key(iso), captured).FromMaybe(false)) return;
        if (a.Length() > 3 && a[3]->IsFunction())
            (void)obj->SetPrivate(ctx, domexception_stack_error_key(iso), a[3]);
        a.GetReturnValue().Set(record);
        return;
    }
    v8::Local<v8::Value> record;
    if (obj->GetPrivate(ctx, domexception_key(iso)).ToLocal(&record)) a.GetReturnValue().Set(record);
}
v8::Local<v8::Private> domexception_proto_key(v8::Isolate *iso) {
    return v8::Private::ForApi(iso, jstr(iso, "lumen:domexception-prototype"));
}
constexpr uint32_t HOST_RECORD = 1;
constexpr uint32_t HOST_DOMEXCEPTION = 2;
void clone_error(v8::Isolate *iso, v8::Local<v8::String> message) {
    auto ctx = iso->GetCurrentContext();
    v8::Local<v8::Value> ctor;
    v8::Local<v8::Object> error;
    v8::Local<v8::Value> args[] = {message, jstr(iso, "DataCloneError")};
    if (ctx->Global()->GetPrivate(ctx, error_key(iso)).ToLocal(&ctor) && ctor->IsFunction() &&
        ctor.As<v8::Function>()->NewInstance(ctx, 2, args).ToLocal(&error)) iso->ThrowException(error);
    else iso->ThrowException(v8::Exception::Error(message));
}
bool serialize_into(v8::Isolate *, v8::Local<v8::Context>, v8::Local<v8::Value>, JsSerializedValue &);
class Serializer final : public v8::ValueSerializer::Delegate {
    v8::Isolate *iso_;
    JsSerializedValue &out_;
    v8::Local<v8::Array> types_;
public:
    v8::ValueSerializer *ser = nullptr;
    std::vector<v8::Global<v8::Object>> domexceptions;
    Serializer(v8::Isolate *iso, JsSerializedValue &out) : iso_(iso), out_(out), types_(host_types(iso)) {}
    void ThrowDataCloneError(v8::Local<v8::String> message) override { clone_error(iso_, message); }
    bool HasCustomHostObject(v8::Isolate *) override { return true; }
    int type_of(v8::Local<v8::Object> o) {
        auto ctx = iso_->GetCurrentContext();
        v8::Local<v8::Value> proto = o->GetPrototype(), t, p;
        for (uint32_t i = 0; i < types_->Length(); i++)
            if (types_->Get(ctx, i).ToLocal(&t) && t->IsArray() && t.As<v8::Array>()->Get(ctx, 0).ToLocal(&p) && p->StrictEquals(proto)) return (int)i;
        return -1;
    }
    v8::Maybe<bool> IsHostObject(v8::Isolate *, v8::Local<v8::Object> o) override {
        v8::Local<v8::Value> state;
        if (!o->GetPrivate(iso_->GetCurrentContext(), domexception_key(iso_)).ToLocal(&state)) return v8::Nothing<bool>();
        return v8::Just(o->InternalFieldCount() > 0 || state->IsArray() || type_of(o) >= 0);
    }
    v8::Maybe<bool> WriteHostObject(v8::Isolate *iso, v8::Local<v8::Object> o) override {
        auto ctx = iso->GetCurrentContext();
        v8::Local<v8::Value> state;
        if (!o->GetPrivate(ctx, domexception_key(iso)).ToLocal(&state)) return v8::Nothing<bool>();
        if (state->IsArray()) {
            if (!ser || domexceptions.size() >= static_cast<size_t>(std::numeric_limits<int>::max())) return v8::Nothing<bool>();
            JsSerializedValue inner;
            if (!serialize_into(iso, ctx, state, inner) || inner.size > std::numeric_limits<uint32_t>::max()) return v8::Nothing<bool>();
            ser->WriteUint32(HOST_DOMEXCEPTION);
            ser->WriteUint32(static_cast<uint32_t>(domexceptions.size()));
            ser->WriteUint32(static_cast<uint32_t>(inner.size));
            ser->WriteRawBytes(inner.bytes.get(), inner.size);
            domexceptions.emplace_back(iso, o);
            return v8::Just(true);
        }
        int i = type_of(o);
        v8::Local<v8::Value> rec;
        if (i < 0 || !ser || !o->GetPrivate(ctx, rec_key(iso)).ToLocal(&rec) || rec->IsUndefined()) {
            clone_error(iso, jstr(iso, "An object could not be cloned."));
            return v8::Nothing<bool>();
        }
        JsSerializedValue inner;
        if (!serialize_into(iso, ctx, rec, inner)) return v8::Nothing<bool>();
        v8::Local<v8::Value> t, id;   /* the type travels as its stable id: realms register types in different orders */
        uint32_t tid = (uint32_t)i;
        if (types_->Get(ctx, i).ToLocal(&t) && t.As<v8::Array>()->Get(ctx, 2).ToLocal(&id) && id->IsUint32()) tid = id.As<v8::Uint32>()->Value();
        ser->WriteUint32(HOST_RECORD); ser->WriteUint32(tid); ser->WriteUint32((uint32_t)inner.size);
        ser->WriteRawBytes(inner.bytes.get(), inner.size);
        return v8::Just(true);
    }
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
    v8::ValueDeserializer *des = nullptr;
    struct Pending { v8::Global<v8::Object> obj; v8::Global<v8::Value> rec; uint32_t type; };
    std::vector<Pending> pending;   /* host objects to initialize from their records once the value is read */
    std::vector<v8::Global<v8::Object>> domexceptions;
    const char *error = nullptr;    /* a DataCloneError to throw once script may run again */
    v8::MaybeLocal<v8::Object> fail(v8::Isolate *iso, const char *message) {
        error = message;
        iso->ThrowException(v8::Exception::Error(jstr(iso, message)));   /* a native error: constructing DOMException is script */
        return {};
    }
    explicit Deserializer(const JsSerializedValue &input) : input_(input) {}
    v8::MaybeLocal<v8::Object> ReadHostObject(v8::Isolate *iso) override {
        auto ctx = iso->GetCurrentContext();
        uint32_t tag = 0, i = 0, n = 0; const void *raw = nullptr;
        v8::Local<v8::Value> t, proto, rec;
        uint32_t tid = 0;
        if (!des || !des->ReadUint32(&tag) || (tag != HOST_RECORD && tag != HOST_DOMEXCEPTION) || !des->ReadUint32(&tid) || !des->ReadUint32(&n) || !des->ReadRawBytes(n, &raw))
            return fail(iso, "Unable to deserialize cloned data.");
        if (tag == HOST_DOMEXCEPTION) {
            v8::ValueDeserializer inner(iso, static_cast<const uint8_t *>(raw), n);
            v8::Local<v8::Value> message, name;
            if (tid != domexceptions.size() || !inner.ReadHeader(ctx).FromMaybe(false) || !inner.ReadValue(ctx).ToLocal(&rec) ||
                !rec->IsArray() || rec.As<v8::Array>()->Length() != 2 || !rec.As<v8::Array>()->Get(ctx, 0).ToLocal(&message) ||
                !rec.As<v8::Array>()->Get(ctx, 1).ToLocal(&name) || !message->IsString() || !name->IsString() ||
                !ctx->Global()->GetPrivate(ctx, domexception_proto_key(iso)).ToLocal(&proto) || !proto->IsObject())
                return fail(iso, "Invalid DOMException clone record.");
            // Construct the native Error shell without invoking JavaScript in
            // this callback; this retains Error.isError while exposing IDL data.
            auto obj = v8::Exception::Error(message.As<v8::String>()).As<v8::Object>();
            if (!obj->Delete(ctx, jstr(iso, "message")).FromMaybe(false) || !obj->SetPrototype(ctx, proto).FromMaybe(false) ||
                !obj->SetPrivate(ctx, domexception_key(iso), rec).FromMaybe(false)) return {};
            domexceptions.emplace_back(iso, obj);
            return obj;
        }
        auto types = host_types(iso);
        i = types->Length();
        for (uint32_t k = 0; k < types->Length(); k++) {
            v8::Local<v8::Value> e, id;
            if (types->Get(ctx, k).ToLocal(&e) && e->IsArray() && e.As<v8::Array>()->Get(ctx, 2).ToLocal(&id) && id->IsUint32() && id.As<v8::Uint32>()->Value() == tid) { i = k; break; }
        }
        if (i == types->Length() || !types->Get(ctx, i).ToLocal(&t) || !t->IsArray() || !t.As<v8::Array>()->Get(ctx, 0).ToLocal(&proto) || !proto->IsObject()) {
            return fail(iso, "Unable to deserialize cloned data.");
        }
        v8::ValueDeserializer inner(iso, (const uint8_t *)raw, n);
        if (!inner.ReadHeader(ctx).FromMaybe(false) || !inner.ReadValue(ctx).ToLocal(&rec)) return {};
        v8::Local<v8::Object> obj = v8::Object::New(iso);
        if (!obj->SetPrototype(ctx, proto).FromMaybe(false)) return {};
        pending.push_back({ v8::Global<v8::Object>(iso, obj), v8::Global<v8::Value>(iso, rec), i });
        return obj;
    }
    v8::MaybeLocal<v8::WasmModuleObject> GetWasmModuleFromId(v8::Isolate *iso, uint32_t id) override {
        if (id >= input_.modules.size()) { fail(iso, "Invalid module reference"); return {}; }
        return v8::WasmModuleObject::FromCompiledModule(iso, input_.modules[id]);
    }
};
void register_error(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); auto ctx = iso->GetCurrentContext();
    if (a[0]->IsFunction()) {
        (void)ctx->Global()->SetPrivate(ctx, error_key(iso), a[0]);
        v8::Local<v8::Value> proto;
        if (a[0].As<v8::Object>()->Get(ctx, jstr(iso, "prototype")).ToLocal(&proto) && proto->IsObject())
            (void)ctx->Global()->SetPrivate(ctx, domexception_proto_key(iso), proto);
    }
}
void clone_module(const v8::FunctionCallbackInfo<v8::Value> &a) {
    if (!a[0]->IsWasmModuleObject()) return;
    auto iso = a.GetIsolate();
    v8::Local<v8::WasmModuleObject> copy;
    if (v8::WasmModuleObject::FromCompiledModule(iso, a[0].As<v8::WasmModuleObject>()->GetCompiledModule()).ToLocal(&copy))
        a.GetReturnValue().Set(copy);
}
void is_module(const v8::FunctionCallbackInfo<v8::Value> &a) { a.GetReturnValue().Set(a[0]->IsWasmModuleObject()); }
/* cloneHost(prototype, fromRecord(object, record), id): id names the type across realms */
void register_host(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); auto ctx = iso->GetCurrentContext();
    if (!a[0]->IsObject() || !a[1]->IsFunction()) return;
    auto types = host_types(iso);
    v8::Local<v8::Value> entry[] = { a[0], a[1], a[2]->IsUint32() ? a[2] : v8::Local<v8::Value>(v8::Integer::NewFromUnsigned(iso, types->Length())) };
    (void)types->Set(ctx, types->Length(), v8::Array::New(iso, entry, 3));
    (void)ctx->Global()->SetPrivate(ctx, host_key(iso), types);
}
/* cloneRec(object, record): the plain values the object is serialized as */
void set_record(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); auto ctx = iso->GetCurrentContext();
    if (a[0]->IsObject()) (void)a[0].As<v8::Object>()->SetPrivate(ctx, rec_key(iso), a[1]);
}
bool serialize_into(v8::Isolate *iso, v8::Local<v8::Context> ctx, v8::Local<v8::Value> value, JsSerializedValue &out) {
    // Build locally: a failed serialization must release retained native code.
    JsSerializedValue pending;
    Serializer delegate(iso, pending);
    v8::ValueSerializer serializer(iso, &delegate);
    delegate.ser = &serializer;
    serializer.WriteHeader();
    if (!serializer.WriteValue(ctx, value).FromMaybe(false)) return false;
    // Stack formatting may execute JavaScript. Collect it only after all host
    // callbacks have returned, never from WriteHostObject. The optional trailing
    // value belongs only to new tag-2 records; legacy tag-1 bytes are unchanged.
    if (!delegate.domexceptions.empty()) {
        auto stacks = v8::Array::New(iso, static_cast<int>(delegate.domexceptions.size()));
        for (uint32_t i = 0; i < delegate.domexceptions.size(); i++) {
            v8::Local<v8::Value> stack;
            if (!domexception_stack_value(iso, ctx, delegate.domexceptions[i].Get(iso)).ToLocal(&stack)) return false;
            if (!stack->IsString()) stack = v8::Undefined(iso);
            if (!stacks->Set(ctx, i, stack).FromMaybe(false)) return false;
        }
        if (!serializer.WriteValue(ctx, stacks).FromMaybe(false)) return false;
    }
    auto data = serializer.Release();
    pending.bytes.reset(data.first); pending.size = data.second;
    out = std::move(pending);
    return true;
}
}

bool js_serialize(v8::Isolate *iso, v8::Local<v8::Context> ctx, v8::Local<v8::Value> value, JsSerializedValue &out) {
    return serialize_into(iso, ctx, value, out);
}
v8::MaybeLocal<v8::Value> js_deserialize(v8::Isolate *iso, v8::Local<v8::Context> ctx, const JsSerializedValue &input) {
    Deserializer delegate(input);
    v8::ValueDeserializer deserializer(iso, input.bytes.get(), input.size, &delegate);
    delegate.des = &deserializer;
    v8::Local<v8::Value> v, exception;
    bool ok;
    {
        v8::TryCatch tc(iso);
        ok = deserializer.ReadHeader(ctx).FromMaybe(false) && deserializer.ReadValue(ctx).ToLocal(&v);
        if (!ok && tc.HasCaught()) exception = tc.Exception();
    }
    if (!ok) {
        if (delegate.error) clone_error(iso, jstr(iso, delegate.error));
        else if (!exception.IsEmpty()) iso->ThrowException(exception);
        return {};
    }
    if (!delegate.domexceptions.empty()) {
        v8::Local<v8::Value> stacks;
        if (!deserializer.ReadValue(ctx).ToLocal(&stacks) || !stacks->IsArray() ||
            stacks.As<v8::Array>()->Length() != delegate.domexceptions.size()) {
            clone_error(iso, jstr(iso, "Invalid DOMException clone stacks.")); return {};
        }
        for (uint32_t i = 0; i < delegate.domexceptions.size(); i++) {
            v8::Local<v8::Value> stack;
            if (!stacks.As<v8::Array>()->Get(ctx, i).ToLocal(&stack) || (!stack->IsString() && !stack->IsUndefined())) {
                clone_error(iso, jstr(iso, "Invalid DOMException clone stack.")); return {};
            }
            auto obj = delegate.domexceptions[i].Get(iso);
            if (!obj->SetPrivate(ctx, domexception_stack_key(iso), stack).FromMaybe(false) ||
                !obj->Delete(ctx, jstr(iso, "stack")).FromMaybe(false)) return {};
            if (stack->IsString() && !obj->DefineOwnProperty(ctx, jstr(iso, "stack"), stack, v8::DontEnum).FromMaybe(false)) return {};
        }
    }
    auto types = host_types(iso);
    for (auto &p : delegate.pending) {
        v8::Local<v8::Value> t, fn;
        if (!types->Get(ctx, p.type).ToLocal(&t) || !t->IsArray() || !t.As<v8::Array>()->Get(ctx, 1).ToLocal(&fn) || !fn->IsFunction()) return {};
        v8::Local<v8::Value> argv[] = { p.obj.Get(iso), p.rec.Get(iso) };
        if (fn.As<v8::Function>()->Call(ctx, v8::Undefined(iso), 2, argv).IsEmpty()) return {};
    }
    return v;
}
void js_clone_install(v8::Local<v8::Context> ctx, v8::Local<v8::Object> native) {
    auto iso = v8::Isolate::GetCurrent();
    (void)native->Set(ctx, jstr(iso, "cloneError"), v8::Function::New(ctx, register_error).ToLocalChecked());
    (void)native->Set(ctx, jstr(iso, "domExceptionState"), v8::Function::New(ctx, domexception_state).ToLocalChecked());
    (void)native->Set(ctx, jstr(iso, "domExceptionStack"), v8::Function::New(ctx, domexception_stack).ToLocalChecked());
    (void)native->Set(ctx, jstr(iso, "cloneWasm"), v8::Function::New(ctx, clone_module).ToLocalChecked());
    (void)native->Set(ctx, jstr(iso, "isWasmModule"), v8::Function::New(ctx, is_module).ToLocalChecked());
    (void)native->Set(ctx, jstr(iso, "cloneHost"), v8::Function::New(ctx, register_host).ToLocalChecked());
    (void)native->Set(ctx, jstr(iso, "cloneRec"), v8::Function::New(ctx, set_record).ToLocalChecked());
}
