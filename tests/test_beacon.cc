/* Public navigator.sendBeacon regressions against real loopback HTTP servers.
 * These are product regressions, not additions to official WPT pass counts.
 */
#include "../src/js/js_int.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <poll.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <chrono>
#include <functional>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" void font_init(void);
static int checks, failures;
#define CHECK(name, condition) do { ++checks; bool passed = !!(condition); \
    fprintf(passed ? stdout : stderr, "%s %s\n", passed ? "PASS" : "FAIL", name); \
    if (!passed) ++failures; } while (0)

struct Receipt {
    std::string method, path, body;
    std::map<std::string, std::string> headers;
};
class Server {
    int listener = -1;
    std::thread thread;
    std::atomic<bool> stopping{false};
    mutable std::mutex mutex;
    std::vector<Receipt> receipts;
    std::map<std::string, std::pair<int, std::string>> redirects;
    static bool write_all(int fd, const std::string &text) {
        size_t offset = 0;
        while (offset < text.size()) {
            ssize_t count = send(fd, text.data() + offset, text.size() - offset, 0);
            if (count <= 0) return false;
            offset += (size_t)count;
        }
        return true;
    }
    bool read_request(int fd, Receipt &r) {
        std::string input;
        char buffer[4096];
        size_t head_end;
        while ((head_end = input.find("\r\n\r\n")) == std::string::npos) {
            ssize_t n = recv(fd, buffer, sizeof buffer, 0);
            if (n <= 0 || input.size() + (size_t)n > 96 * 1024) return false;
            input.append(buffer, (size_t)n);
        }
        size_t line_end = input.find("\r\n"), first = input.find(' '), second = input.find(' ', first + 1);
        if (first == std::string::npos || second == std::string::npos || second > line_end) return false;
        r.method = input.substr(0, first); r.path = input.substr(first + 1, second - first - 1);
        for (size_t offset = line_end + 2; offset < head_end;) {
            size_t end = input.find("\r\n", offset), colon = input.find(':', offset);
            if (end == std::string::npos || colon == std::string::npos || colon >= end) return false;
            std::string key = input.substr(offset, colon - offset);
            for (char &c : key) c = (char)std::tolower((unsigned char)c);
            size_t value = colon + 1; while (value < end && (input[value] == ' ' || input[value] == '\t')) value++;
            r.headers[key] = input.substr(value, end - value);
            offset = end + 2;
        }
        size_t length = 0;
        if (r.headers.count("content-length")) {
            char *end = nullptr;
            unsigned long parsed = strtoul(r.headers["content-length"].c_str(), &end, 10);
            if (!end || *end || parsed > 65536) return false;
            length = (size_t)parsed;
        }
        r.body = input.substr(head_end + 4);
        while (r.body.size() < length) {
            ssize_t n = recv(fd, buffer, std::min(sizeof buffer, length - r.body.size()), 0);
            if (n <= 0) return false;
            r.body.append(buffer, (size_t)n);
        }
        return r.body.size() == length;
    }
    void serve() {
        while (!stopping.load()) {
            pollfd poller{listener, POLLIN, 0};
            if (poll(&poller, 1, 50) <= 0) continue;
            int fd = accept(listener, nullptr, nullptr);
            if (fd < 0) continue;
            timeval timeout{1, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
            Receipt r;
            if (!read_request(fd, r)) { close(fd); continue; }
            std::pair<int, std::string> redirect{};
            {
                std::lock_guard<std::mutex> lock(mutex);
                receipts.push_back(r);
                auto found = redirects.find(r.path);
                if (found != redirects.end()) redirect = found->second;
            }
            if (r.path == "/hold" || r.path == "/teardown") {
                double deadline = now_ms() + 3000;
                while (!released.load() && !stopping.load() && now_ms() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (r.path == "/large-response") {
                write_all(fd, "HTTP/1.1 200 OK\r\nContent-Length: 1000000000\r\nConnection: close\r\n\r\n");
                large_headers.store(true);
                double deadline = now_ms() + 3000;
                while (!released.load() && !stopping.load() && now_ms() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                close(fd); continue;
            }
            int status = redirect.first ? redirect.first :
                (r.method == "OPTIONS" && r.path.find("deny") != std::string::npos) ? 403 : 204;
            std::string response = "HTTP/1.1 " + std::to_string(status) + " Test\r\nContent-Length: 0\r\nConnection: close\r\n";
            if (!redirect.second.empty()) response += "Location: " + redirect.second + "\r\n";
            std::string origin = r.headers.count("origin") ? r.headers["origin"] : "null";
            if (r.path.find("wildcard") != std::string::npos) origin = "*";
            response += "Access-Control-Allow-Origin: " + origin + "\r\nAccess-Control-Allow-Credentials: true\r\n";
            if (r.path.find("duplicate") != std::string::npos) response += "Access-Control-Allow-Origin: https://different.invalid\r\n";
            response += "Access-Control-Allow-Headers: content-type\r\n\r\n";
            write_all(fd, response);
            close(fd);
        }
    }
public:
    int port = 0;
    std::atomic<bool> released{true}, large_headers{false};
    Server() {
        listener = socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) return;
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener, (sockaddr *)&address, sizeof address) || listen(listener, 16)) { close(listener); listener = -1; return; }
        socklen_t size = sizeof address;
        if (getsockname(listener, (sockaddr *)&address, &size)) { close(listener); listener = -1; return; }
        port = ntohs(address.sin_port);
        thread = std::thread([this] { serve(); });
    }
    ~Server() {
        stopping.store(true); released.store(true);
        if (thread.joinable()) thread.join();
        if (listener >= 0) close(listener);
    }
    std::string origin() const { return "http://127.0.0.1:" + std::to_string(port); }
    void redirect(const std::string &path, int status, const std::string &target) {
        std::lock_guard<std::mutex> lock(mutex); redirects[path] = {status, target};
    }
    size_t count(const std::string &path, const std::string &method) const {
        std::lock_guard<std::mutex> lock(mutex);
        size_t n = 0; for (const auto &r : receipts) if (r.path == path && r.method == method) ++n;
        return n;
    }
    Receipt receipt(const std::string &path, const std::string &method) const {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto &r : receipts) if (r.path == path && r.method == method) return r;
        return {};
    }
};

