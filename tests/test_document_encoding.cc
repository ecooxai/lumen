/* Genuine HTTP bytes, native frame loading and document-owned hyperlink APIs.
 * Custom regression rows are kept separate from unchanged upstream WPT rows. */
#include "../src/js/js_int.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
extern "C" void font_init(void);
extern "C" bool js_workers_shutdown(unsigned timeout_ms);

static int checks, failures;
#define CHECK(label, expression) do { const bool okay_ = !!(expression); ++checks; \
    printf("%s %s\n", okay_ ? "PASS" : "FAIL", label); \
    if (!okay_) ++failures; fflush(stdout); } while (0)

struct Reply { std::string content_type, bytes; };

static std::string html_bytes(const std::string &payload, const std::string &references) {
    return "<!doctype html><html><head></head><body><span id='payload'>" + payload +
        "</span><a id='link' href='?" + references + "#" + references + "'>link</a></body></html>";
}
static std::string utf16_bytes(bool little, bool bom) {
    const std::u16string text = u"<!doctype html><html><head></head><body><span id='payload'>\u03A3"
        u"</span><a id='link' href='?&#931;#&#931;'>link</a></body></html>";
    std::string output;
    if (bom) output.append(little ? "\xff\xfe" : "\xfe\xff", 2);
    for (char16_t value : text) {
        output.push_back(static_cast<char>(little ? value & 255 : value >> 8));
        output.push_back(static_cast<char>(little ? value >> 8 : value & 255));
    }
    return output;
}
static std::map<std::string, Reply> replies() {
    const std::string dagger("\xe2\x80\xa0", 3), sigma("\xce\xa3", 2);
    return {
        {"/utf8", {"text/html; charset=UTF-8", html_bytes(dagger, "&#8224;")}},
        {"/windows1252", {"text/html; charset=ISO-8859-1", html_bytes(std::string("\x86", 1), "&#8224;")}},
        {"/big5", {"text/html; charset=Big5", html_bytes(std::string("\xa4\x40", 2), "&#8224;")}},
        {"/euckr", {"text/html; charset=EUC-KR", html_bytes(std::string("\xa2\xd3", 2), "&#8224;")}},
        {"/iso2022jp", {"text/html; charset=ISO-2022-JP", html_bytes(std::string("\x1b$B$\"\x1b(B", 8), "&#8254;\\")}},
        {"/gb18030", {"text/html; charset=gb18030", html_bytes(std::string("\xd6\xd0", 2), "&#58853;")}},
        {"/shiftjis", {"text/html; charset=Shift_JIS", html_bytes(std::string("\x82\xa0", 2), "&#8722;")}},
        {"/iso88592", {"text/html; charset=ISO-8859-2", html_bytes(std::string("\xa1", 1), "&#162;")}},
        {"/bom8", {"text/html; charset=windows-1252", std::string("\xef\xbb\xbf", 3) + html_bytes(sigma, "&#931;")}},
        {"/bom16le", {"text/html; charset=windows-1252", utf16_bytes(true, true)}},
        {"/header16be", {"text/html; charset=UTF-16BE", utf16_bytes(false, false)}},
        {"/unknown", {"text/html; charset=not-a-supported-encoding", html_bytes(sigma, "&#931;")}},
    };
}

class Server {
    int listener = -1;
    std::thread worker;
    std::atomic<bool> stopped{false};
    const std::map<std::string, Reply> routes = replies();
    mutable std::mutex mutex;
    std::map<std::string, unsigned> requests;
    unsigned malformed = 0;

