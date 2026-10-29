#include <libplatform/libplatform.h>
#include <v8.h>
#include <cstdio>
#include <cstring>
#include <memory>

static bool check_runtime(v8::Isolate *isolate) {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);
    v8::TryCatch exception(isolate);
    const char *source = R"JS(
        (() => {
            if (new Intl.DateTimeFormat('en', {timeZone: 'UTC', year: 'numeric'}).format(new Date(0)) !== '1970')
                throw new Error('ICU date formatting failed');
            if (new Intl.NumberFormat('en-US').format(1234.5) !== '1,234.5')
                throw new Error('ICU number formatting failed');
            const bytes = new Uint8Array([
                0,97,115,109,1,0,0,0,
                1,7,1,96,2,127,127,1,127,
                3,2,1,0,
                7,7,1,3,97,100,100,0,0,
                10,9,1,7,0,32,0,32,1,106,11
            ]);
            const instance = new WebAssembly.Instance(new WebAssembly.Module(bytes));
            if (instance.exports.add(20, 22) !== 42) throw new Error('Wasm execution failed');
            if (12345678901234567890n + 1n !== 12345678901234567891n)
                throw new Error('BigInt arithmetic failed');
            let jobs = 0;
            Promise.resolve().then(() => { jobs++; });
            globalThis.verifyJobs = () => jobs === 1;
            return 'Lumen Android V8 smoke passed';
        })()
    )JS";
    v8::Local<v8::String> text;
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> result;
    if (!v8::String::NewFromUtf8(isolate, source).ToLocal(&text) ||
        !v8::Script::Compile(context, text).ToLocal(&script) ||
        !script->Run(context).ToLocal(&result)) {
        v8::String::Utf8Value message(isolate, exception.Exception());
        std::fprintf(stderr, "V8 runtime check failed: %s\n", *message ? *message : "unknown exception");
        return false;
    }
    v8::String::Utf8Value message(isolate, result);
    if (!*message || std::strcmp(*message, "Lumen Android V8 smoke passed")) return false;
    isolate->PerformMicrotaskCheckpoint();
    if (!v8::String::NewFromUtf8(isolate, "verifyJobs()").ToLocal(&text) ||
        !v8::Script::Compile(context, text).ToLocal(&script) ||
        !script->Run(context).ToLocal(&result) || !result->IsTrue()) {
        std::fputs("V8 microtask check failed\n", stderr);
        return false;
    }
    std::puts(*message);
    return true;
}

int main(int argc, char **argv) {
    (void)argc;
    if (!v8::V8::InitializeICUDefaultLocation(argv[0])) {
        std::fputs("V8 embedded ICU initialization failed\n", stderr);
        return 1;
    }
    v8::V8::InitializeExternalStartupData(argv[0]);
    std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(platform.get());
    if (!v8::V8::Initialize()) return 1;
    std::unique_ptr<v8::ArrayBuffer::Allocator> allocator(v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    v8::Isolate::CreateParams parameters;
    parameters.array_buffer_allocator = allocator.get();
    v8::Isolate *isolate = v8::Isolate::New(parameters);
    bool passed = isolate && check_runtime(isolate);
    if (isolate) isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
    return passed ? 0 : 1;
}
