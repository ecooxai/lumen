/* Actual worker-isolate/network regression. These are focused native checks,
 * not upstream WPT assertions. Link in place of worker.cc, like the existing
 * test_worker_lifecycle target. The HTTP server holds responses until the test
 * permits them, so timer/message progress cannot be explained by fast I/O.
 */
#include "../src/js/worker.cc"
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <functional>
#include <thread>

extern "C" void font_init(void);

static int checks, failures;
#define CHECK(name, condition) do { \
    bool ok_ = (condition); ++checks; \
    printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); fflush(stdout); \
    if (!ok_) ++failures; \
} while (0)

struct AsyncHTTPServer {
    struct Connection {
        std::string path;
        std::atomic<bool> released{false}, disconnected{false};
    };
    int listener = -1;
    unsigned port = 0;
    std::atomic<bool> stopping{false}, release_all{false};
    std::thread accept_thread;
    std::vector<std::thread> clients;
    std::mutex mu;
    std::vector<std::shared_ptr<Connection>> connections;

    static bool send_all(int fd, const std::string &value) {
        size_t sent = 0;
        while (sent < value.size()) {
            ssize_t n = send(fd, value.data() + sent, value.size() - sent, 0);
            if (n <= 0) return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    void serve(int client) {
        std::string request;
        const double deadline = now_ms() + 3000;
        while (!stopping && now_ms() < deadline && request.size() < 8192 &&
               request.find("\r\n\r\n") == std::string::npos) {
            pollfd p{client, POLLIN, 0};
            if (poll(&p, 1, 20) <= 0) continue;
            char bytes[1024];
            ssize_t n = recv(client, bytes, sizeof bytes, 0);
            if (n <= 0) break;
            request.append(bytes, static_cast<size_t>(n));
        }
        const size_t first = request.find(' '), last = request.find(' ', first + 1);
        if (first == std::string::npos || last == std::string::npos ||
            request.find("\r\n\r\n") == std::string::npos) { close(client); return; }
        auto connection = std::make_shared<Connection>();
        connection->path = request.substr(first + 1, last - first - 1);
        {
            std::lock_guard<std::mutex> lock(mu);
            connections.push_back(connection);
        }
        if (connection->path.rfind("/hold/", 0) == 0) {
            while (!stopping && !release_all && !connection->released) {
                pollfd p{client, POLLIN, 0};
                if (poll(&p, 1, 10) > 0) {
                    char byte;
                    ssize_t n = recv(client, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
                    if (n == 0 || (p.revents & (POLLHUP | POLLERR | POLLNVAL))) {
                        connection->disconnected = true;
                        close(client);
                        return;
                    }
                }
            }
        }
        if (!stopping) {
            std::string body = "payload:" + connection->path;
            std::string head = "HTTP/1.1 200 OK\r\nContent-Type: text/plain;charset=UTF-8\r\n"
                "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\n"
                "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
            (void)send_all(client, head + body);
        }
        close(client);
    }

    bool start() {
        listener = socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) return false;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) || listen(listener, 16)) return false;
        socklen_t length = sizeof address;
        if (getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length)) return false;
        port = ntohs(address.sin_port);
        accept_thread = std::thread([this] {
            while (!stopping) {
                pollfd p{listener, POLLIN, 0};
                if (poll(&p, 1, 20) <= 0) continue;
                if (stopping) break;
                int client = accept(listener, nullptr, nullptr);
                if (client >= 0) clients.emplace_back([this, client] { serve(client); });
            }
        });
        return true;
    }

    size_t count(const std::string &prefix, bool disconnected = false) {
        std::lock_guard<std::mutex> lock(mu);
        size_t total = 0;
        for (const auto &c : connections)
            if (c->path.rfind(prefix, 0) == 0 && (!disconnected || c->disconnected)) ++total;
        return total;
    }
    std::string origin() const { return "http://127.0.0.1:" + std::to_string(port); }
    void release() { release_all = true; }
    void stop() {
        stopping = true;
        if (listener >= 0) shutdown(listener, SHUT_RDWR);
        if (accept_thread.joinable()) accept_thread.join();
        for (auto &t : clients) if (t.joinable()) t.join();
        if (listener >= 0) close(listener);
        listener = -1;
    }
    ~AsyncHTTPServer() { stop(); }
};