    static bool send_all(int fd, const std::string &wire) {
        size_t written = 0;
        while (written < wire.size()) {
            const ssize_t amount = send(fd, wire.data() + written, wire.size() - written, 0);
            if (amount <= 0) return false;
            written += static_cast<size_t>(amount);
        }
        return true;
    }
    static bool request_path(int fd, std::string &path) {
        std::string wire;
        char block[2048];
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (wire.find("\r\n\r\n") == std::string::npos) {
            const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) return false;
            const timeval timeout{static_cast<time_t>(remaining / 1000000),
                                  static_cast<suseconds_t>(remaining % 1000000)};
            if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout)) return false;
            const ssize_t amount = recv(fd, block, sizeof block, 0);
            if (amount <= 0 || wire.size() + static_cast<size_t>(amount) > 16384) return false;
            wire.append(block, static_cast<size_t>(amount));
        }
        if (wire.rfind("GET ", 0) != 0) return false;
        const size_t end = wire.find(' ', 4);
        if (end == std::string::npos || end == 4 || end > wire.find("\r\n")) return false;
        path = wire.substr(4, end - 4);
        return true;
    }
    void run() {
        while (!stopped.load()) {
            pollfd ready{listener, POLLIN, 0};
            if (poll(&ready, 1, 50) <= 0 || !(ready.revents & POLLIN)) continue;
            const int fd = accept(listener, nullptr, nullptr);
            if (fd < 0) continue;
            const timeval timeout{1, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
            const int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
            std::string path;
            const bool valid = request_path(fd, path);
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (valid) ++requests[path]; else ++malformed;
            }
            auto route = valid ? routes.find(path) : routes.end();
            const bool found = route != routes.end();
            const Reply fallback{"text/plain; charset=UTF-8", "Unknown encoding fixture"};
            const Reply &reply = found ? route->second : fallback;
            const std::string wire = std::string(found ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Not Found\r\n") +
                "Content-Type: " + reply.content_type + "\r\nContent-Length: " + std::to_string(reply.bytes.size()) +
                "\r\nConnection: close\r\n\r\n" + reply.bytes;
            send_all(fd, wire);
            close(fd);
        }
    }
public:
    unsigned port = 0;
    Server() {
        listener = socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) return;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t size = sizeof address;
        if (bind(listener, reinterpret_cast<sockaddr *>(&address), size) ||
            getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size) || listen(listener, 8)) {
            close(listener); listener = -1; return;
        }
        port = ntohs(address.sin_port);
        worker = std::thread([this] { run(); });
    }
    ~Server() {
        stopped.store(true);
        if (worker.joinable()) worker.join();
        if (listener >= 0) close(listener);
    }
    std::string origin() const { return "http://127.0.0.1:" + std::to_string(port); }
    bool complete() const {
        std::lock_guard<std::mutex> lock(mutex);
        if (malformed || requests.size() != routes.size()) return false;
        for (const auto &route : routes) {
            auto observed = requests.find(route.first);
            if (observed == requests.end() || observed->second != 1) return false;
        }
        return true;
    }
};

static bool read_source(std::string &source) {
    std::ifstream input("tests/js/document-encoding-regression.js", std::ios::binary);
    source.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return !input.bad() && !source.empty() && source.size() < 65536;
}
static bool evaluate(JsCtx *context, const char *source) {
    if (!context || context->ctx.IsEmpty()) return false;
    JS_ENTER(context);
    v8::TryCatch caught(iso);
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> result;
    const bool okay = v8::Script::Compile(ctx, jstr(iso, source)).ToLocal(&script) &&
        script->Run(ctx).ToLocal(&result);
    if (!okay && caught.HasCaught())
        fprintf(stderr, "document encoding driver: %s\n", jcstr(iso, caught.Exception()).c_str());
    return okay && result->IsTrue();
}
static void result_rows(JsCtx *context) {
    constexpr unsigned expected = 66;
    JS_ENTER(context);
    v8::TryCatch caught(iso);
    v8::Local<v8::Value> value;
    const bool obtained = ctx->Global()->Get(ctx, jstr(iso, "__documentEncodingRows")).ToLocal(&value);
    const bool complete = obtained && value->IsArray() && value.As<v8::Array>()->Length() == expected;
    CHECK("all document encoding cases produce a complete result array", complete);
    if (!complete) return;
    auto rows = value.As<v8::Array>();
    std::set<std::string> names;
    bool valid = true;
    for (unsigned index = 0; index < expected; ++index) {
        v8::Local<v8::Value> row, name, status, message;
        bool shape = rows->Get(ctx, index).ToLocal(&row) && row->IsObject();
        if (shape) {
            auto object = row.As<v8::Object>();
            shape = object->Get(ctx, jstr(iso, "name")).ToLocal(&name) && name->IsString() &&
                object->Get(ctx, jstr(iso, "status")).ToLocal(&status) && status->IsString();
        }
        std::string label = shape ? jcstr(iso, name) : "malformed document encoding row";
        std::string outcome = shape ? jcstr(iso, status) : "";
        shape = shape && !label.empty() && names.insert(label).second &&
            (outcome == "PASS" || outcome == "FAIL");
        valid = valid && shape;
        CHECK(label.c_str(), shape && outcome == "PASS");
        if (shape && outcome == "FAIL" && row.As<v8::Object>()->Get(ctx, jstr(iso, "message")).ToLocal(&message)) {
            const std::string explanation = jcstr(iso, message);
            fprintf(stderr, "  %.*s\n", static_cast<int>(std::min(explanation.size(), size_t(8192))), explanation.data());
        }
    }
    CHECK("document encoding rows have unique names and explicit outcomes", valid && names.size() == expected);
}

