#pragma once
#include <v8.h>
#include <v8-wasm.h>
#include <cstdlib>
#include <memory>
#include <vector>

// In-memory messages within a dedicated worker's agent cluster. This is not a
// persistent storage format or a channel for arbitrary cross-origin windows.
// No isolate-local handle may survive serialization or cross a thread boundary.
struct JsSerializedValue {
    std::unique_ptr<uint8_t, decltype(&std::free)> bytes{nullptr, std::free};
    size_t size = 0;
    std::vector<v8::CompiledWasmModule> modules;
};
bool js_serialize(v8::Isolate *, v8::Local<v8::Context>, v8::Local<v8::Value>, JsSerializedValue &);
v8::MaybeLocal<v8::Value> js_deserialize(v8::Isolate *, v8::Local<v8::Context>, const JsSerializedValue &);
void js_clone_install(v8::Local<v8::Context>, v8::Local<v8::Object>);
