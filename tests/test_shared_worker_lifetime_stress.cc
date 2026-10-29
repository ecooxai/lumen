/* Repeated SharedWorker owner/port teardown in real native V8 isolates.
 * The dedicated-worker regression is imported only for existing setup helpers.
 * This is a native lifetime oracle, not WPT or a GUI performance measurement. */
#define main worker_async_reference_main
#include "test_worker_async_lifecycle.cc"
#undef main
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#ifdef __APPLE__
#include <libproc.h>
#endif

struct SharedScriptServer {
    int listener = -1;
    unsigned port = 0;
    std::atomic<bool> stopping{false};
    std::thread thread;
    std::atomic<unsigned> script_requests{0};
    bool start() {
        listener = socket(AF_INET, SOCK_STREAM, 0);
        if (listener < 0) return false;
        sockaddr_in address{};
        address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) || listen(listener, 8)) return false;
        socklen_t length = sizeof address;
        if (getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length)) return false;
        port = ntohs(address.sin_port);
        thread = std::thread([this] {
            while (!stopping) {
                pollfd pending{listener, POLLIN, 0};
                if (poll(&pending, 1, 20) <= 0 || stopping) continue;
                int client = accept(listener, nullptr, nullptr);
                if (client < 0) continue;
                std::string request;
                const double end = now_ms() + 2000;
                while (!stopping && now_ms() < end && request.size() < 8192 && request.find("\r\n\r\n") == std::string::npos) {
                    pollfd input{client, POLLIN, 0};
                    if (poll(&input, 1, 20) <= 0) continue;
                    char buffer[2048]; ssize_t count = recv(client, buffer, sizeof buffer, 0);
                    if (count <= 0) break;
                    request.append(buffer, static_cast<size_t>(count));
                }
                const std::string source = R"JS(
let connectionCount=0;
onconnect=event=>{
  const port=event.ports[0], connection=++connectionCount;
  port.onmessage=message=>port.postMessage({kind:'echo',connection,value:message.data});
  port.postMessage({kind:'connected',connection,shared:self instanceof SharedWorkerGlobalScope});
};
)JS";
                if (request.find("GET /shared.js ") == 0) {
                    ++script_requests;
                    const std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/javascript\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: "
                        + std::to_string(source.size()) + "\r\n\r\n" + source;
                    (void)AsyncHTTPServer::send_all(client, response);
                }
                close(client);
            }
        });
        return true;
    }
    std::string origin() const { return "http://127.0.0.1:" + std::to_string(port); }
    ~SharedScriptServer() {
        stopping = true;
        if (listener >= 0) shutdown(listener, SHUT_RDWR);
        if (thread.joinable()) thread.join();
        if (listener >= 0) close(listener);
    }
};

struct SharedOwner {
    Document *doc = nullptr;
    JsCtx *ctx = nullptr;
    SharedOwner(const std::string &origin) {
        doc = doc_new((origin + "/owner.html").c_str());
        const char *html = "<!doctype html><html><body></body></html>";
        html_parse(doc, html, strlen(html));
        JsHost host{}; ctx = js_new(doc, &host);
    }
    void connect(unsigned cycle, const char *suffix) {
        const std::string script = "globalThis.events=[];globalThis.shared=new SharedWorker('/shared.js','lifetime-"
            + std::to_string(cycle) + suffix + "');shared.port.onmessage=e=>events.push(e.data);"
            "shared.onerror=e=>events.push({kind:'error'});";
        js_eval(ctx, script.c_str(), "test:shared-worker-lifetime");
    }
    void eval(const char *script) { if (ctx) js_eval(ctx, script, "test:shared-worker-lifetime-command"); }
    void pump() { if (ctx) { net_poll(); js_tick(ctx); } }
    bool truth(const char *expression) {
        if (!ctx) return false;
        JsCtx *c = ctx; JS_ENTER(c); v8::TryCatch caught(iso);
        v8::Local<v8::Script> script; v8::Local<v8::Value> value;
        return v8::Script::Compile(ctx, jstr(iso, expression)).ToLocal(&script)
            && script->Run(ctx).ToLocal(&value) && value->BooleanValue(iso);
    }
    void dispose() {
        if (!ctx) return;
        js_free(ctx); doc_free(doc); ctx = nullptr; doc = nullptr;
    }
    ~SharedOwner() { dispose(); }
};

