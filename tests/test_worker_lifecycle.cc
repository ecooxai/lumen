/* Native resource regression for self-closing workers. Link like jsrun while
 * replacing src/js/worker.cc with this file. Including the implementation lets
 * the test check an undeliverable message queue directly instead of trusting a
 * noisy process-RSS threshold or adding a production-only diagnostics API.
 */
#include "../src/js/worker.cc"
#include <thread>
#include <fstream>
#include <iterator>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

extern "C" void font_init(void);

static int checks, failures;
#define CHECK(name, condition) do { \
    bool ok_ = (condition); ++checks; \
    printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); \
    if (!ok_) failures++; \
} while (0)

/* Hold a loopback response open until the test releases it. A successful drain
 * while this server is still holding the connection proves cancellation; it
 * cannot be explained by a server response or the ordinary 30-second timeout.
 */
struct StalledResponse {
    int listener = -1;
    unsigned port = 0;
    std::thread thread;
    std::mutex mu;
    std::condition_variable cv;
    bool entered = false, release = false;

    bool start() {
        listener = socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) return false;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) || listen(listener, 1)) return false;
        socklen_t length = sizeof address;
        if (getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length)) return false;
        port = ntohs(address.sin_port);
        thread = std::thread([this] {
            pollfd ready{listener, POLLIN, 0};
            if (poll(&ready, 1, 5000) <= 0) return;
            int client = accept(listener, nullptr, nullptr);
            if (client < 0) return;
            std::string request;
            while (request.size() < 8192 && request.find("\r\n\r\n") == std::string::npos) {
                pollfd readable{client, POLLIN, 0};
                if (poll(&readable, 1, 2000) <= 0) break;
                char bytes[1024];
                ssize_t n = recv(client, bytes, sizeof bytes, 0);
                if (n <= 0) break;
                request.append(bytes, static_cast<size_t>(n));
            }
            {
                std::unique_lock<std::mutex> lock(mu);
                entered = request.find("\r\n\r\n") != std::string::npos;
                cv.notify_all();
                if (entered) cv.wait(lock, [this] { return release; });
            }
            close(client);
        });
        return true;
    }
    bool wait_entered() {
        std::unique_lock<std::mutex> lock(mu);
        return cv.wait_for(lock, std::chrono::seconds(5), [this] { return entered; });
    }
    void stop() {
        { std::lock_guard<std::mutex> lock(mu); release = true; cv.notify_all(); }
        if (listener >= 0) shutdown(listener, SHUT_RDWR);
        if (thread.joinable()) thread.join();
        if (listener >= 0) close(listener);
        listener = -1;
    }
    ~StalledResponse() { stop(); }
};

static std::shared_ptr<Worker> exit_worker;
/* Async worker transports finalize on the thread pumping net_poll, after the
 * worker isolate itself may already have exited. Drain both ownership layers. */
