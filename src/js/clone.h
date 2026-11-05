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
    // Independently owned stores, never sender-isolate allocator allocations.
    // Transfer IDs preserve buffer/view identity without serializing their bytes.
    std::vector<std::shared_ptr<v8::BackingStore>> buffers;
};
bool js_serialize(v8::Isolate *, v8::Local<v8::Context>, v8::Local<v8::Value>, JsSerializedValue &);
bool js_serialize_transfer_batch(v8::Isolate *, v8::Local<v8::Context>, v8::Local<v8::Value>,
                                 v8::Local<v8::Value> converted_list, v8::Local<v8::Value> private_hooks,
                                 std::vector<JsSerializedValue> &);
void js_discard_transfer_ports(v8::Isolate *, v8::Local<v8::Context>, v8::Local<v8::Value> private_hooks);
v8::MaybeLocal<v8::Value> js_deserialize(v8::Isolate *, v8::Local<v8::Context>, const JsSerializedValue &);
void js_clone_install(v8::Local<v8::Context>, v8::Local<v8::Object>);