static bool spin_shared(SharedOwner &first, SharedOwner &second, const std::function<bool()> &ready) {
    const double deadline = now_ms() + 3000;
    do {
        first.pump(); second.pump();
        if (ready()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (now_ms() < deadline);
    return ready();
}

struct SharedResources { long rss_kib=-1, threads=-1, fds=-1; };
static SharedResources shared_resources() {
    SharedResources value;
#ifdef __APPLE__
    proc_taskinfo task{};
    if (proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &task, sizeof task) == sizeof task) {
        value.rss_kib = static_cast<long>(task.pti_resident_size / 1024); value.threads = task.pti_threadnum;
    }
    const int capacity = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);
    if (capacity >= 0) {
        std::vector<proc_fdinfo> files(static_cast<size_t>(capacity) / sizeof(proc_fdinfo) + 8);
        int size = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, files.data(), static_cast<int>(files.size() * sizeof(proc_fdinfo)));
        if (size >= 0) value.fds = size / sizeof(proc_fdinfo);
    }
#else
    std::ifstream input("/proc/self/status"); std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("VmRSS:", 0) == 0) value.rss_kib = strtol(line.c_str()+6, nullptr, 10);
        if (line.rfind("Threads:", 0) == 0) value.threads = strtol(line.c_str()+8, nullptr, 10);
    }
    DIR *directory = opendir("/proc/self/fd");
    if (directory) {
        value.fds = -1;
        while (dirent *entry = readdir(directory)) if (entry->d_name[0] != '.') ++value.fds;
        closedir(directory);
    }
#endif
    return value;
}

static int journal_fd=-1;
static void checkpoint(unsigned cycle, const SharedResources &value) {
    char line[512];
    const int length = snprintf(line, sizeof line,
        "{\"cycle\":%u,\"checks\":%d,\"failures\":%d,\"rss_kib\":%ld,\"threads\":%ld,\"fds\":%ld,\"workers\":%zu,\"clients\":%zu,\"pending\":%d}\n",
        cycle, checks, failures, value.rss_kib, value.threads, value.fds, g_workers.size(), g_shared_clients.size(), net_pending());
    if (length <= 0 || static_cast<size_t>(length) >= sizeof line) { ++failures; return; }
    printf("SHARED_RESOURCE_CHECKPOINT %s", line); fflush(stdout);
    if (journal_fd >= 0 && write(journal_fd, line, static_cast<size_t>(length)) != length) ++failures;
}

