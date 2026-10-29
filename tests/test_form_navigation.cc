/* Public form submissions and real named iframe HTTP requests. Custom native
 * regressions are never included in official WPT assertion counts. */
#include "../src/js/js_int.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <poll.h>
#include <dlfcn.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
extern "C" void font_init(void);
/* The baseline intentionally has no form queue. Resolve only the genuine
 * diagnostic exported by this executable; absence fails explicit rows.
 * RTLD_MAIN_ONLY confines lookup to the standalone test executable on macOS;
 * Linux verifies the resolved symbol's device/inode against /proc/self/exe. */
using FormQueueCounter = void (*)(size_t *, size_t *);
static FormQueueCounter form_queue_counter() {
    static const auto counter = []() -> FormQueueCounter {
#ifdef __APPLE__
        return reinterpret_cast<FormQueueCounter>(dlsym(RTLD_MAIN_ONLY, "js_form_navigations_pending"));
#else
        void *symbol = dlsym(RTLD_DEFAULT, "js_form_navigations_pending");
        Dl_info info{};
        struct stat executable{}, resolved{};
        if (!symbol || !dladdr(symbol, &info) || !info.dli_fname ||
            stat("/proc/self/exe", &executable) || stat(info.dli_fname, &resolved) ||
            executable.st_dev != resolved.st_dev || executable.st_ino != resolved.st_ino) return nullptr;
        return reinterpret_cast<FormQueueCounter>(symbol);
#endif
    }();
    return counter;
}
static int checks, failures;
#define CHECK(label, expression) do { const bool okay = !!(expression); ++checks; \
    printf("%s %s\n", okay ? "PASS" : "FAIL", label); if (!okay) ++failures; fflush(stdout); } while (0)

