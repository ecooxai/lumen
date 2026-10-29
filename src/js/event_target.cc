#include "event_target.h"
#include "js_int.h"

namespace {
using FCI = v8::FunctionCallbackInfo<v8::Value>;
v8::Local<v8::Private> brand_key(v8::Isolate *iso) {
    return v8::Private::ForApi(iso, jstr(iso, "lumen:event-target"));
}
void brand(const FCI &a) {
    auto iso = a.GetIsolate();
    if (a[0]->IsObject())
        (void)a[0].As<v8::Object>()->SetPrivate(iso->GetCurrentContext(), brand_key(iso), v8::True(iso));
}
void branded(const FCI &a) {
    auto iso = a.GetIsolate();
    a.GetReturnValue().Set(a[0]->IsObject() &&
        a[0].As<v8::Object>()->HasPrivate(iso->GetCurrentContext(), brand_key(iso)).FromMaybe(false));
}
}
void js_event_target_install(v8::Local<v8::Context> realm, v8::Local<v8::Object> bridge) {
    auto iso = v8::Isolate::GetCurrent();
    (void)bridge->Set(realm, jstr(iso, "etBrand"), v8::Function::New(realm, brand).ToLocalChecked());
    (void)bridge->Set(realm, jstr(iso, "etBranded"), v8::Function::New(realm, branded).ToLocalChecked());
}