static bool drain_network(double timeout_ms = 2000) {
    const double end = now_ms() + timeout_ms;
    do {
        net_poll();
        if (!net_pending()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (now_ms() < end);
    return false;
}

struct WorkerPage {
    Document *doc = nullptr;
    JsCtx *ctx = nullptr;
    std::shared_ptr<Worker> native;
    size_t registry_before = 0;

    explicit WorkerPage(const std::string &origin) {
        registry_before = g_workers.size();
        doc = doc_new((origin + "/owner.html").c_str());
        const char *html = "<!doctype html><html><body></body></html>";
        html_parse(doc, html, strlen(html));
        JsHost host{};
        ctx = js_new(doc, &host);
    }
    void start(const std::string &script) {
        {
            JsCtx *c = ctx;
            JS_ENTER(c);
            (void)ctx->Global()->Set(ctx, jstr(iso, "__source"), jstr(iso, script.c_str()));
        }
        const uint32_t previous = g_wseq;
        js_eval(ctx, R"JS(
globalThis.events = [];
globalThis.worker = new Worker(URL.createObjectURL(new Blob([__source], {type:'text/javascript'})));
worker.onmessage = e => events.push(e.data);
worker.onerror = e => events.push({kind:'worker-error', message:String(e.message)});
worker.onmessageerror = () => events.push({kind:'worker-error', message:'messageerror'});
)JS", "test:worker-async-owner");
        auto it = g_workers.find(g_wseq);
        native = g_wseq != previous && it != g_workers.end() ? it->second : nullptr;
        CHECK("real worker isolate is registered", native != nullptr);
    }
    void pump() { net_poll(); js_tick(ctx); }
    bool truth(const char *expression) {
        JsCtx *c = ctx;
        JS_ENTER(c);
        v8::TryCatch caught(iso);
        v8::Local<v8::Script> script;
        v8::Local<v8::Value> result;
        return v8::Script::Compile(ctx, jstr(iso, expression)).ToLocal(&script) &&
            script->Run(ctx).ToLocal(&result) && result->BooleanValue(iso);
    }
    bool spin(const std::function<bool()> &ready, double timeout_ms = 3000) {
        const double end = now_ms() + timeout_ms;
        do {
            pump();
            if (ready()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (now_ms() < end);
        return ready();
    }
    bool event(const char *kind, double timeout_ms = 3000) {
        const std::string expression = "events.some(e=>e&&e.kind==='" + std::string(kind) + "')";
        bool result = spin([&] { return truth(expression.c_str()) || truth("events.some(e=>e&&e.kind==='worker-error')"); }, timeout_ms);
        return result && truth(expression.c_str());
    }
    void send(const char *literal) {
        const std::string source = "worker.postMessage(" + std::string(literal) + ");";
        js_eval(ctx, source.c_str(), "test:worker-async-command");
    }
    void terminate() { js_eval(ctx, "worker.terminate();", "test:worker-async-terminate"); }
    void dump_events() { js_eval(ctx, "console.log('worker async events', JSON.stringify(events));", "test:worker-async-events"); }
    void dispose() {
        if (!ctx) return;
        terminate();
        std::weak_ptr<Worker> weak = native;
        native.reset();
        js_free(ctx); doc_free(doc); ctx = nullptr; doc = nullptr;
        CHECK("worker disposal completes its actual isolate cleanup", wait_worker_threads(2000));
        CHECK("worker disposal drains canceled network ownership", drain_network());
        CHECK("worker registry returns to its original size", g_workers.size() == registry_before);
        CHECK("network finalizers release the worker ownership", weak.expired());
    }
    ~WorkerPage() { dispose(); }
};

static std::string prefix(const AsyncHTTPServer &server) {
    return "const base='" + server.origin() + "';\n";
}

static void check_concurrency() {
    printf("CASE async-worker concurrent fetch responsiveness\n");
    AsyncHTTPServer server;
    CHECK("concurrency server starts", server.start());
    WorkerPage page(server.origin());
    page.start(prefix(server) + R"JS(
let completed=0;
for (const name of ['a','b']) fetch(base+'/hold/'+name).then(r=>r.text()).then(body=>{
  postMessage({kind:'fetch-result',body});
  if (++completed===2) postMessage({kind:'fetch-complete'});
}, e=>postMessage({kind:'worker-error',message:String(e)}));
setTimeout(()=>postMessage({kind:'timer'}),20);
onmessage=()=>postMessage({kind:'pong'});
postMessage({kind:'ready'});
)JS");
    CHECK("both network requests reach the server before either response", page.spin([&]{return server.count("/hold/")==2;}));
    CHECK("worker timers run while both responses remain held", page.event("timer", 1000));
    page.send("'ping'");
    CHECK("worker receives messages while both responses remain held", page.event("pong", 1000));
    CHECK("neither held response has spuriously completed", page.truth("!events.some(e=>e.kind==='fetch-result')"));
    server.release();
    CHECK("both independent fetches complete after real responses", page.event("fetch-complete"));
    CHECK("concurrent bodies stay associated with their requests", page.truth("events.filter(e=>e.kind==='fetch-result').map(e=>e.body).sort().join('|')==='payload:/hold/a|payload:/hold/b'"));
    page.dispose();
}

static void check_fetch_abort() {
    printf("CASE async-worker AbortSignal cancellation\n");
    AsyncHTTPServer server;
    CHECK("fetch abort server starts", server.start());
    WorkerPage page(server.origin());
    page.start(prefix(server) + R"JS(
const controller=new AbortController();
fetch(base+'/hold/abort',{signal:controller.signal}).then(
  ()=>postMessage({kind:'unexpected-load'}),
  e=>postMessage({kind:'rejected',name:e.name}));
onmessage=()=>{controller.abort();postMessage({kind:'abort-returned'});};
)JS");
    CHECK("abort test observes an actual pending request", page.spin([&]{return server.count("/hold/abort")==1;}));
    page.send("'abort'");
    CHECK("AbortSignal rejects pending fetch promptly", page.event("rejected", 1500));
    CHECK("fetch rejects with AbortError", page.truth("events.filter(e=>e.kind==='rejected').length===1&&events.find(e=>e.kind==='rejected').name==='AbortError'"));
    CHECK("AbortSignal cancels the held transport socket", page.spin([&]{return server.count("/hold/abort",true)==1;},1500));
    server.release();
    CHECK("aborted native request is finalized", page.spin([]{return net_pending()==0;}));
    CHECK("fetch does not later fulfill after abort", page.truth("!events.some(e=>e.kind==='unexpected-load')"));
    page.dispose();
}

static void check_fetch_completion_burst() {
    printf("CASE async-worker concurrent fast completion ownership\n");
    AsyncHTTPServer server;
    CHECK("completion burst server starts",server.start());
    WorkerPage page(server.origin());
    page.start(prefix(server)+R"JS(
Promise.all(Array.from({length:64},(_,i)=>fetch(base+'/burst/'+i).then(r=>r.text())))
  .then(bodies=>postMessage({kind:'burst',bodies}),
        e=>postMessage({kind:'worker-error',message:String(e)}));
)JS");
    CHECK("all concurrent fast completions return to their owning worker",page.event("burst",5000));
    CHECK("completion burst preserves all 64 request and body identities",page.truth("events.filter(e=>e.kind==='burst').length===1&&events.find(e=>e.kind==='burst').bodies.every((b,i)=>b==='payload:/burst/'+i)&&events.find(e=>e.kind==='burst').bodies.length===64"));
    CHECK("all 64 burst requests actually reached the HTTP server",server.count("/burst/")==64);
    CHECK("burst completions release their native transport ownership",page.spin([]{return net_pending()==0;}));
    page.dispose();
}

static void check_falsy_abort_reasons() {
    printf("CASE async-worker fetch preserves falsy AbortSignal reasons\n");
    AsyncHTTPServer server;
    CHECK("falsy abort reason server starts",server.start());
    WorkerPage page(server.origin());
    page.start(prefix(server)+R"JS(
const reasons=[null,0,false,''];const controllers=[];let completed=0;
const result=(which,ok)=>{
  postMessage({kind:'reason-result',which,ok});
  if(++completed===8)postMessage({kind:'reasons-complete'});
};
for(let i=0;i<reasons.length;i++){
  const pending=new AbortController();controllers.push(pending);
  fetch(base+'/hold/reason-'+i,{signal:pending.signal}).then(
    ()=>result('pending-'+i,false),e=>result('pending-'+i,e===reasons[i]));
  const prior=new AbortController();prior.abort(reasons[i]);
  fetch(base+'/must-not-load-'+i,{signal:prior.signal}).then(
    ()=>result('prior-'+i,false),e=>result('prior-'+i,e===reasons[i]));
}
onmessage=()=>controllers.forEach((c,i)=>c.abort(reasons[i]));
)JS");
    CHECK("all four pending reason requests reach the held server",page.spin([&]{return server.count("/hold/reason-")==4;}));
    page.send("'abort'");
    CHECK("all pending and pre-aborted fetches settle with their reasons",page.event("reasons-complete",1500));
    CHECK("null zero false and empty-string reasons remain exact rejection values",page.truth("events.filter(e=>e.kind==='reason-result').length===8&&events.filter(e=>e.kind==='reason-result').every(e=>e.ok===true)"));
    CHECK("pre-aborted signals never start HTTP requests",server.count("/must-not-load-")==0);
    CHECK("all pending falsy-reason requests cancel their sockets",page.spin([&]{return server.count("/hold/reason-",true)==4;},1500));
    page.dispose();
}

static void check_owner_loop_independence() {
    printf("CASE async-worker completion without pumping the parent event loop\n");
    AsyncHTTPServer server;
    CHECK("independent completion server starts",server.start());
    WorkerPage page(server.origin());
    page.start(prefix(server)+R"JS(
fetch(base+'/hold/independent').then(r=>r.text()).then(
  body=>postMessage({kind:'independent',body}),
  e=>postMessage({kind:'worker-error',message:String(e)}));
)JS");
    // Deliberately do not call page.pump(), js_tick(), or net_poll() here.
    // The held server response makes it impossible for main-thread startup to
    // have already dispatched this completion before the pause begins.
    double end=now_ms()+1500;
    while(server.count("/hold/independent")!=1&&now_ms()<end)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK("worker dispatches HTTP while parent event loop is paused",server.count("/hold/independent")==1);
    server.release();
    bool queued=false;
    end=now_ms()+1500;
    while(now_ms()<end){
        if(page.native){
            std::lock_guard<std::mutex> lock(page.native->mu);
            queued=page.native->out.size()==1&&page.native->out.front().kind==WM_MESSAGE;
        }
        if(queued)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK("worker completes its response before parent resumes net_poll",queued);
    CHECK("paused-parent completion contains the real HTTP response",page.event("independent",1000)&&page.truth("events.some(e=>e.kind==='independent'&&e.body==='payload:/hold/independent')"));
    page.dispose();
}

static void check_xhr_reopen() {
    printf("CASE async-worker XMLHttpRequest abort and reopen\n");
    AsyncHTTPServer server;
    CHECK("XHR reopen server starts", server.start());
    WorkerPage page(server.origin());
    page.start(prefix(server) + R"JS(
const xhr=new XMLHttpRequest();
xhr.onload=()=>postMessage({kind:'load',body:xhr.responseText,state:xhr.readyState,status:xhr.status});
xhr.onabort=()=>postMessage({kind:'abort',state:xhr.readyState,status:xhr.status});
xhr.onerror=()=>postMessage({kind:'worker-error',message:'xhr error'});
xhr.open('GET',base+'/hold/old',true);xhr.send();
onmessage=()=>{
  xhr.abort();const reset=xhr.readyState;
  xhr.open('GET',base+'/new',true);xhr.send();
  postMessage({kind:'reopened',reset});
};
)JS");
    CHECK("old XHR reaches the held server response", page.spin([&]{return server.count("/hold/old")==1;}));
    page.send("'reopen'");
    CHECK("worker handles abort/reopen during pending XHR", page.event("reopened",1500));
    CHECK("abort resets readyState outside its event callbacks", page.truth("events.some(e=>e.kind==='reopened'&&e.reset===0)"));
    CHECK("old XHR emits one abort with network-error status", page.truth("events.filter(e=>e.kind==='abort').length===1&&events.find(e=>e.kind==='abort').status===0"));
    CHECK("reopened XHR receives the new actual response", page.event("load",1500));
    CHECK("new XHR state and payload belong to the new generation", page.truth("events.filter(e=>e.kind==='load').length===1&&events.find(e=>e.kind==='load').body==='payload:/new'&&events.find(e=>e.kind==='load').state===4&&events.find(e=>e.kind==='load').status===200"));
    CHECK("reopen cancels the old transport socket", page.spin([&]{return server.count("/hold/old",true)==1;},1500));
    server.release();
    CHECK("old and new transport ownership both finalize", page.spin([]{return net_pending()==0;}));
    CHECK("stale old response cannot produce a second load", page.truth("events.filter(e=>e.kind==='load').length===1"));
    page.dispose();
}

static void check_xhr_timeout_and_sync() {
    printf("CASE async-worker XMLHttpRequest timeout and synchronous behavior\n");
    AsyncHTTPServer server;
    CHECK("XHR timeout server starts", server.start());
    WorkerPage page(server.origin());
    page.start(prefix(server) + R"JS(
const xhr=new XMLHttpRequest();const order=[];
xhr.timeout=400;
xhr.onreadystatechange=()=>{if(xhr.readyState===4)order.push('done');};
xhr.ontimeout=()=>{order.push('timeout');postMessage({kind:'timeout',status:xhr.status,state:xhr.readyState,body:xhr.responseText});};
xhr.onload=()=>postMessage({kind:'unexpected-load'});
xhr.onloadend=()=>{order.push('loadend');postMessage({kind:'end',order});};
xhr.open('GET',base+'/hold/timeout',true);xhr.send();
onmessage=()=>{
  const sync=new XMLHttpRequest();sync.open('GET',base+'/sync',false);sync.send();
  postMessage({kind:'sync',state:sync.readyState,status:sync.status,body:sync.responseText});
};
)JS");
    CHECK("timeout request reaches the server before timeout", page.spin([&]{return server.count("/hold/timeout")==1;},1500));
    CHECK("XHR timeout occurs without receiving an HTTP response", page.event("end",1500));
    CHECK("timeout reports DONE with cleared response and status", page.truth("events.some(e=>e.kind==='timeout'&&e.state===4&&e.status===0&&e.body==='')"));
    CHECK("timeout dispatches readyState, timeout, loadend once in order", page.truth("events.filter(e=>e.kind==='end').length===1&&events.find(e=>e.kind==='end').order.join(',')==='done,timeout,loadend'&&!events.some(e=>e.kind==='unexpected-load')"));
    CHECK("XHR timeout cancels the held transport socket", page.spin([&]{return server.count("/hold/timeout",true)==1;},1500));
    page.send("'sync'");
    CHECK("synchronous worker XHR returns a completed response", page.event("sync",1500));
    CHECK("synchronous XHR preserves immediate status and decoded text", page.truth("events.some(e=>e.kind==='sync'&&e.state===4&&e.status===200&&e.body==='payload:/sync')"));
    page.dispose();
}

static void check_xhr_reentrant_open() {
    printf("CASE async-worker XMLHttpRequest reentrant open during response events\n");
    AsyncHTTPServer server;
    CHECK("XHR reentrant server starts", server.start());
    WorkerPage page(server.origin());
    page.start(prefix(server) + R"JS(
const xhr=new XMLHttpRequest();let generation=0;const states=[];
xhr.onreadystatechange=()=>{
  states.push([generation,xhr.readyState]);
  if(generation===0&&xhr.readyState===2){
    generation=1;xhr.open('GET',base+'/second',true);xhr.send();
    postMessage({kind:'reentrant-open'});
  }
};
xhr.onload=()=>postMessage({kind:'load',body:xhr.responseText,status:xhr.status,states});
xhr.onerror=()=>postMessage({kind:'worker-error',message:'reentrant xhr error'});
xhr.open('GET',base+'/first',true);xhr.send();
)JS");
    CHECK("HEADERS_RECEIVED permits a new asynchronous open/send",page.event("reentrant-open",1500));
    CHECK("new generation completes after reentrant open",page.event("load",1500));
    CHECK("old response does not overwrite the new generation body",page.truth("events.filter(e=>e.kind==='load').length===1&&events.find(e=>e.kind==='load').body==='payload:/second'&&events.find(e=>e.kind==='load').status===200"));
    CHECK("both reentrant HTTP requests actually reached the server",server.count("/first")==1&&server.count("/second")==1);
    CHECK("reentrant requests leave no native pending ownership",page.spin([]{return net_pending()==0;}));
    page.dispose();
}

static void check_exit_cancellation(bool self_close) {
    printf("CASE async-worker %s cancels all outstanding transports\n", self_close ? "self-close" : "terminate");
    AsyncHTTPServer server;
    CHECK("worker exit server starts", server.start());
    WorkerPage page(server.origin());
    page.start(prefix(server) + R"JS(
for(const name of ['one','two']) fetch(base+'/hold/'+name).catch(()=>{});
const xhr=new XMLHttpRequest();xhr.open('GET',base+'/hold/three');xhr.send();
onmessage=()=>{close();postMessage({kind:'closing'});};
)JS");
    CHECK("all three requests are outstanding before exit", page.spin([&]{return server.count("/hold/")==3;}));
    const double start=now_ms();
    if(self_close) page.send("'close'"); else page.terminate();
    CHECK("exit request returns promptly", now_ms()-start<1000);
    CHECK("worker completes isolate teardown before HTTP timeout", page.spin([&]{return page.native&&page.native->finished.load();},1500));
    CHECK("every held socket is canceled on worker exit", page.spin([&]{return server.count("/hold/",true)==3;},1500));
    CHECK("network completion finalizers drain after worker exit", page.spin([]{return net_pending()==0;},1500));
    if(self_close) CHECK("self-close preserves its current-task message", page.event("closing",1000));
    page.dispose();
}

int main(int argc, char **argv) {
    dom_init();net_init(4);font_init();js_global_init(argv[0]);
    check_concurrency();
    check_fetch_abort();
    check_fetch_completion_burst();
    check_falsy_abort_reasons();
    check_owner_loop_independence();
    check_xhr_reopen();
    check_xhr_timeout_and_sync();
    check_xhr_reentrant_open();
    check_exit_cancellation(false);
    check_exit_cancellation(true);
    CHECK("all worker threads complete before process shutdown",js_workers_shutdown(3000));
    CHECK("all native network requests finalize before process shutdown",drain_network());
    net_shutdown();
    printf("RESULTS worker-async-lifecycle %s (%d/%d checks)\n",failures?"FAIL":"PASS",checks-failures,checks);
    return failures?1:0;
}