int main(int argc, char **argv) {
    (void)argc;
    std::string source;
    const bool source_ready = read_source(source);
    CHECK("document encoding regression source is present and bounded", source_ready);
    if (!source_ready) return 1;
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    Server server;
    CHECK("the real loopback encoding server initializes", server.port != 0);
    if (!server.port) return 1;
    Document *document = doc_new((server.origin() + "/parent").c_str());
    const char *html = "<!doctype html><html><head></head><body></body></html>";
    html_parse(document, html, strlen(html));
    JsHost host{};
    JsCtx *context = js_new(document, &host);
    const bool ready = context && !context->ctx.IsEmpty();
    CHECK("the real parent document and V8 realm initialize", ready);
    if (ready) {
        const std::string setup = "globalThis.__documentEncodingOrigin='" + server.origin() + "';true";
        CHECK("the fixture receives only the actual loopback origin", evaluate(context, setup.c_str()));
        js_eval(context, source.c_str(), "test:document-encoding-source");
        CHECK("the real asynchronous encoding driver starts", evaluate(context,
            "Array.isArray(globalThis.__documentEncodingRows)&&globalThis.__documentEncodingDone===false"));
        bool done = false;
        const double deadline = now_ms() + 30000;
        while (!(done = evaluate(context, "globalThis.__documentEncodingDone===true")) && now_ms() < deadline) {
            net_poll(); js_release_pins(); js_tick(context);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK("the real HTTP and foreign-realm cases finish before the deadline", done);
        CHECK("the asynchronous driver has no unhandled setup failure", evaluate(context,
            "typeof __documentEncodingFatal==='undefined'"));
        if (!evaluate(context, "typeof __documentEncodingFatal==='undefined'")) {
            JS_ENTER(context);
            v8::Local<v8::Value> fatal;
            if (ctx->Global()->Get(ctx, jstr(iso, "__documentEncodingFatal")).ToLocal(&fatal))
                fprintf(stderr, "document encoding fatal: %s\n", jcstr(iso, fatal).c_str());
        }
        result_rows(context);
        CHECK("all twelve HTTP byte fixtures were actually requested once", server.complete());
    }
    js_release_pins();
    if (context) js_free(context);
    doc_free(document);
    const double deadline = now_ms() + 3000;
    while (net_pending() && now_ms() < deadline) {
        net_poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool network_drained = net_pending() == 0;
    CHECK("all encoding HTTP requests finish before network teardown", network_drained);
    const bool workers_drained = js_workers_shutdown(5000);
    CHECK("native worker ownership remains drained before teardown", workers_drained);
    printf("RESULTS document-encoding %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    fflush(stdout);
    if (!network_drained || !workers_drained) std::_Exit(1);
    net_shutdown();
    return failures ? 1 : 0;
}