int main(int argc, char **argv) {
    const unsigned cycles = argc > 1 ? static_cast<unsigned>(strtoul(argv[1], nullptr, 10)) : 100;
    if (cycles < 5 || cycles > 100 || argc < 3) { fprintf(stderr, "usage: test_shared_worker_lifetime_stress 5..100 new-journal.jsonl\n"); return 2; }
    journal_fd = open(argv[2], O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (journal_fd < 0) { perror("shared stress journal"); return 2; }
    dom_init(); net_init(4); font_init(); js_global_init(argv[0]);
    SharedScriptServer server;
    CHECK("shared lifetime fixture starts", server.start());
    SharedResources warm;
    for (unsigned cycle=1; cycle<=cycles && !failures; ++cycle) {
        printf("SHARED_STRESS_CYCLE %u of %u\n", cycle, cycles); fflush(stdout);
        const unsigned prior_requests=server.script_requests;
        SharedOwner first(server.origin()), second(server.origin());
        first.connect(cycle, "-joined"); second.connect(cycle, "-joined");
        CHECK("both owners receive distinct connect events from one shared scope", spin_shared(first, second, [&] {
            return first.truth("events.some(e=>e.kind==='connected'&&e.connection===1&&e.shared)")
                && second.truth("events.some(e=>e.kind==='connected'&&e.connection===2&&e.shared)");
        }));
        CHECK("same identity has one native worker and two owner routes", g_workers.size()==1 && g_shared_clients.size()==2);
        CHECK("shared script loads once for both owners", server.script_requests==prior_requests+1);
        std::weak_ptr<Worker> joined;
        if (!g_workers.empty()) joined=g_workers.begin()->second;
        first.dispose();
        CHECK("creator teardown preserves one live joined route", g_workers.size()==1 && g_shared_clients.size()==1 && !joined.expired());
        second.eval("shared.port.postMessage(42)");
        CHECK("remaining owner receives messages after creator teardown", spin_shared(first, second, [&] {
            return second.truth("events.some(e=>e.kind==='echo'&&e.connection===2&&e.value===42)");
        }));
        second.eval("shared.port.close()");
        CHECK("last port close removes worker and connection registries", g_workers.empty() && g_shared_clients.empty());
        CHECK("last port close completes actual native thread cleanup", wait_worker_threads(2000));
        CHECK("last port close releases all native worker ownership", joined.expired());
        second.dispose();
        SharedOwner sole(server.origin());
        sole.connect(cycle, "-destroyed");
        CHECK("sole owner creates a fresh live worker", spin_shared(sole, sole, [&] {
            return sole.truth("events.some(e=>e.kind==='connected'&&e.connection===1&&e.shared)");
        }));
        std::weak_ptr<Worker> destroyed;
        if (!g_workers.empty()) destroyed=g_workers.begin()->second;
        sole.dispose();
        CHECK("owner disposal closes its unclosed port routes", g_workers.empty() && g_shared_clients.empty());
        CHECK("owner disposal completes actual native thread cleanup", wait_worker_threads(2000));
        CHECK("owner disposal releases all native worker ownership", destroyed.expired());
        CHECK("cycle drains network finalizers", drain_network());
        SharedResources current=shared_resources();
        if (cycle>4 && (current.threads>warm.threads || current.fds>warm.fds)) {
            const SharedResources initial=current;
            const double started=now_ms(), until=started+500;
            while (now_ms()<until && (current.threads>warm.threads || current.fds>warm.fds)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5)); current=shared_resources();
            }
            printf("SHARED_RESOURCE_SETTLE {\"cycle\":%u,\"initial_threads\":%ld,\"final_threads\":%ld,\"initial_fds\":%ld,\"final_fds\":%ld,\"elapsed_ms\":%.3f,\"limit_ms\":500}\n",
                   cycle, initial.threads,current.threads,initial.fds,current.fds,now_ms()-started);
        }
        CHECK("native resource counters are readable", current.rss_kib>0 && current.threads>0 && current.fds>=0);
        if (cycle==4) warm=current;
        if (cycle>4) {
            CHECK("shared worker threads do not accumulate", current.threads<=warm.threads);
            CHECK("shared worker descriptors do not accumulate", current.fds<=warm.fds);
            CHECK("settled resident growth remains below the declared 64MiB limit", current.rss_kib-warm.rss_kib<=64*1024);
        }
        checkpoint(cycle,current);
    }
    CHECK("all shared workers complete before process shutdown", js_workers_shutdown(3000));
    CHECK("all shared requests finalize before process shutdown", drain_network());
    printf("SHARED_STRESS_IDLE_BEGIN\n"); fflush(stdout);
    std::this_thread::sleep_for(std::chrono::seconds(12));
    checkpoint(cycles+1,shared_resources());
    if (fsync(journal_fd)) ++failures;
    close(journal_fd); journal_fd=-1;
    net_shutdown();
    printf("RESULTS shared-worker-lifetime-stress %s (%d/%d checks)\n", failures?"FAIL":"PASS", checks-failures,checks);
    return failures?1:0;
}