static bool wait_network_requests(double timeout_ms) {
    const double end = now_ms() + timeout_ms;
    do {
        net_poll();
        if (!net_pending()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (now_ms() < end);
    return false;
}

static void check_exit_worker() {
    bool complete = exit_worker && exit_worker->finished;
    fprintf(stderr, "%s exit callback observes completed worker cleanup\n", complete ? "PASS" : "FAIL");
    if (!complete) std::_Exit(EXIT_FAILURE);
}

/* Use worker-created default backing stores, rather than a custom deleter:
 * their lifetime includes the worker isolate's ArrayBuffer allocator. A native
 * weak pointer observes release without keeping the store or allocator alive.
 */
static void check_shared_buffer_lifetime(int order) {
    static const char *names[] = {
        "source closes before delivery", "source terminates after delivery",
        "owner closes with an undelivered buffer"
    };
    printf("CASE worker-shared-buffer %s\n", names[order]);
    fflush(stdout);
    Document *d = doc_new("https://worker.test/shared-buffer.html");
    d->cross_origin_isolated = true;
    const char *html = "<!doctype html><html><body></body></html>";
    html_parse(d, html, strlen(html));
    JsHost host{};
    JsCtx *c = js_new(d, &host);
    uint32_t previous = g_wseq;
    std::string source =
        "globalThis.receivedShared = null;"
        "globalThis.sharedWorker = new Worker(URL.createObjectURL(new Blob([\""
        "globalThis.payload = new SharedArrayBuffer(2*1024*1024);"
        "const bytes = new Uint8Array(payload); bytes[0] = 23; bytes[bytes.length-1] = 199;"
        "postMessage(payload);";
    source += order == 1 ? "onmessage = () => {};" : "close();";
    source += "\"], {type:'text/javascript'}))); sharedWorker.onmessage = e => { receivedShared = e.data; };";
    js_eval(c, source.c_str(), "test:worker-shared-buffer");
    auto it = g_workers.find(g_wseq);
    std::shared_ptr<Worker> w = g_wseq != previous && it != g_workers.end() ? it->second : nullptr;
    CHECK("isolated owner creates a shared-buffer worker", w && w->coi);
    std::weak_ptr<v8::BackingStore> backing;
    bool queued = false;
    double end = now_ms() + 5000;
    while (w && now_ms() < end) {
        {
            std::lock_guard<std::mutex> lock(w->mu);
            if (!w->out.empty() && (order == 1 || w->finished)) {
                const OutMsg &message = w->out.front();
                queued = w->out.size() == 1 && message.kind == WM_MESSAGE && message.data.side &&
                    message.data.side->sab.size() == 1 && message.data.side->sab[0]->ByteLength() == (2u << 20);
                if (queued) backing = message.data.side->sab[0];
                break;
            }
            if (w->finished && w->out.empty()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK("message owns the worker-created two-MiB shared backing store", queued && !backing.expired());
    CHECK("source completion matches the delivery order", w && (order == 1 ? !w->finished.load() : w->finished.load()));
    if (order != 1) CHECK("source thread cleanup completes before delivery or discard", wait_worker_threads(5000));
    if (queued && order != 2) {
        js_tick(c);
        js_eval(c,
            "document.documentElement.setAttribute('data-shared-buffer', String("
            "receivedShared instanceof SharedArrayBuffer && receivedShared.byteLength === 2*1024*1024 && "
            "new Uint8Array(receivedShared)[0] === 23 && new Uint8Array(receivedShared)[receivedShared.byteLength-1] === 199));",
            "test:worker-shared-buffer-delivery");
        const char *valid = node_attr(d->html, "data-shared-buffer");
        CHECK("owner receives the intact shared buffer", valid && !strcmp(valid, "true"));
        if (order == 1) {
            js_eval(c, "sharedWorker.terminate();", "test:worker-shared-buffer-terminate");
            CHECK("worker termination finishes while the owner retains its buffer", wait_worker_threads(5000) && w->finished);
        }
        CHECK("shared backing store survives its source isolate", !backing.expired());
        {
            std::lock_guard<std::mutex> lock(w->mu);
            CHECK("delivery releases native queue ownership", w->out.empty());
        }
        js_eval(c, "sharedWorker.terminate(); receivedShared = null; sharedWorker = null;",
            "test:worker-shared-buffer-release");
        fflush(stdout); /* Preserve the exact release boundary if old code crashes. */
        {
            v8::Isolate::Scope isolate_scope(c->iso);
            for (int n = 0; n < 3 && !backing.expired(); ++n) c->iso->LowMemoryNotification();
        }
        CHECK("collection releases the default shared backing store after its source exits", backing.expired());
    }
    fflush(stdout);
    js_free(c); doc_free(d);
    CHECK("owner teardown drains any remaining worker thread", wait_worker_threads(5000));
    CHECK("owner teardown releases delivered and undelivered shared buffers", backing.expired());
}

/* Exercise the actual worker isolate, global bindings and cross-thread pump.
 * This source is loaded from the repository so it also remains a readable JS
 * regression. Its cases are separate from the upstream WPT assertion counts.
 */
static void check_worker_eventtarget_receivers() {
    printf("CASE worker-eventtarget receivers\n");
    std::ifstream input("tests/js/worker-eventtarget-regression.js", std::ios::binary);
    std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    CHECK("worker receiver regression source is available", !input.bad() && !source.empty() && source.size() < 65536);
    if (input.bad() || source.empty() || source.size() >= 65536) return;

    Document *d = doc_new("https://worker.test/eventtarget.html");
    const char *html = "<!doctype html><html><body></body></html>";
    html_parse(d, html, strlen(html));
    JsHost host{};
    JsCtx *c = js_new(d, &host);
    {
        JS_ENTER(c);
        (void)ctx->Global()->Set(ctx, jstr(iso, "__workerReceiverSource"), jstr(iso, source.c_str()));
    }
    js_eval(c, R"JS(
globalThis.__workerReceiverRows = null;
globalThis.__workerReceiverReady = false;
globalThis.__workerReceiverErrors = [];
globalThis.__workerReceiver = new Worker(URL.createObjectURL(new Blob(
    [__workerReceiverSource], {type: 'text/javascript'})));
const receiverFailure = message => {
    __workerReceiverErrors.push(String(message));
    document.documentElement.setAttribute('data-worker-receiver-error', __workerReceiverErrors.join('; '));
};
__workerReceiver.onerror = event => receiverFailure('worker error: ' + event.message);
__workerReceiver.onmessageerror = () => receiverFailure('worker messageerror');
__workerReceiver.onmessage = event => {
    const data = event.data;
    if (!data || typeof data !== 'object') { receiverFailure('invalid message'); return; }
    if (data.kind === 'lumen-eventtarget-ready') {
        if (__workerReceiverReady || data.completed !== 47 || data.expected !== 48) {
            receiverFailure('invalid or repeated ready message'); return;
        }
        __workerReceiverReady = true;
        __workerReceiver.postMessage({kind: 'lumen-eventtarget-ping'});
    } else if (data.kind === 'lumen-eventtarget-results') {
        if (!__workerReceiverReady || __workerReceiverRows !== null || data.expected !== 48 ||
            !Array.isArray(data.rows) || data.rows.length !== 48 ||
            data.rows.some(row => !row || typeof row.name !== 'string' ||
                (row.status !== 'PASS' && row.status !== 'FAIL')) ||
            new Set(data.rows.map(row => row.name)).size !== 48) {
            receiverFailure('invalid, duplicate or incomplete result rows'); return;
        }
        __workerReceiverRows = data.rows;
        document.documentElement.setAttribute('data-worker-receiver-complete', 'true');
    } else {
        receiverFailure('unexpected worker message');
    }
};
)JS", "test:worker-eventtarget-receivers");

    bool complete = false;
    const double deadline = now_ms() + 5000;
    while (now_ms() < deadline) {
        js_tick(c);
        const char *done = node_attr(d->html, "data-worker-receiver-complete");
        complete = done && !strcmp(done, "true");
        if (complete || node_attr(d->html, "data-worker-receiver-error")) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const char *protocol_error = node_attr(d->html, "data-worker-receiver-error");
    CHECK("actual worker completes all 48 unique receiver cases before the deadline", complete && !protocol_error);
    if (protocol_error) fprintf(stderr, "worker receiver protocol error: %s\n", protocol_error);
    if (complete && !protocol_error) {
        JS_ENTER(c);
        v8::Local<v8::Value> value;
        bool found = ctx->Global()->Get(ctx, jstr(iso, "__workerReceiverRows")).ToLocal(&value) && value->IsArray();
        CHECK("worker receiver results remain an intact 48-row array", found && value.As<v8::Array>()->Length() == 48);
        if (found && value.As<v8::Array>()->Length() == 48) {
            auto rows = value.As<v8::Array>();
            for (uint32_t i = 0; i < rows->Length(); ++i) {
                v8::Local<v8::Value> row_value, name_value, status_value;
                bool shape = rows->Get(ctx, i).ToLocal(&row_value) && row_value->IsObject();
                if (shape) {
                    auto row = row_value.As<v8::Object>();
                    shape = row->Get(ctx, jstr(iso, "name")).ToLocal(&name_value) &&
                            row->Get(ctx, jstr(iso, "status")).ToLocal(&status_value);
                }
                std::string name = shape ? jcstr(iso, name_value) : "malformed worker receiver row";
                bool passed = shape && jcstr(iso, status_value) == "PASS";
                CHECK(name.c_str(), passed);
                if (!passed && shape) {
                    v8::Local<v8::Value> message;
                    if (row_value.As<v8::Object>()->Get(ctx, jstr(iso, "message")).ToLocal(&message))
                        fprintf(stderr, "  %s\n", jcstr(iso, message).c_str());
                }
            }
        }
    }
    // Cleanup remains bounded even when the pre-fix worker fails or times out.
    js_eval(c, "__workerReceiver.terminate(); __workerReceiver = null; __workerReceiverSource = null;",
            "test:worker-eventtarget-receiver-cleanup");
    js_free(c); doc_free(d);
    CHECK("worker receiver regression releases its thread and owner", wait_worker_threads(5000));
}


static void check_worker_native_urls() {
    printf("CASE worker-url native bindings\n");
    std::ifstream input("tests/js/worker-url-regression.js", std::ios::binary);
    std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    CHECK("worker URL regression source is available", !input.bad() && !source.empty() && source.size() < 65536);
    if (input.bad() || source.empty() || source.size() >= 65536) return;

    Document *d = doc_new("https://worker.test/url.html");
    const char *html = "<!doctype html><html><body></body></html>";
    html_parse(d, html, strlen(html));
    JsHost host{};
    JsCtx *c = js_new(d, &host);
    {
        JS_ENTER(c);
        (void)ctx->Global()->Set(ctx, jstr(iso, "__workerURLRegressionSource"), jstr(iso, source.c_str()));
    }
    js_eval(c, R"JS(
globalThis.__workerURLRegressionRows = null;
globalThis.__workerURLRegressionReady = false;
globalThis.__workerURLRegressionErrors = [];
globalThis.__workerURLRegression = new Worker(URL.createObjectURL(new Blob(
    [__workerURLRegressionSource], {type: 'text/javascript'})));
const URLFailure = message => {
    __workerURLRegressionErrors.push(String(message));
    document.documentElement.setAttribute('data-worker-url-error', __workerURLRegressionErrors.join('; '));
};
__workerURLRegression.onerror = event => URLFailure('worker error: ' + event.message);
__workerURLRegression.onmessageerror = () => URLFailure('worker messageerror');
__workerURLRegression.onmessage = event => {
    const data = event.data;
    if (!data || typeof data !== 'object') { URLFailure('invalid message'); return; }
    if (data.kind === 'lumen-url-ready') {
        if (__workerURLRegressionReady || data.completed !== 73 || data.expected !== 74) {
            URLFailure('invalid or repeated ready message'); return;
        }
        __workerURLRegressionReady = true;
        __workerURLRegression.postMessage({kind: 'lumen-url-ping'});
    } else if (data.kind === 'lumen-url-results') {
        if (!__workerURLRegressionReady || __workerURLRegressionRows !== null || data.expected !== 74 ||
            !Array.isArray(data.rows) || data.rows.length !== 74 ||
            data.rows.some(row => !row || typeof row.name !== 'string' ||
                (row.status !== 'PASS' && row.status !== 'FAIL')) ||
            new Set(data.rows.map(row => row.name)).size !== 74) {
            URLFailure('invalid, duplicate or incomplete result rows'); return;
        }
        __workerURLRegressionRows = data.rows;
        document.documentElement.setAttribute('data-worker-url-complete', 'true');
    } else {
        URLFailure('unexpected worker message');
    }
};
)JS", "test:worker-native-url");

    bool complete = false;
    const double deadline = now_ms() + 5000;
    while (now_ms() < deadline) {
        js_tick(c);
        const char *done = node_attr(d->html, "data-worker-url-complete");
        complete = done && !strcmp(done, "true");
        if (complete || node_attr(d->html, "data-worker-url-error")) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const char *protocol_error = node_attr(d->html, "data-worker-url-error");
    CHECK("actual worker completes all 74 unique URL cases before the deadline", complete && !protocol_error);
    if (protocol_error) fprintf(stderr, "worker URL protocol error: %s\n", protocol_error);
    if (complete && !protocol_error) {
        JS_ENTER(c);
        v8::Local<v8::Value> value;
        bool found = ctx->Global()->Get(ctx, jstr(iso, "__workerURLRegressionRows")).ToLocal(&value) && value->IsArray();
        CHECK("worker URL results remain an intact 74-row array", found && value.As<v8::Array>()->Length() == 74);
        if (found && value.As<v8::Array>()->Length() == 74) {
            auto rows = value.As<v8::Array>();
            for (uint32_t i = 0; i < rows->Length(); ++i) {
                v8::Local<v8::Value> row_value, name_value, status_value;
                bool shape = rows->Get(ctx, i).ToLocal(&row_value) && row_value->IsObject();
                if (shape) {
                    auto row = row_value.As<v8::Object>();
                    shape = row->Get(ctx, jstr(iso, "name")).ToLocal(&name_value) &&
                            row->Get(ctx, jstr(iso, "status")).ToLocal(&status_value);
                }
                std::string name = shape ? jcstr(iso, name_value) : "malformed worker URL row";
                bool passed = shape && jcstr(iso, status_value) == "PASS";
                CHECK(name.c_str(), passed);
                if (!passed && shape) {
                    v8::Local<v8::Value> message;
                    if (row_value.As<v8::Object>()->Get(ctx, jstr(iso, "message")).ToLocal(&message))
                        fprintf(stderr, "  %s\n", jcstr(iso, message).c_str());
                }
            }
        }
    }
    // Cleanup remains bounded even when the pre-fix worker fails or times out.
    js_eval(c, "__workerURLRegression.terminate(); __workerURLRegression = null; __workerURLRegressionSource = null;",
            "test:worker-native-url-cleanup");
    js_free(c); doc_free(d);
    CHECK("worker URL regression releases its thread and owner", wait_worker_threads(5000));
}

static void check_worker_query_bodies() {
    printf("CASE worker-query-body native bindings\n");
    std::ifstream common_input("tests/js/url-params-body-regression.js", std::ios::binary);
    std::ifstream input("tests/js/worker-query-body-regression.js", std::ios::binary);
    std::string common((std::istreambuf_iterator<char>(common_input)), std::istreambuf_iterator<char>());
    std::string driver((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    bool available = !common_input.bad() && !input.bad() && !common.empty() && !driver.empty() &&
        common.size() + driver.size() + 1 < 65536;
    CHECK("worker query/body regression source is available", available);
    if (!available) return;
    std::string source = common + "\n" + driver;

    Document *d = doc_new("https://worker.test/query-body.html");
    const char *html = "<!doctype html><html><body></body></html>";
    html_parse(d, html, strlen(html));
    JsHost host{};
    JsCtx *c = js_new(d, &host);
    {
        JS_ENTER(c);
        (void)ctx->Global()->Set(ctx, jstr(iso, "__workerQueryBodyRegressionSource"), jstr(iso, source.c_str()));
    }
    js_eval(c, R"JS(
globalThis.__workerQueryBodyRegressionRows = null;
globalThis.__workerQueryBodyRegressionReady = false;
globalThis.__workerQueryBodyRegressionErrors = [];
globalThis.__workerQueryBodyRegression = new Worker(URL.createObjectURL(new Blob(
    [__workerQueryBodyRegressionSource], {type: 'text/javascript'})));
const QueryBodyFailure = message => {
    __workerQueryBodyRegressionErrors.push(String(message));
    document.documentElement.setAttribute('data-worker-query-body-error', __workerQueryBodyRegressionErrors.join('; '));
};
__workerQueryBodyRegression.onerror = event => QueryBodyFailure('worker error: ' + event.message);
__workerQueryBodyRegression.onmessageerror = () => QueryBodyFailure('worker messageerror');
__workerQueryBodyRegression.onmessage = event => {
    const data = event.data;
    if (!data || typeof data !== 'object') { QueryBodyFailure('invalid message'); return; }
    if (data.kind === 'lumen-query-body-ready') {
        if (__workerQueryBodyRegressionReady || data.completed !== 72 || data.expected !== 73) {
            QueryBodyFailure('invalid or repeated ready message'); return;
        }
        __workerQueryBodyRegressionReady = true;
        __workerQueryBodyRegression.postMessage({kind: 'lumen-query-body-ping'});
    } else if (data.kind === 'lumen-query-body-results') {
        if (!__workerQueryBodyRegressionReady || __workerQueryBodyRegressionRows !== null || data.expected !== 73 ||
            !Array.isArray(data.rows) || data.rows.length !== 73 ||
            data.rows.some(row => !row || typeof row.name !== 'string' ||
                (row.status !== 'PASS' && row.status !== 'FAIL')) ||
            new Set(data.rows.map(row => row.name)).size !== 73) {
            QueryBodyFailure('invalid, duplicate or incomplete result rows'); return;
        }
        __workerQueryBodyRegressionRows = data.rows;
        document.documentElement.setAttribute('data-worker-query-body-complete', 'true');
    } else {
        QueryBodyFailure('unexpected worker message');
    }
};
)JS", "test:worker-query-body");

    bool complete = false;
    const double deadline = now_ms() + 5000;
    while (now_ms() < deadline) {
        js_tick(c);
        const char *done = node_attr(d->html, "data-worker-query-body-complete");
        complete = done && !strcmp(done, "true");
        if (complete || node_attr(d->html, "data-worker-query-body-error")) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const char *protocol_error = node_attr(d->html, "data-worker-query-body-error");
    CHECK("actual worker completes all 73 unique query/body cases before the deadline", complete && !protocol_error);
    if (protocol_error) fprintf(stderr, "worker query/body protocol error: %s\n", protocol_error);
    if (complete && !protocol_error) {
        JS_ENTER(c);
        v8::Local<v8::Value> value;
        bool found = ctx->Global()->Get(ctx, jstr(iso, "__workerQueryBodyRegressionRows")).ToLocal(&value) && value->IsArray();
        CHECK("worker query/body results remain an intact 73-row array", found && value.As<v8::Array>()->Length() == 73);
        if (found && value.As<v8::Array>()->Length() == 73) {
            auto rows = value.As<v8::Array>();
            for (uint32_t i = 0; i < rows->Length(); ++i) {
                v8::Local<v8::Value> row_value, name_value, status_value;
                bool shape = rows->Get(ctx, i).ToLocal(&row_value) && row_value->IsObject();
                if (shape) {
                    auto row = row_value.As<v8::Object>();
                    shape = row->Get(ctx, jstr(iso, "name")).ToLocal(&name_value) &&
                            row->Get(ctx, jstr(iso, "status")).ToLocal(&status_value);
                }
                std::string name = shape ? jcstr(iso, name_value) : "malformed worker query/body row";
                bool passed = shape && jcstr(iso, status_value) == "PASS";
                CHECK(name.c_str(), passed);
                if (!passed && shape) {
                    v8::Local<v8::Value> message;
                    if (row_value.As<v8::Object>()->Get(ctx, jstr(iso, "message")).ToLocal(&message))
                        fprintf(stderr, "  %s\n", jcstr(iso, message).c_str());
                }
            }
        }
    }
    // Cleanup remains bounded even when the pre-fix worker fails or times out.
    js_eval(c, "__workerQueryBodyRegression.terminate(); __workerQueryBodyRegression = null; __workerQueryBodyRegressionSource = null;",
            "test:worker-query-body-cleanup");
    js_free(c); doc_free(d);
    CHECK("worker query/body regression releases its thread and owner", wait_worker_threads(5000));
}

int main(int argc, char **argv) {
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    Document *d = doc_new("file:///worker-lifecycle.html");
    const char *html = "<!doctype html><html><body></body></html>";
    html_parse(d, html, strlen(html));
    JsHost host = {};
    JsCtx *c = js_new(d, &host);
    if (argc > 1 && !strcmp(argv[1], "--exit-with-worker")) {
        /* Registered before the first worker, so this observer runs after the
         * worker exit backstop. Its shared_ptr outlives both exit callbacks.
         */
        std::atexit(check_exit_worker);
        js_eval(c, "globalThis.worker = new Worker(URL.createObjectURL(new Blob(['onmessage = () => {};'])));", "test:exit-with-worker");
        if (g_workers.empty()) return 1;
        exit_worker = g_workers.begin()->second;
        js_free(c); doc_free(d); net_shutdown();
        printf("RESULTS worker-exit returning through the registered backstop\n");
        return 0;
    }
    check_worker_eventtarget_receivers();
    check_worker_native_urls();
    check_worker_query_bodies();
    js_eval(c,
        "globalThis.received = []; globalThis.closedWorker = new Worker(URL.createObjectURL(new Blob(["
        "\"postMessage('before-close'); close(); postMessage('after-close');\""
        "], {type:'text/javascript'}))); closedWorker.onmessage = e => received.push(e.data);",
        "test:worker-close");
    CHECK("worker is registered", g_workers.size() == 1);
    if (g_workers.size() != 1) return 1;
    auto w = g_workers.begin()->second;

    bool stopped = false;
    double end = now_ms() + 5000;
    while (now_ms() < end) {
        { std::lock_guard<std::mutex> lk(w->mu); stopped = w->finished && w->out.size() == 2; }
        if (stopped) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK("close preserves messages from the current task", stopped);

    js_eval(c,
        "globalThis.closePayload = new Uint8Array(1024*1024);"
        "for(let i=0;i<32;i++) closedWorker.postMessage(closePayload);",
        "test:post-after-close");
    size_t queued = 0;
    { std::lock_guard<std::mutex> lk(w->mu); queued = w->in.size(); }
    CHECK("closed worker discards incoming messages", queued == 0);
    if (queued) fprintf(stderr, "closed worker retained %zu one-MiB messages\n", queued);
    js_tick(c);
    js_eval(c,
        "document.documentElement.setAttribute('data-events', received.join(','));"
        "closedWorker.terminate(); closePayload = null;",
        "test:worker-close-events");
    const char *events = node_attr(d->html, "data-events");
    CHECK("both current-task messages reach the owner", events && !strcmp(events, "before-close,after-close"));
    js_eval(c,
        "globalThis.getterSeen = false;"
        "const duringClone = new Worker(URL.createObjectURL(new Blob(['onmessage = () => {};'])));",
        "test:worker-startup");
    std::shared_ptr<Worker> clone_worker = g_workers.empty() ? nullptr : g_workers.rbegin()->second;
    bool counted;
    { WorkerThreads &threads = worker_threads(); std::lock_guard<std::mutex> lock(threads.mu); counted = threads.running > 0; }
    CHECK("worker startup is tracked before isolate publication", clone_worker && counted);
    CHECK("a bounded drain reports an unfinished worker", !wait_worker_threads(0));
    js_eval(c,
        "duringClone.postMessage({get value() { getterSeen = true; duringClone.terminate(); return 1; }});"
        "document.documentElement.setAttribute('data-getter', String(getterSeen));",
        "test:terminate-during-serialization");
    const char *getter = node_attr(d->html, "data-getter");
    CHECK("a clone getter can terminate its worker safely", getter && !strcmp(getter, "true"));
    CHECK("terminated registry entries drain through actual worker cleanup", wait_worker_threads(5000));
    CHECK("completion includes isolate and allocator teardown", w->finished && clone_worker && clone_worker->finished);

    auto terminated = std::make_shared<Worker>();
    WEnv env; env.w = terminated;
    worker_terminate(terminated.get());
    Buf late; late.p = static_cast<uint8_t *>(malloc(128)); late.n = 128;
    post_out(&env, OutMsg{ WM_MESSAGE, late, {}, {}, 0, 0 });
    CHECK("terminate discards racing outgoing messages", terminated->out.empty());

    for (int operation = 0; operation < 3; ++operation) {
        static const char *names[] = { "fetch", "importScripts", "module import" };
        printf("CASE stalled-worker %s\n", names[operation]);
        StalledResponse server;
        bool listening = server.start();
        CHECK("loopback cancellation server starts", listening);
        if (listening) {
            std::string url = "http://127.0.0.1:" + std::to_string(server.port) + "/stall";
            std::string script = operation == 0 ? "fetch('" + url + "').catch(() => {});" :
                                 operation == 1 ? "importScripts('" + url + "');" : "import '" + url + "';";
            std::string source = "globalThis.networkWorker = new Worker(URL.createObjectURL(new Blob([\"" + script +
                "\"], {type:'text/javascript'})), {type:'" + (operation == 2 ? "module" : "classic") + "'});";
            js_eval(c, source.c_str(), "test:worker-stalled-response");
            auto network_worker = g_workers.empty() ? nullptr : g_workers.rbegin()->second;
            CHECK("worker is blocked on a response that has not been sent", server.wait_entered());
            bool active = false;
            if (network_worker) { std::lock_guard<std::mutex> lock(network_worker->mu); active = network_worker->request != nullptr || !network_worker->requests.empty(); }
            CHECK("worker retains its active async or sync request until cancellation completes", active);
            double before = now_ms();
            js_eval(c, "networkWorker.terminate();", "test:cancel-stalled-worker");
            CHECK("terminate returns promptly while the server holds its response", now_ms() - before < 1000);
            bool drained = wait_worker_threads(1500);
            CHECK("socket cancellation drains without waiting for an HTTP timeout", drained);
            bool cleared = false;
            if (network_worker) { std::lock_guard<std::mutex> lock(network_worker->mu); cleared = network_worker->finished && !network_worker->request && network_worker->requests.empty(); }
            CHECK("canceled worker releases request and isolate resources", cleared);
            CHECK("canceled worker network finalizers drain", wait_network_requests(1500));
            server.stop();
        }
    }
    /* Keep the regression itself leak-free even when run against the old code. */
    for (OutMsg &m : terminated->out) free(m.data.p);
    terminated->out.clear();
    for (Buf &b : w->in) free(b.p);
    w->in.clear();
    for (int order = 0; order < 3; ++order) check_shared_buffer_lifetime(order);
    js_free(c); doc_free(d);
    CHECK("explicit process shutdown completes before subsystem teardown", js_workers_shutdown(5000));
    CHECK("native transport finalizers complete before subsystem teardown", wait_network_requests(2000));
    net_shutdown();
    printf("RESULTS worker-lifecycle %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}

