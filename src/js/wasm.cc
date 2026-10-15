/* Feed Response chunks to V8's streaming compiler. The managed V8 handle in
 * callback data owns the compiler lifetime; no raw pointer crosses JS awaits. */
#include "js_int.h"
#include <v8-wasm.h>

namespace {
v8::Local<v8::Private> key(v8::Isolate *iso) { return v8::Private::ForApi(iso, jstr(iso, "lumen:wasm-response")); }
v8::Local<v8::Private> brand_key(v8::Isolate *iso) { return v8::Private::ForApi(iso, jstr(iso, "lumen:response-inspector")); }
void brand_response(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); auto ctx = iso->GetCurrentContext();
    v8::Local<v8::Value> inspect;
    if (a[0]->IsObject() && ctx->Global()->GetPrivate(ctx, brand_key(iso)).ToLocal(&inspect))
        (void)a[0].As<v8::Object>()->SetPrivate(ctx, brand_key(iso), inspect);
}
void inspect_response(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); auto ctx = iso->GetCurrentContext();
    v8::Local<v8::Value> inspect, result, response = a[0];
    if (response->IsObject() && !response->IsProxy() &&
        response.As<v8::Object>()->GetPrivate(ctx, brand_key(iso)).ToLocal(&inspect) && inspect->IsFunction()) {
        // The function retains its defining realm's WeakMap. This also accepts
        // genuine Responses from another same-origin frame without duck typing.
        if (inspect.As<v8::Function>()->Call(ctx, v8::Undefined(iso), 1, &response).ToLocal(&result)) a.GetReturnValue().Set(result);
        return;
    }
    iso->ThrowException(v8::Exception::TypeError(jstr(iso, "Expected a Response")));
}
void receive(const v8::FunctionCallbackInfo<v8::Value> &a) {
    if (!a[0]->IsUint8Array()) {
        a.GetIsolate()->ThrowException(v8::Exception::TypeError(jstr(a.GetIsolate(), "Wasm response chunk must be a Uint8Array")));
        return;
    }
    auto view = a[0].As<v8::Uint8Array>();
    if (view->ByteLength()) v8::WasmStreaming::Unpack(a.GetIsolate(), a.Data())->OnBytesReceived(
        static_cast<const uint8_t *>(view->Buffer()->Data()) + view->ByteOffset(), view->ByteLength());
}
void finish(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto streaming = v8::WasmStreaming::Unpack(a.GetIsolate(), a.Data());
    std::string url = jcstr(a.GetIsolate(), a[0]);
    streaming->SetUrl(url.data(), url.size());
    streaming->Finish({});
}
void abort_stream(const v8::FunctionCallbackInfo<v8::Value> &a) {
    v8::WasmStreaming::Unpack(a.GetIsolate(), a.Data())->Abort(a[0]);
}
void stream_response(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); auto ctx = iso->GetCurrentContext();
    v8::TryCatch caught(iso);
    v8::Local<v8::Value> handler, result;
    v8::Local<v8::Function> bytes, done, fail;
    if (ctx->Global()->GetPrivate(ctx, key(iso)).ToLocal(&handler) && handler->IsFunction() &&
        v8::Function::New(ctx, receive, a.Data()).ToLocal(&bytes) &&
        v8::Function::New(ctx, finish, a.Data()).ToLocal(&done) &&
        v8::Function::New(ctx, abort_stream, a.Data()).ToLocal(&fail)) {
        v8::Local<v8::Value> argv[] = {a[0], bytes, done, fail};
        if (handler.As<v8::Function>()->Call(ctx, v8::Undefined(iso), 4, argv).ToLocal(&result)) return;
    }
    auto error = caught.HasCaught() ? caught.Exception() : v8::Exception::TypeError(jstr(iso, "Wasm Response integration unavailable"));
    v8::WasmStreaming::Unpack(iso, a.Data())->Abort(error);
}
// The inspector accesses private Response state, never public property overrides.
// V8 resolves the input promise and owns import/compile options and error classes.
constexpr char consume[] = R"JS(
(inspect => async (response, receive, finish, abort) => {
    let reader;
    try {
        const info = inspect(response);
        if (info.mime.trim().toLowerCase() !== 'application/wasm') throw new TypeError('WebAssembly response requires application/wasm');
        if (info.status < 200 || info.status > 299 || info.type === 'opaque' || info.type === 'opaqueredirect') throw new TypeError('WebAssembly response was not successful');
        reader = info.take();
        if (reader) for (;;) {
            const chunk = await reader.read();
            if (chunk.done) break;
            receive(chunk.value);
        }
        finish(info.url);
    } catch (error) {
        abort(error);
        if (reader) try { await reader.cancel(error); } catch (_) {}
    } finally { if (reader) try { reader.releaseLock(); } catch (_) {} }
})
)JS";
void bind_response(const v8::FunctionCallbackInfo<v8::Value> &a) {
    auto iso = a.GetIsolate(); auto ctx = iso->GetCurrentContext();
    if (!a[0]->IsFunction()) return;
    v8::Local<v8::Script> script;
    (void)ctx->Global()->SetPrivate(ctx, brand_key(iso), a[0]);
    v8::Local<v8::Value> factory, handler, inspect = v8::Function::New(ctx, inspect_response).ToLocalChecked();
    if (v8::Script::Compile(ctx, jstr(iso, consume)).ToLocal(&script) && script->Run(ctx).ToLocal(&factory) &&
        factory.As<v8::Function>()->Call(ctx, v8::Undefined(iso), 1, &inspect).ToLocal(&handler))
        (void)ctx->Global()->SetPrivate(ctx, key(iso), handler);
}
}
void js_wasm_init(v8::Isolate *iso) { iso->SetWasmStreamingCallback(stream_response); }
void js_wasm_install(v8::Local<v8::Context> ctx, v8::Local<v8::Object> native) {
    auto iso = v8::Isolate::GetCurrent();
    (void)native->Set(ctx, jstr(iso, "wasmResponse"), v8::Function::New(ctx, bind_response).ToLocalChecked());
    (void)native->Set(ctx, jstr(iso, "wasmResponseBrand"), v8::Function::New(ctx, brand_response).ToLocalChecked());
}