static bool value(JsCtx *c, const std::string &text) {
    if (!c) return false;
    JS_ENTER(c);
    v8::TryCatch caught(iso);
    v8::Local<v8::Script> script; v8::Local<v8::Value> result;
    bool ok = v8::Script::Compile(ctx, jstr(iso, text.c_str())).ToLocal(&script) &&
        script->Run(ctx).ToLocal(&result) && result->BooleanValue(iso);
    if (caught.HasCaught()) fprintf(stderr, "JavaScript exception: %s\n", jcstr(iso, caught.Exception()).c_str());
    return ok;
}
struct Fixture {
    Document *document = nullptr;
    JsCtx *context = nullptr;
    explicit Fixture(const std::string &url) {
        document = doc_new(url.c_str());
        const char *html = "<!doctype html><html><body></body></html>";
        html_parse(document, html, strlen(html));
        JsHost host{}; context = js_new(document, &host);
    }
    ~Fixture() { if (context) js_free(context); if (document) doc_free(document); }
};
/* Match the existing URL/DOM regression fixture: real child V8 contexts with
   native frame ownership, initialized from in-memory documents. Their Beacon
   requests below still travel through the real loopback TCP server. */
static JsCtx *child_context(JsCtx *parent, const std::string &url, const char *sandbox = nullptr) {
    if (!parent) return nullptr;
    Node *element = node_new_element(parent->doc, "iframe", NS_HTML);
    node_set_attr(element, "src", url.c_str());
    if (sandbox) node_set_attr(element, "sandbox", sandbox);
    node_append(parent->doc->body, element);
    Document *child = doc_new(url.c_str());
    const char *html = "<!doctype html><html><body></body></html>";
    html_parse(child, html, strlen(html));
    JsHost host{};
    return js_new_ex(child, &host, parent, element);
}
static bool wait_for(const std::function<bool()> &condition, double timeout = 3000) {
    double end = now_ms() + timeout;
    while (now_ms() < end) {
        net_poll();
        if (condition()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    net_poll(); return condition();
}
static bool drain(double timeout = 3000) { return wait_for([] { return net_pending() == 0; }, timeout); }

int main(int argc, char **argv) {
    (void)argc;
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    Server a, b;
    CHECK("two real loopback origins start", a.port > 0 && b.port > 0 && a.port != b.port);
    if (!a.port || !b.port) return 1;
    {
        Fixture f(a.origin() + "/page");
        CHECK("fixture exposes the real second origin", value(f.context, "globalThis.beaconOther='" + b.origin() + "';true"));
        CHECK("sendBeacon requires a URL", value(f.context, R"JS((()=>{try{navigator.sendBeacon();}catch(e){return e instanceof TypeError;}return false;})())JS"));
        CHECK("sendBeacon rejects an invalid receiver", value(f.context, R"JS((()=>{try{navigator.sendBeacon.call({},'/x');}catch(e){return e instanceof TypeError;}return false;})())JS"));
        CHECK("sendBeacon rejects Symbol URL input", value(f.context, R"JS((()=>{try{navigator.sendBeacon(Symbol());}catch(e){return e instanceof TypeError;}return false;})())JS"));
        CHECK("sendBeacon rejects Symbol body input", value(f.context, R"JS((()=>{try{navigator.sendBeacon('/x',Symbol());}catch(e){return e instanceof TypeError;}return false;})())JS"));
        CHECK("URL conversion exceptions are preserved", value(f.context, R"JS((()=>{const marker={};try{navigator.sendBeacon({toString(){throw marker;}});}catch(e){return e===marker;}return false;})())JS"));
        CHECK("BodyInit conversion precedes URL algorithm validation", value(f.context, R"JS((()=>{const marker={},seen=[];try{navigator.sendBeacon({toString(){seen.push('url');return 'http://[';}},{toString(){seen.push('body');throw marker;}});}catch(e){return e===marker&&seen.join(',')==='url,body';}return false;})())JS"));
        CHECK("non-HTTP schemes throw instead of reporting queued", value(f.context, R"JS(['data:text/plain,x','file:///tmp/x','ftp://example.test/x','about:blank','javascript:1'].every(u=>{try{navigator.sendBeacon(u);}catch(e){return e instanceof TypeError;}return false;}))JS"));
        CHECK("forbidden host bytes reject at the length-aware URL boundary", value(f.context, R"JS(['http://[','http://a\0b/','http://ho%00st/'].every(u=>{try{navigator.sendBeacon(u);}catch(e){return e instanceof TypeError;}return false;}))JS"));
        CHECK("ReadableStream bodies fail keepalive extraction", value(f.context, R"JS((()=>{try{navigator.sendBeacon('/stream',new ReadableStream());}catch(e){return e instanceof TypeError;}return false;})())JS"));
        CHECK("a body larger than 64 KiB is not queued", value(f.context, R"JS(navigator.sendBeacon('/oversize',new Uint8Array(65537))===false)JS"));

        CHECK("text beacon queues immediately", value(f.context, R"JS(navigator.sendBeacon('/text','hello\0\u03a9\ud800')===true)JS"));
        CHECK("text beacon reaches the real HTTP server", wait_for([&] { return a.count("/text", "POST") == 1; }));
        Receipt text = a.receipt("/text", "POST");
        const char expected_text[] = {'h','e','l','l','o',0,(char)0xce,(char)0xa9,(char)0xef,(char)0xbf,(char)0xbd};
        CHECK("text UTF-8 preserves NUL and replaces a lone surrogate", text.body == std::string(expected_text, sizeof expected_text));
        CHECK("text request has the actual body MIME type and origin", text.headers["content-type"] == "text/plain;charset=UTF-8" && text.headers["origin"] == a.origin());
        CHECK("completed text request releases its native job", drain());

        CHECK("ArrayBufferView input queues its exact slice", value(f.context, R"JS(globalThis.beaconBytes=new Uint8Array([9,0,255,8]);navigator.sendBeacon('/bytes',beaconBytes.subarray(1,3)))JS"));
        CHECK("mutating source bytes after queueing succeeds", value(f.context, R"JS(beaconBytes.fill(42);true)JS"));
        CHECK("queued buffer owns its bytes independently", wait_for([&] { return a.count("/bytes", "POST") == 1; }) && a.receipt("/bytes", "POST").body == std::string("\0\xff", 2));
        CHECK("BufferSource request adds no invented MIME type", a.receipt("/bytes", "POST").headers.count("content-type") == 0);
        CHECK("buffer request drains", drain());

        CHECK("URLSearchParams body is queued", value(f.context, R"JS(navigator.sendBeacon('/params',new URLSearchParams([['a','x y'],['b','+']])))JS"));
        CHECK("URLSearchParams uses form encoding and MIME type", wait_for([&] { return a.count("/params", "POST") == 1; }) &&
            a.receipt("/params", "POST").body == "a=x+y&b=%2B" && a.receipt("/params", "POST").headers["content-type"] == "application/x-www-form-urlencoded;charset=UTF-8");
        CHECK("plain objects use the BodyInit string member", value(f.context, R"JS(navigator.sendBeacon('/object',{toString(){return 'converted';}}))JS"));
        CHECK("converted body reaches the server", wait_for([&] { return a.count("/object", "POST") == 1; }) && a.receipt("/object", "POST").body == "converted");
        CHECK("null and omitted bodies can be queued", value(f.context, R"JS(navigator.sendBeacon('/null',null)&&navigator.sendBeacon('/omitted'))JS"));
        CHECK("empty-body beacons are actual zero-length POSTs", wait_for([&] { return a.count("/null", "POST") == 1 && a.count("/omitted", "POST") == 1; }) &&
            a.receipt("/null", "POST").body.empty() && a.receipt("/omitted", "POST").headers["content-length"] == "0");
        CHECK("basic body cases drain", drain());

        CHECK("FormData body is queued", value(f.context, R"JS((()=>{const form=new FormData();form.append('field','value');form.append('file',new Blob([new Uint8Array([0,255])],{type:'application/octet-stream'}),'sample.bin');return navigator.sendBeacon('/form',form);})())JS"));
        CHECK("FormData reaches the server with its boundary and binary part", wait_for([&] { return a.count("/form", "POST") == 1; }) &&
            a.receipt("/form", "POST").headers["content-type"].find("multipart/form-data; boundary=") == 0 &&
            a.receipt("/form", "POST").body.find("name=\"field\"\r\n\r\nvalue") != std::string::npos &&
            a.receipt("/form", "POST").body.find(std::string("\0\xff", 2)) != std::string::npos);
        CHECK("FormData request drains", drain());

        cookies_set_document((a.origin() + "/page").c_str(), (a.origin() + "/page").c_str(), "beacon_cookie=present; Path=/");
        CHECK("cross-origin safelisted body queues", value(f.context, R"JS(navigator.sendBeacon(beaconOther+'/plain','cross'))JS"));
        CHECK("safelisted body is delivered without OPTIONS", wait_for([&] { return b.count("/plain", "POST") == 1; }) && b.count("/plain", "OPTIONS") == 0);
        CHECK("Beacon actual request includes credentials", b.receipt("/plain", "POST").headers["cookie"].find("beacon_cookie=present") != std::string::npos);
        CHECK("cross-origin JSON queues for native preflight", value(f.context, R"JS(navigator.sendBeacon(beaconOther+'/allow',new Blob(['{"ok":true}'],{type:'application/json'})))JS"));
        CHECK("successful native preflight precedes actual JSON delivery", wait_for([&] { return b.count("/allow", "POST") == 1; }) && b.count("/allow", "OPTIONS") == 1);
        Receipt preflight = b.receipt("/allow", "OPTIONS"), json = b.receipt("/allow", "POST");
        CHECK("preflight excludes cookies and names the unsafe header", preflight.headers.count("cookie") == 0 && preflight.headers["access-control-request-method"] == "POST" && preflight.headers["access-control-request-headers"] == "content-type");
        CHECK("authorized JSON body and credentials survive preflight", json.body == "{\"ok\":true}" && json.headers["content-type"] == "application/json" && json.headers["cookie"].find("beacon_cookie=present") != std::string::npos);
        CHECK("allowed cross-origin requests drain", drain());
        CHECK("denied preflight is still a queued asynchronous operation", value(f.context, R"JS(navigator.sendBeacon(beaconOther+'/deny',new Blob(['blocked'],{type:'application/json'})))JS"));
        CHECK("a denied preflight prevents the POST", drain() && b.count("/deny", "OPTIONS") == 1 && b.count("/deny", "POST") == 0);
        CHECK("credentialed wildcard and duplicate CORS cases queue", value(f.context, R"JS(navigator.sendBeacon(beaconOther+'/wildcard',new Blob(['blocked'],{type:'application/json'}))&&navigator.sendBeacon(beaconOther+'/duplicate',new Blob(['blocked'],{type:'application/json'})))JS"));
        CHECK("wildcard or duplicate CORS authorization never sends the body", drain() && b.count("/wildcard", "OPTIONS") == 1 && b.count("/duplicate", "OPTIONS") == 1 && b.count("/wildcard", "POST") == 0 && b.count("/duplicate", "POST") == 0);

        a.redirect("/redirect307", 307, b.origin() + "/allow307");
        a.redirect("/redirect-denied", 307, b.origin() + "/deny-redirect");
        a.redirect("/redirect303", 303, b.origin() + "/after303");
        CHECK("a same-origin JSON POST can queue before a cross-origin redirect", value(f.context, R"JS(navigator.sendBeacon('/redirect307',new Blob(['redirect-body'],{type:'application/json'})))JS"));
        CHECK("307 requires new-origin preflight and preserves exact body", drain() && b.count("/allow307", "OPTIONS") == 1 && b.receipt("/allow307", "POST").body == "redirect-body");
        CHECK("a redirected denied preflight queues", value(f.context, R"JS(navigator.sendBeacon('/redirect-denied',new Blob(['must-not-arrive'],{type:'application/json'})))JS"));
        CHECK("redirected unsafe body never bypasses denied preflight", drain() && b.count("/deny-redirect", "OPTIONS") == 1 && b.count("/deny-redirect", "POST") == 0);
        CHECK("303 transition queues", value(f.context, R"JS(navigator.sendBeacon('/redirect303',new Blob(['discard'],{type:'application/json'})))JS"));
        CHECK("303 follows as GET without the old body or MIME type", drain() && b.count("/after303", "GET") == 1 && b.receipt("/after303", "GET").body.empty() && b.receipt("/after303", "GET").headers.count("content-type") == 0);

        a.released.store(false);
        CHECK("two in-flight 32 KiB beacons fit one document quota", value(f.context, R"JS(navigator.sendBeacon('/hold',new Uint8Array(32768))&&navigator.sendBeacon('/hold',new Uint8Array(32768)))JS"));
        CHECK("a further byte cannot exceed the in-flight 64 KiB quota", value(f.context, R"JS(navigator.sendBeacon('/quota-rejected','x')===false)JS"));
        a.released.store(true);
        CHECK("quota-held requests actually complete", drain() && a.count("/hold", "POST") == 2 && a.count("/quota-rejected", "POST") == 0);
        CHECK("completion makes quota reusable", value(f.context, R"JS(navigator.sendBeacon('/quota-reused',new Uint8Array(65536)))JS") && drain() && a.receipt("/quota-reused", "POST").body.size() == 65536);

        a.released.store(false); a.large_headers.store(false);
        CHECK("ignored response body test queues", value(f.context, R"JS(navigator.sendBeacon('/large-response','small'))JS"));
        CHECK("server sends headers advertising a one-gigabyte unused body", wait_for([&] { return a.large_headers.load(); }));
        CHECK("Beacon completes before any advertised response body is sent", drain(1000));
        a.released.store(true);
    }
    CHECK("all first-document requests released before teardown test", drain());
    {
        Fixture site(a.origin() + "/page");
        cookies_set_document((a.origin() + "/page").c_str(), (a.origin() + "/page").c_str(),
                             "beacon_strict=site-secret; Path=/; SameSite=Strict");
        JsCtx *cross = child_context(site.context, "http://cross-site.invalid/frame");
        CHECK("a real cross-site child context queues a request to its top site", value(cross,
            "navigator.sendBeacon('" + a.origin() + "/cross-child','cross-child')"));
        CHECK("cross-site child cannot borrow the top site's Strict cookie", drain() && a.count("/cross-child", "POST") == 1 &&
            a.receipt("/cross-child", "POST").headers["cookie"].find("beacon_strict=") == std::string::npos);
        JsCtx *nested = child_context(cross, a.origin() + "/nested");
        CHECK("a same-site child under a cross-site ancestor queues", value(nested,
            "navigator.sendBeacon('" + a.origin() + "/cross-ancestor','cross-ancestor')"));
        CHECK("a cross-site intermediate ancestor prevents Strict cookie delivery", drain() && a.count("/cross-ancestor", "POST") == 1 &&
            a.receipt("/cross-ancestor", "POST").headers["cookie"].find("beacon_strict=") == std::string::npos);
        JsCtx *secure = child_context(site.context, "https://127.0.0.1/frame");
        JsCtx *scheme_nested = child_context(secure, a.origin() + "/scheme-nested");
        CHECK("a child under a different-scheme ancestor queues", value(scheme_nested,
            "navigator.sendBeacon('" + a.origin() + "/scheme-ancestor','scheme-ancestor')"));
        CHECK("scheme mismatch in the ancestor chain is cross-site for cookies", drain() && a.count("/scheme-ancestor", "POST") == 1 &&
            a.receipt("/scheme-ancestor", "POST").headers["cookie"].find("beacon_strict=") == std::string::npos);
        JsCtx *sandboxed = child_context(site.context, a.origin() + "/sandboxed", "allow-scripts");
        CHECK("same-site sandbox uses its URI for the cookie-site audit", sandboxed && sandboxed->sb_opaque && value(sandboxed,
            "navigator.sendBeacon('" + a.origin() + "/sandbox-cookie','sandbox-cookie')"));
        CHECK("same-site sandbox keeps Strict cookies while serializing Origin as null", drain() && a.count("/sandbox-cookie", "POST") == 1 &&
            a.receipt("/sandbox-cookie", "POST").headers["cookie"].find("beacon_strict=site-secret") != std::string::npos &&
            a.receipt("/sandbox-cookie", "POST").headers["origin"] == "null");
        JsCtx *inherited = child_context(site.context, "about:blank");
        CHECK("an inherited-origin blank child queues", value(inherited,
            "navigator.sendBeacon('" + a.origin() + "/inherited-cookie','inherited-cookie')"));
        CHECK("inherited origin remains same-site for cookie delivery", drain() && a.count("/inherited-cookie", "POST") == 1 &&
            a.receipt("/inherited-cookie", "POST").headers["cookie"].find("beacon_strict=site-secret") != std::string::npos);
    }
    a.released.store(false);
    {
        Fixture dying(a.origin() + "/page");
        CHECK("a document can queue a beacon immediately before destruction", value(dying.context, R"JS(navigator.sendBeacon('/teardown','retained-after-context-destruction'))JS"));
        CHECK("the queued body reaches the server while response is held", wait_for([&] { return a.count("/teardown", "POST") == 1; }));
    }
    a.released.store(true);
    CHECK("context destruction does not cancel its independent queued job", drain() && a.receipt("/teardown", "POST").body == "retained-after-context-destruction");
    {
        Fixture blocked(a.origin() + "/page");
        free(blocked.document->csp); blocked.document->csp = xstrdup("connect-src 'none'");
        CHECK("CSP denial remains asynchronous after successful queue admission", value(blocked.context, R"JS(navigator.sendBeacon('/csp-blocked','not-sent'))JS"));
        CHECK("CSP-prohibited destination receives no request", drain() && a.count("/csp-blocked", "POST") == 0);
    }
    CHECK("all detached jobs finish within the regression deadline", drain());
    net_shutdown();
    printf("beacon: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