struct Receipt {
    std::string method, path, body;
    std::map<std::string, std::string> headers;
};
class Server {
    int listener = -1;
    std::thread worker;
    std::atomic<bool> stopped{false};
    mutable std::mutex mutex;
    std::vector<Receipt> received;
    std::string redirect;
    static bool send_all(int fd, const std::string &value) {
        size_t done = 0;
        while (done < value.size()) {
            int flags = 0;
#ifdef MSG_NOSIGNAL
            flags = MSG_NOSIGNAL;
#endif
            const ssize_t n = send(fd, value.data() + done, value.size() - done, flags);
            if (n <= 0) return false;
            done += (size_t)n;
        }
        return true;
    }
    bool read_request(int fd, Receipt &result) {
        std::string wire; char bytes[4096]; size_t head;
        while ((head = wire.find("\r\n\r\n")) == std::string::npos) {
            const ssize_t n = recv(fd, bytes, sizeof bytes, 0);
            if (n <= 0 || wire.size() + (size_t)n > 128 * 1024) return false;
            wire.append(bytes, (size_t)n);
        }
        const size_t end = wire.find("\r\n"), first = wire.find(' '), second = wire.find(' ', first + 1);
        if (first == std::string::npos || second == std::string::npos || second > end) return false;
        result.method = wire.substr(0, first); result.path = wire.substr(first + 1, second - first - 1);
        for (size_t at = end + 2; at < head;) {
            const size_t next = wire.find("\r\n", at), colon = wire.find(':', at);
            if (next == std::string::npos || colon == std::string::npos || colon >= next) return false;
            std::string name = wire.substr(at, colon - at);
            for (char &ch : name) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
            size_t value = colon + 1;
            while (value < next && (wire[value] == ' ' || wire[value] == '\t')) ++value;
            result.headers[name] = wire.substr(value, next - value); at = next + 2;
        }
        size_t length = 0;
        if (result.headers.count("content-length")) {
            char *tail = nullptr; unsigned long value = strtoul(result.headers["content-length"].c_str(), &tail, 10);
            if (!tail || *tail || value > 2 * 1024 * 1024) return false;
            length = value;
        }
        result.body = wire.substr(head + 4);
        while (result.body.size() < length) {
            const ssize_t n = recv(fd, bytes, std::min(sizeof bytes, length - result.body.size()), 0);
            if (n <= 0) return false;
            result.body.append(bytes, (size_t)n);
        }
        return result.body.size() == length;
    }
    void run() {
        while (!stopped.load()) {
            pollfd ready{listener, POLLIN, 0};
            if (poll(&ready, 1, 50) <= 0) continue;
            int fd = accept(listener, nullptr, nullptr);
            if (fd < 0) continue;
            timeval timeout{1, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
#ifdef SO_NOSIGPIPE
            int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
            Receipt request;
            if (!read_request(fd, request)) { close(fd); continue; }
            std::string location;
            {
                std::lock_guard<std::mutex> lock(mutex);
                received.push_back(request); location = redirect;
            }
            const bool moved = request.path == "/redirect-blocked" && !location.empty();
            const std::string body = "<!doctype html><html><body><p id='received'>form response</p></body></html>";
            std::string reply = moved ? "HTTP/1.1 307 Temporary Redirect\r\nLocation: " + location + "\r\n" :
                                       "HTTP/1.1 200 OK\r\n";
            if (request.path == "/embedding-denied") reply += "X-Frame-Options: DENY\r\n";
            reply += "Content-Type: text/html; charset=utf-8\r\nContent-Length: " +
                     std::to_string(moved ? 0 : body.size()) + "\r\nConnection: close\r\n\r\n";
            if (!moved) reply += body;
            send_all(fd, reply); close(fd);
        }
    }
public:
    unsigned port = 0;
    Server() {
        listener = socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) return;
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t size = sizeof address;
        if (bind(listener, (sockaddr *)&address, size) || getsockname(listener, (sockaddr *)&address, &size) ||
            listen(listener, 8)) { close(listener); listener = -1; return; }
        port = ntohs(address.sin_port); worker = std::thread([this] { run(); });
    }
    ~Server() { stopped.store(true); if (worker.joinable()) worker.join(); if (listener >= 0) close(listener); }
    std::string origin() const { return "http://127.0.0.1:" + std::to_string(port); }
    void redirect_to(const std::string &url) { std::lock_guard<std::mutex> lock(mutex); redirect = url; }
    size_t count(const std::string &path) const {
        std::lock_guard<std::mutex> lock(mutex);
        return std::count_if(received.begin(), received.end(), [&](const Receipt &row) { return row.path == path; });
    }
    Receipt last(const std::string &path) const {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto at = received.rbegin(); at != received.rend(); ++at) if (at->path == path) return *at;
        return {};
    }
};
static bool value(JsCtx *context, const std::string &source) {
    JS_ENTER(context); v8::TryCatch caught(iso);
    v8::Local<v8::Script> script; v8::Local<v8::Value> answer;
    const bool okay = v8::Script::Compile(ctx, jstr(iso, source.c_str())).ToLocal(&script) &&
        script->Run(ctx).ToLocal(&answer);
    if (!okay && caught.HasCaught()) fprintf(stderr, "form fixture: %s\n", jcstr(iso, caught.Exception()).c_str());
    iso->PerformMicrotaskCheckpoint();
    return okay && answer->BooleanValue(iso);
}
struct Fixture {
    Document *document; JsCtx *context;
    unsigned gets = 0, posts = 0;
    std::string last_url, last_body, last_type;
    static void get(void *opaque, const char *url) {
        auto *f = (Fixture *)opaque; ++f->gets; f->last_url = url;
    }
    static void post(void *opaque, const char *url, const char *body, size_t n, const char *type) {
        auto *f = (Fixture *)opaque; ++f->posts; f->last_url = url; f->last_body.assign(body, n); f->last_type = type;
    }
    explicit Fixture(const std::string &origin, const char *csp = nullptr) {
        document = doc_new((origin + "/start").c_str());
        if (csp) document->csp = xstrdup(csp);
        const char *html = "<!doctype html><html><body><iframe id='first' name='first'></iframe>"
            "<iframe id='second' name='second'></iframe><form id='form' method='post' target='first' action='/post'>"
            "<input id='field' name='key' value='value'><button id='button' name='button' value='clicked'>send</button>"
            "</form></body></html>";
        html_parse(document, html, strlen(html));
        JsHost host{}; host.ud = this; host.navigate = get; host.navigate_post = post;
        context = js_new(document, &host);
        frames_scan(context);
        value(context, "globalThis.f=document.getElementById('form');globalThis.b=document.getElementById('button');"
            "globalThis.x=document.getElementById('field');globalThis.a=document.getElementById('first');"
            "globalThis.z=document.getElementById('second');globalThis.loads=0;"
            "a.onload=()=>loads++;z.onload=()=>loads++;true");
    }
    ~Fixture() { js_release_pins(); js_free(context); doc_free(document); }
    bool untouched() const { return gets == 0 && posts == 0; }
    bool pump(const std::function<bool()> &done, double milliseconds = 2500) {
        const double deadline = now_ms() + milliseconds;
        do {
            net_poll(); js_tick(context);
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (now_ms() < deadline);
        return done();
    }
    void settle() { pump([] { return false; }, 75); }
};
static bool queue(size_t expected, bool empty_bytes = false) {
    const auto counter = form_queue_counter();
    if (!counter) return false;
    size_t count = 999, bytes = 999; counter(&count, &bytes);
    return count == expected && (empty_bytes ? bytes == 0 : count == 0 || bytes > 0);
}
static JsCtx *child(Fixture &f, const std::string &url, const char *sandbox = nullptr) {
    Node *node = node_new_element(f.document, "iframe", NS_HTML);
    node_set_attr(node, "src", url.c_str()); node_set_attr(node, "name", "source");
    if (sandbox) node_set_attr(node, "sandbox", sandbox);
    node_append(f.document->body, node);
    Document *document = doc_new(url.c_str());
    const char *html = "<!doctype html><html><body><form id='child' method='post'><input name='child' value='value'></form></body></html>";
    html_parse(document, html, strlen(html));
    JsHost host{}; JsCtx *context = js_new_ex(document, &host, f.context, node);
    context->frame_src = url; return context;
}
int main(int argc, char **argv) {
    (void)argc; dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    Server server, other;
    CHECK("real independent loopback form origins initialize", server.port && other.port && server.port != other.port);
    if (!server.port || !other.port) return 1;
    {
        Fixture f(server.origin());
        CHECK("a named-frame POST is deferred past the submit call", value(f.context, "f.submit();loads===0") && f.untouched());
        CHECK("planned form navigation owns one counted request", queue(1));
        CHECK("the selected iframe receives an actual POST", f.pump([&] { return server.count("/post") == 1 && value(f.context, "loads===1"); }));
        Receipt r = server.last("/post");
        CHECK("POST method body and content type are preserved", r.method == "POST" && r.body == "key=value" &&
              r.headers["content-type"] == "application/x-www-form-urlencoded");
        CHECK("the selected frame installs the HTTP response without navigating its parent", f.untouched() &&
              value(f.context, "a.contentDocument.getElementById('received').textContent==='form response'&&z.contentDocument.URL==='about:blank'"));
        CHECK("the form origin and referrer belong to the initiating document", r.headers["origin"] == server.origin() &&
              r.headers["referer"] == server.origin() + "/start");
        CHECK("completed form queue releases all counted bytes", queue(0, true));
    }
    {
        Fixture f(server.origin());
        CHECK("same-script iframe insertion exposes a real named form target", value(f.context,
            "globalThis.dynamic=document.createElement('iframe');dynamic.name='dynamic';document.body.append(dynamic);"
            "f.target='dynamic';f.action='/dynamic';f.submit();dynamic.onload=()=>loads++;true") && f.untouched());
        CHECK("a listener assigned after submit receives the later frame load", f.pump([&] {
            return server.count("/dynamic") == 1 && value(f.context, "loads===1&&dynamic.contentDocument.getElementById('received')!==null");
        }));
    }
    {
        Fixture f(server.origin());
        CHECK("GET targets the selected frame and serializes its query", value(f.context,
            "f.method='get';f.action='/query?discard=1';f.submit();true") && f.untouched());
        CHECK("GET sends a real request without a POST body", f.pump([&] { return server.count("/query?key=value") == 1; }) &&
              server.last("/query?key=value").method == "GET" && server.last("/query?key=value").body.empty());
    }
    {
        Fixture f(server.origin());
        CHECK("submitter target overrides the form target", value(f.context,
            "b.setAttribute('formtarget','second');f.action='/override';f.requestSubmit(b);true"));
        CHECK("the submitter-selected frame receives its successful-control body", f.pump([&] {
            return server.count("/override") == 1 && value(f.context, "z.contentDocument.getElementById('received')!==null");
        }) && server.last("/override").body == "key=value&button=clicked" && f.untouched());
    }
    {
        Fixture f(server.origin());
        CHECK("a present empty submitter target means self", value(f.context,
            "b.setAttribute('formtarget','');f.action='/self';f.requestSubmit(b);true") && f.untouched());
        CHECK("empty target uses the source host only after its planned task", f.pump([&] { return f.posts == 1; }) &&
              f.last_url == server.origin() + "/self" && f.last_body == "key=value&button=clicked");
    }
    {
        Fixture f(server.origin());
        CHECK("first base target is used only when the form target is absent", value(f.context,
            "f.removeAttribute('target');const base=document.createElement('base');base.target='second';document.head.append(base);"
            "f.action='/base';f.submit();true"));
        CHECK("base-selected target receives the request", f.pump([&] {
            return server.count("/base") == 1 && value(f.context, "z.contentDocument.getElementById('received')!==null");
        }) && f.untouched());
    }
    {
        Fixture f(server.origin());
        CHECK("a present empty formaction overrides a nonempty form action", value(f.context,
            "f.target='_self';f.action='/ignored';b.setAttribute('formaction','');f.requestSubmit(b);true"));
        CHECK("empty formaction resolves to the owner document URL", f.pump([&] { return f.posts == 1; }) &&
              f.last_url == server.origin() + "/start");
    }
    {
        Fixture f(server.origin());
        CHECK("later submission replaces one form's pending target and copied body", value(f.context,
            "f.action='/superseded';f.submit();f.target='second';f.action='/replacement';x.value='new';f.submit();x.value='later';true") && queue(1));
        CHECK("only the replacement target and body are delivered", f.pump([&] {
            return server.count("/replacement") == 1 && value(f.context, "z.contentDocument.getElementById('received')!==null");
        }) && server.count("/superseded") == 0 && server.last("/replacement").body == "key=new" && f.untouched());
    }
    {
        Fixture f(server.origin());
        CHECK("multipart POST retains binary bytes including NUL", value(f.context,
            "f.action='/binary';f.enctype='multipart/form-data';const input=document.createElement('input');input.type='file';input.name='upload';"
            "const transfer=new DataTransfer();transfer.items.add(new File([new Uint8Array([65,0,66,255])],'bytes.bin',{type:'application/octet-stream'}));"
            "input.files=transfer.files;f.append(input);f.submit();true"));
        CHECK("binary upload reaches the named frame through real HTTP", f.pump([&] { return server.count("/binary") == 1; }) &&
              server.last("/binary").body.find(std::string("A\0B\xff", 4)) != std::string::npos &&
              server.last("/binary").headers["content-type"].find("multipart/form-data; boundary=") == 0 && f.untouched());
    }
    for (const char *target : {"missing-target", "_blank", "first\\0suffix", "first\\n<capture"}) {
        Fixture f(server.origin());
        std::string script = std::string("f.target='") + target + "';f.action='/must-not-submit-parent';f.submit();true";
        CHECK("unavailable or sanitized targets never fall back to the parent", value(f.context, script));
        f.settle();
        CHECK("unsupported new target retains the parent document and sends no request",
              f.untouched() && server.count("/must-not-submit-parent") == 0 && queue(0, true));
    }
    {
        Fixture f(server.origin());
        CHECK("noopener named target is not silently reused", value(f.context,
            "f.setAttribute('rel','noopener');f.action='/noopener';f.submit();true"));
        f.settle();
        CHECK("unsupported auxiliary form navigation sends no existing-frame request", f.untouched() &&
              server.count("/noopener") == 0 && queue(0, true));
    }
    {
        Fixture f(server.origin());
        CHECK("disconnected form submission is not planned", value(f.context, "f.remove();f.submit();true"));
        f.settle(); CHECK("disconnected form leaves its host and queue idle", f.untouched() && queue(0, true));
    }
    {
        Fixture f(server.origin());
        CHECK("removing a target invalidates its queued navigation", value(f.context,
            "f.action='/removed-target';f.submit();a.remove();true"));
        f.settle(); CHECK("removed target receives no request and releases planned bytes",
            f.untouched() && server.count("/removed-target") == 0 && queue(0, true));
    }
    {
        Fixture f(server.origin());
        CHECK("a planned task retains its genuine form identity after element removal", value(f.context,
            "f.action='/retained-form';f.submit();f.remove();true") && queue(1));
        CHECK("removing only the form does not replace its active source document", f.pump([&] {
            return server.count("/retained-form") == 1;
        }) && server.last("/retained-form").body == "key=value" && f.untouched());
    }
    {
        Fixture f(server.origin());
        JsCtx *c = child(f, server.origin() + "/child-owner");
        CHECK("borrowed parent submit resolves the actual child form owner", value(f.context,
            "globalThis.childForm=frames[2].document.getElementById('child');childForm.action='/child-owner-submit';"
            "childForm.target='first';HTMLFormElement.prototype.submit.call(childForm);true"));
        CHECK("borrowed submit uses the child referrer and real target", f.pump([&] {
            return server.count("/child-owner-submit") == 1;
        }) && server.last("/child-owner-submit").headers["referer"] == server.origin() + "/child-owner" && f.untouched() && !c->dead);
    }
    {
        Fixture f(server.origin());
        JsCtx *c = child(f, server.origin() + "/retired-source");
        CHECK("a source frame can plan a navigation before removal", value(c,
            "const form=document.getElementById('child');form.target='first';form.action='/retired-source-post';form.submit();true"));
        node_remove(c->frame_el);
        f.settle();
        CHECK("source removal cancels its planned request and owned bytes", server.count("/retired-source-post") == 0 &&
              f.untouched() && queue(0, true));
    }
    for (const char *method : {"get", "post"}) {
        Fixture f(server.origin());
        JsCtx *c = child(f, server.origin() + "/top-source");
        CHECK("cross-document top targets are explicitly unsupported", value(c,
            std::string("const form=document.getElementById('child');form.method='") + method +
            "';form.target='_top';form.action='/unsupported-child-top';form.submit();true"));
        f.settle();
        CHECK("child-to-top submission never substitutes target-host metadata", f.untouched() &&
              server.count("/unsupported-child-top") == 0 && queue(0, true));
    }
    {
        Fixture f(server.origin());
        JsCtx *c = child(f, other.origin() + "/cross-origin-source");
        CHECK("cross-origin sibling navigation is refused", value(c,
            "const form=document.getElementById('child');form.target='first';form.action='" + server.origin() +
            "/cross-sibling';form.submit();true"));
        f.settle(); CHECK("denied sibling target receives no cross-origin form", server.count("/cross-sibling") == 0 &&
            f.untouched() && queue(0, true));
    }
    for (bool allow : {false, true}) {
        Fixture f(server.origin());
        JsCtx *c = child(f, server.origin() + (allow ? "/allowed-source" : "/sandbox-source"),
                        allow ? "allow-scripts allow-same-origin allow-forms" : "allow-scripts allow-same-origin");
        const std::string path = allow ? "/sandbox-allowed" : "/sandbox-blocked";
        CHECK("sandbox form policy is an immutable native decision", value(c,
            "globalThis.submits=0;const form=document.getElementById('child');form.addEventListener('submit',()=>submits++);"
            "form.action='" + server.origin() + path + "';form.requestSubmit();submits===" + (allow ? "1" : "0")));
        if (allow) CHECK("allow-forms admits the genuine child POST", f.pump([&] { return server.count(path) == 1; }) && f.untouched());
        else {
            node_set_attr(c->frame_el, "sandbox", "allow-scripts allow-same-origin allow-forms");
            CHECK("changing the sandbox attribute does not relax the current document", value(c,
                "document.getElementById('child').submit();true"));
            f.settle(); CHECK("sandbox denial sends no request", server.count(path) == 0 && f.untouched() && queue(0, true));
        }
    }
    {
        Fixture f(server.origin(), "form-action 'none'");
        CHECK("CSP form-action blocks a real form submission", value(f.context, "f.action='/csp-denied';f.submit();true"));
        f.settle(); CHECK("CSP denial retains the source and sends no request", server.count("/csp-denied") == 0 &&
            f.untouched() && queue(0, true));
    }
    {
        Fixture f(server.origin(), "default-src 'none'");
        CHECK("form-action does not inherit default-src", value(f.context,
            "f.target='_self';f.action='/default-not-form';f.submit();true"));
        CHECK("an absent form-action permits the existing top navigation", f.pump([&] { return f.posts == 1; }) &&
              f.last_url == server.origin() + "/default-not-form");
    }
    {
        Fixture f(server.origin(), "form-action 'self'");
        server.redirect_to(other.origin() + "/redirect-escaped");
        CHECK("redirects retain the initiating form policy", value(f.context, "f.action='/redirect-blocked';f.submit();true"));
        CHECK("initial allowed form hop reaches the real server", f.pump([&] { return server.count("/redirect-blocked") == 1; }));
        f.pump([] { return net_pending() == 0; });
        CHECK("redirect to the disallowed origin is not sent", other.count("/redirect-escaped") == 0 && f.untouched());
    }
    {
        Fixture f(server.origin());
        CHECK("normal frame embedding protections remain active for POST", value(f.context,
            "f.action='/embedding-denied';f.submit();true"));
        CHECK("denied embedded response never becomes the child document", f.pump([&] {
            return server.count("/embedding-denied") == 1 && value(f.context, "loads===1");
        }) && value(f.context, "a.contentDocument.getElementById('received')===null") && f.untouched());
    }
    CHECK("every closed fixture releases its form navigation queue", queue(0, true));
    const double deadline = now_ms() + 3000;
    while (net_pending() && now_ms() < deadline) { net_poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    const bool network_drained = net_pending() == 0;
    CHECK("all real form HTTP requests finish before network teardown", network_drained);
    const bool workers_drained = js_workers_shutdown(5000);
    CHECK("worker ownership remains drained before subsystem teardown", workers_drained);
    printf("RESULTS form-navigation %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks); fflush(stdout);
    if (!network_drained || !workers_drained) std::_Exit(1);
    net_shutdown(); return failures ? 1 : 0;
}
