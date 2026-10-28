/* Public form encodings verified against complete real loopback HTTP bytes.
 * Custom native regressions are separate from official WPT assertion totals. */
#include "../src/js/js_int.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <poll.h>
#include <dlfcn.h>
#include <unistd.h>
#include <algorithm>
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
            const ssize_t n = send(fd, value.data() + done, value.size() - done, 0);
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
            int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
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
static bool evaluate(JsCtx *context, const std::string &source) {
    JS_ENTER(context); v8::TryCatch caught(iso);
    v8::Local<v8::Script> script; v8::Local<v8::Value> result;
    const bool okay = v8::Script::Compile(ctx, jstr(iso, source.c_str())).ToLocal(&script) &&
        script->Run(ctx).ToLocal(&result);
    if (!okay && caught.HasCaught()) fprintf(stderr, "form encoding fixture: %s\n", jcstr(iso, caught.Exception()).c_str());
    iso->PerformMicrotaskCheckpoint();
    return okay && result->BooleanValue(iso);
}

struct Fixture {
    Document *document;
    JsCtx *context;
    Server &server;
    explicit Fixture(Server &server, WebEncoding encoding = WEB_ENC_UTF8) : server(server) {
        document = doc_new((server.origin() + "/start").c_str());
        const char *html = "<!doctype html><html><body><iframe id='target' name='target'></iframe>"
            "<form id='form' target='target' method='post'></form></body></html>";
        html_parse(document, html, strlen(html));
        document->encoding = encoding;
        JsHost host{};
        context = js_new(document, &host);
        frames_scan(context);
        evaluate(context, R"JS(
            globalThis.f=document.getElementById('form');
            globalThis.targetFrame=document.getElementById('target');
            globalThis.loads=0; targetFrame.onload=()=>loads++;
            globalThis.add=(name,value,type='hidden')=>{
                const input=document.createElement('input');
                input.type=type; input.name=name; input.value=value; f.append(input); return input;
            };
            globalThis.addFile=(name,filename,bytes=[65,0,66,13,10,255])=>{
                const input=document.createElement('input'); input.type='file'; input.name=name;
                const transfer=new DataTransfer();
                transfer.items.add(new File([new Uint8Array(bytes)],filename,{type:'text/plain'}));
                input.files=transfer.files; f.append(input); return input;
            };
            true;
        )JS");
    }
    ~Fixture() { js_release_pins(); js_free(context); doc_free(document); }
    bool pump(const std::function<bool()> &done) {
        const double deadline = now_ms() + 2500;
        do {
            net_poll(); js_tick(context);
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (now_ms() < deadline);
        return done();
    }
    bool submit(const char *path, const std::string &script, Receipt &receipt, const char *request_path = nullptr) {
        const std::string actual = request_path ? request_path : path;
        const bool setup = evaluate(context, "f.action='" + std::string(path) + "';" + script + ";f.submit();true");
        const bool arrived = setup && pump([&] { return server.count(actual) == 1 && evaluate(context, "loads===1"); });
        receipt = server.last(actual);
        return arrived;
    }
};

struct ExpectedPart {
    std::string name, bytes, filename, type;
    bool file = false;
};
static ExpectedPart field(std::string name, std::string bytes) {
    return {std::move(name), std::move(bytes), {}, {}, false};
}
static ExpectedPart file(std::string name, std::string filename, std::string bytes) {
    return {std::move(name), std::move(bytes), std::move(filename), "text/plain", true};
}
static bool exact_bytes(const std::string &actual, const std::string &expected) {
    if (actual == expected) return true;
    size_t offset = 0;
    while (offset < actual.size() && offset < expected.size() && actual[offset] == expected[offset]) ++offset;
    fprintf(stderr, "raw request mismatch at byte %zu; actual=%zu bytes, expected=%zu bytes\n",
            offset, actual.size(), expected.size());
    return false;
}
static bool multipart(const Receipt &receipt, const std::vector<ExpectedPart> &parts) {
    const auto type = receipt.headers.find("content-type");
    const std::string prefix = "multipart/form-data; boundary=";
    if (receipt.method != "POST" || type == receipt.headers.end() || type->second.rfind(prefix, 0) != 0) return false;
    const std::string boundary = type->second.substr(prefix.size());
    if (boundary.empty() || boundary.size() > 200 || boundary.find_first_of("\r\n") != std::string::npos) return false;
    std::string expected;
    for (const auto &part : parts) {
        expected += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + part.name + "\"";
        if (part.file) expected += "; filename=\"" + part.filename + "\"\r\nContent-Type: " + part.type;
        expected += "\r\n\r\n" + part.bytes + "\r\n";
    }
    expected += "--" + boundary + "--\r\n";
    return exact_bytes(receipt.body, expected);
}
static std::string file_bytes() { return std::string("A\0B\r\n\xff", 6); }
static bool empty_form_queue() {
    using Counter = void (*)(size_t *, size_t *);
    const auto counter = reinterpret_cast<Counter>(dlsym(RTLD_MAIN_ONLY, "js_form_navigations_pending"));
    if (!counter) return false;
    size_t count = 999, bytes = 999;
    counter(&count, &bytes);
    return count == 0 && bytes == 0;
}

int main(int argc, char **argv) {
    (void)argc;
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    Server server;
    CHECK("real loopback form server initializes", server.port != 0);
    if (!server.port) return 1;
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/ascii-multipart", R"JS(
            f.enctype='multipart/form-data'; add('key','value'); addFile('upload','bytes.bin');
        )JS", r);
        CHECK("ASCII multipart and raw binary file bytes remain unchanged", sent && multipart(r,
            {field("key", "value"), file("upload", "bytes.bin", file_bytes())}));
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/utf8-newlines", R"JS(
            f.enctype='multipart/form-data'; f.acceptCharset='UtF-8';
            add('_charset_','author value'); add('na\rme\nk\r\nq"','a\rb\nc\r\nd');
            addFile('file','A\rB\nC\r\nD"%E\\F');
        )JS", r);
        CHECK("multipart distinguishes normalized fields from literal filename newlines", sent && multipart(r, {
            field("_charset_", "UTF-8"), field("na%0D%0Ame%0D%0Ak%0D%0Aq%22", "a\r\nb\r\nc\r\nd"),
            file("file", "A%0DB%0AC%0D%0AD%22%E\\F", file_bytes())}));
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/surrogate-filename", R"JS(
            f.enctype='multipart/form-data'; f.acceptCharset='UTF-8'; addFile('file','left\ud800right');
        )JS", r);
        CHECK("UTF-8 form serialization replaces an unpaired filename surrogate", sent && multipart(r,
            {file("file", "left\xef\xbf\xbdright", file_bytes())}));
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/iso2022-multipart", R"JS(
            f.enctype='multipart/form-data'; f.acceptCharset='ISO-2022-JP'; add('_charset_','');
            add('\u661f\ud83c\udf1f\u661f','\u00a5\u203e~\u00e9');
            addFile('file','\u661f\ud83c\udf1f\u661f');
        )JS", r);
        const std::string stars = "\x1b$B@1\x1b(B&#127775;\x1b$B@1\x1b(B";
        CHECK("ISO-2022-JP form fields and filenames retain state around HTML fallback", sent && multipart(r, {
            field("_charset_", "ISO-2022-JP"), field(stars, "\x1b(J\\~\x1b(B~&#233;"),
            file("file", stars, file_bytes())}));
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/windows1252-multipart", R"JS(
            f.enctype='multipart/form-data'; f.acceptCharset='windows-1252'; add('_charset_','');
            add('\u00e9\u20ac','\u00a3\ud83d\ude02'); addFile('file','\u00e9\u20ac\u661f');
        )JS", r);
        CHECK("windows-1252 form bytes and decimal fallback leave file bytes intact", sent && multipart(r, {
            field("_charset_", "windows-1252"), field("\xe9\x80", "\xa3&#128514;"),
            file("file", "\xe9\x80&#26143;", file_bytes())}));
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/xuser-multipart", R"JS(
            f.enctype='multipart/form-data'; f.acceptCharset='x-user-defined'; add('_charset_','');
            add('\uf780\uf7ff','\uf7f0\u00e9'); addFile('file','\uf780\uf7ff\u00e9');
        )JS", r);
        CHECK("x-user-defined form encoding uses private-use bytes and HTML fallback", sent && multipart(r, {
            field("_charset_", "x-user-defined"), field("\x80\xff", "\xf0&#233;"),
            file("file", "\x80\xff&#233;", file_bytes())}));
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/nul-punctuation", R"JS(
            f.enctype='multipart/form-data'; f.acceptCharset='UTF-8';
            add('x\0%\\"','v\0&<>'); addFile('file','n\0%\\"');
        )JS", r);
        CHECK("multipart preserves NUL percent backslash and non-header punctuation", sent && multipart(r, {
            field(std::string("x\0%\\%22", 7), std::string("v\0&<>", 5)),
            file("file", std::string("n\0%\\%22", 7), file_bytes())}));
    }
    {
        Fixture f(server, WEB_ENC_WINDOWS_1252); Receipt r;
        const bool sent = f.submit("/document-encoding", "add('_charset_','');add('v','\\u00e9')", r);
        CHECK("missing accept-charset selects the owner document encoding", sent &&
            exact_bytes(r.body, "_charset_=windows-1252&v=%E9"));
    }
    {
        Fixture f(server, WEB_ENC_WINDOWS_1252); Receipt r;
        const bool sent = f.submit("/empty-accept", "f.acceptCharset='';add('_charset_','');add('v','\\u00e9')", r);
        CHECK("present empty accept-charset selects UTF-8", sent &&
            exact_bytes(r.body, "_charset_=UTF-8&v=%C3%A9"));
    }
    {
        Fixture f(server, WEB_ENC_WINDOWS_1252); Receipt r;
        const bool sent = f.submit("/unknown-accept", "f.acceptCharset='bogus unavailable';add('_charset_','');add('v','\\u00e9')", r);
        CHECK("unknown accept-charset labels select UTF-8", sent &&
            exact_bytes(r.body, "_charset_=UTF-8&v=%C3%A9"));
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/first-known-label", R"JS(
            f.acceptCharset=' \tbad\nLaTiN1\futf-8\r'; add('_charset_',''); add('v','\u00e9');
        )JS", r);
        CHECK("first recognized label uses all ASCII whitespace and canonical naming", sent &&
            exact_bytes(r.body, "_charset_=windows-1252&v=%E9"));
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/label-nul", R"JS(
            f.acceptCharset='utf-8\0 windows-1252'; add('_charset_',''); add('v','\u00e9');
        )JS", r);
        CHECK("an embedded NUL does not truncate an accept-charset token", sent &&
            exact_bytes(r.body, "_charset_=windows-1252&v=%E9"));
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/utf16-output", "f.acceptCharset='utf-16le windows-1252';add('_charset_','');add('v','\\u00e9')", r);
        CHECK("UTF-16 form labels map to UTF-8 output encoding", sent &&
            exact_bytes(r.body, "_charset_=UTF-8&v=%C3%A9"));
    }
    {
        Fixture f(server);
        CHECK("forged and Proxy form receivers cannot read controls or enqueue requests", evaluate(f.context, R"JS(
            let touched=0;
            const fake={get elements(){touched++;return [];},get acceptCharset(){touched++;return 'UTF-8';}};
            const proxy=new Proxy(f,{get(){touched++;return undefined;}});
            for(const receiver of [fake,Object.create(HTMLFormElement.prototype),proxy]) {
                try { HTMLFormElement.prototype.submit.call(receiver); }
                catch(error) { if(!(error instanceof TypeError)) throw error; }
            }
            touched===0;
        )JS") && empty_form_queue());
    }
    {
        Fixture f(server);
        CHECK("multipart rejects an aggregate above the native byte limit without publishing a request", evaluate(f.context, R"JS(
            f.enctype='multipart/form-data'; f.action='/oversize';
            const input=document.createElement('input'); input.type='file'; input.name='files'; input.multiple=true;
            const shared=new File([new Uint8Array(65536)],'shared.bin');
            const transfer=new DataTransfer();
            // One physical 64 KiB file, repeated as entries. The old serializer
            // builds one approximately 64 MiB aggregate before native rejection.
            for(let index=0;index<1025;index++) transfer.items.add(shared);
            input.files=transfer.files; f.append(input);
            let rejected=false;
            try { f.submit(); } catch(error) { rejected=error instanceof RangeError; }
            rejected;
        )JS") && empty_form_queue() && server.count("/oversize")==0);
    }
    {
        Fixture f(server, WEB_ENC_WINDOWS_1252); Receipt r;
        const bool sent = f.submit("/shadow-charset", R"JS(
            Object.defineProperty(document,'characterSet',{value:'UTF-8'}); add('_charset_',''); add('v','\u00e9');
        )JS", r);
        CHECK("form encoding reads native owner metadata despite a shadowed document property", sent &&
            exact_bytes(r.body, "_charset_=windows-1252&v=%E9"));
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/url-get?discard=1#keep", R"JS(
            f.method='get'; f.acceptCharset='windows-1252'; add('n\rm','v\nx'); addFile('file','\u00e9\n.txt');
        )JS", r, "/url-get?n%0D%0Am=v%0D%0Ax&file=%E9%0D%0A.txt");
        CHECK("GET normalizes fields and includes the encoded file name", sent && r.method == "GET" && r.body.empty());
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/url-percent", R"JS(
            f.acceptCharset='windows-1252'; add('a b*~_-','\u00e9\u661f&=');
        )JS", r);
        CHECK("URL form encoding escapes HTML fallback and applies the form percent set", sent &&
            r.headers["content-type"] == "application/x-www-form-urlencoded" &&
            exact_bytes(r.body, "a+b*%7E_-=%E9%26%2326143%3B%26%3D"));
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/plain-legacy", R"JS(
            f.enctype='text/plain'; f.acceptCharset='windows-1252'; add('n\rm','\u00e9\nx'); addFile('file','\u00e9\r.txt');
        )JS", r);
        CHECK("plain-text forms normalize names values and filenames before legacy encoding", sent &&
            r.headers["content-type"] == "text/plain" && exact_bytes(r.body, "n\r\nm=\xe9\r\nx\r\nfile=\xe9\r\n.txt\r\n"));
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/plain-iso", R"JS(
            f.enctype='text/plain'; f.acceptCharset='ISO-2022-JP'; add('n','\u661f\ud83c\udf1f\u661f');
        )JS", r);
        CHECK("plain-text ISO-2022-JP resets state correctly around decimal fallback", sent &&
            exact_bytes(r.body, "n=\x1b$B@1\x1b(B&#127775;\x1b$B@1\x1b(B\r\n"));
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/url-iso", R"JS(
            f.acceptCharset='ISO-2022-JP'; add('n','\u661f\ud83c\udf1f\u661f');
        )JS", r);
        CHECK("URL ISO-2022-JP percent-encodes state escapes and decimal fallback", sent &&
            exact_bytes(r.body, "n=%1B%24B%401%1B%28B%26%23127775%3B%1B%24B%401%1B%28B"));
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/duplicate-order", "add('key','first');add('key','second');add('tail','last')", r);
        CHECK("ASCII duplicate form entries preserve their original order", sent &&
            exact_bytes(r.body, "key=first&key=second&tail=last"));
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/charset-control-type", R"JS(
            f.acceptCharset='windows-1252'; add('_ChArSeT_','ignored'); add('_charset_','author','text');
        )JS", r);
        CHECK("charset replacement is case insensitive and limited to hidden input controls", sent &&
            exact_bytes(r.body, "_ChArSeT_=windows-1252&_charset_=author"));
    }
    {
        Fixture f(server, WEB_ENC_WINDOWS_1252);
        CHECK("FormData form construction uses UTF-8 without mutating the hidden control", evaluate(f.context, R"JS(
            f.acceptCharset='windows-1252'; const input=add('_charset_','author');
            new FormData(f).get('_charset_')==='UTF-8' && input.value==='author';
        )JS"));
    }
    {
        Fixture f(server);
        CHECK("FormData append stores original linebreaks until serialization", evaluate(f.context, R"JS(
            const data=new FormData(); data.append('n\rm','v\nx');
            [...data][0][0]==='n\rm' && data.get('n\rm')==='v\nx';
        )JS"));
    }
    {
        Fixture f(server, WEB_ENC_WINDOWS_1252);
        CHECK("URLSearchParams remains UTF-8 in a legacy-encoded document", evaluate(f.context,
            "new URLSearchParams([['v','\\u00e9']]).toString()==='v=%C3%A9'"));
    }
    {
        Fixture f(server, WEB_ENC_WINDOWS_1252);
        const bool setup = evaluate(f.context, R"JS(
            const data=new FormData(); data.append('n\rm','\u00e9\nx');
            data.append('file',new File([new Uint8Array([65,0,66,13,10,255])],'f\n.txt',{type:'text/plain'}));
            globalThis.done=false;
            fetch('/fetch-multipart',{method:'POST',body:data}).then(r=>r.text()).then(()=>done=true);
            true;
        )JS");
        const bool arrived = setup && f.pump([&] { return server.count("/fetch-multipart") == 1 && evaluate(f.context, "done"); });
        CHECK("fetch FormData uses UTF-8 and shared field-only newline normalization", arrived &&
            multipart(server.last("/fetch-multipart"), {field("n%0D%0Am", "\xc3\xa9\r\nx"), file("file", "f%0A.txt", file_bytes())}));
    }
    {
        Fixture f(server, WEB_ENC_WINDOWS_1252);
        const bool setup = evaluate(f.context, R"JS(
            const data=new FormData(); data.append('n\rm','\u00e9\nx');
            data.append('file',new File([new Uint8Array([65,0,66,13,10,255])],'f\r.txt',{type:'text/plain'}));
            globalThis.done=false; const xhr=new XMLHttpRequest();
            xhr.open('POST','/xhr-multipart'); xhr.onload=()=>done=true; xhr.send(data); true;
        )JS");
        const bool arrived = setup && f.pump([&] { return server.count("/xhr-multipart") == 1 && evaluate(f.context, "done"); });
        CHECK("XHR FormData uses UTF-8 and independently escaped filename CR", arrived &&
            multipart(server.last("/xhr-multipart"), {field("n%0D%0Am", "\xc3\xa9\r\nx"), file("file", "f%0D.txt", file_bytes())}));
    }
    {
        Fixture f(server); Receipt r;
        const bool sent = f.submit("/empty-file", R"JS(
            const input=document.createElement('input'); input.type='file'; input.name='empty'; f.append(input);
        )JS", r);
        CHECK("URL-encoded submission includes an empty selected-file entry", sent && exact_bytes(r.body, "empty="));
    }
    CHECK("all completed form requests release their counted queue bytes", empty_form_queue());
    printf("RESULTS form-encoding %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
