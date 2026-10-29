/* Real native isolate/port lifetime oracle, separate from the frozen basic
 * SharedWorker100-cycle fixture. The receiving DedicatedWorker deliberately
 * stays alive after each transferred last-client port is closed. */
#define main worker_async_reference_main
#include "test_worker_async_lifecycle.cc"
#undef main
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#ifdef __APPLE__
#include <libproc.h>
#endif

struct TransferResources { long rss_kib=-1, threads=-1, fds=-1; };
static TransferResources transfer_resources() {
    TransferResources value;
#ifdef __APPLE__
    proc_taskinfo task{};
    if (proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &task, sizeof task)==sizeof task) {
        value.rss_kib=static_cast<long>(task.pti_resident_size/1024); value.threads=task.pti_threadnum;
    }
    const int capacity=proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);
    if (capacity>=0) {
        std::vector<proc_fdinfo> files(static_cast<size_t>(capacity)/sizeof(proc_fdinfo)+8);
        const int size=proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, files.data(), static_cast<int>(files.size()*sizeof(proc_fdinfo)));
        if (size>=0) value.fds=size/sizeof(proc_fdinfo);
    }
#else
    std::ifstream input("/proc/self/status"); std::string line;
    while (std::getline(input,line)) {
        if (line.rfind("VmRSS:",0)==0) value.rss_kib=strtol(line.c_str()+6,nullptr,10);
        if (line.rfind("Threads:",0)==0) value.threads=strtol(line.c_str()+8,nullptr,10);
    }
    DIR *directory=opendir("/proc/self/fd");
    if (directory) { value.fds=-1; while(dirent *entry=readdir(directory)) if(entry->d_name[0]!='.') ++value.fds; closedir(directory); }
#endif
    return value;
}

static int transfer_journal=-1;
static void transfer_checkpoint(unsigned cycle, const TransferResources &value) {
    char line[512];
    const int length=snprintf(line,sizeof line,
        "{\"cycle\":%u,\"checks\":%d,\"failures\":%d,\"rss_kib\":%ld,\"threads\":%ld,\"fds\":%ld,\"workers\":%zu,\"clients\":%zu,\"pending\":%d}\n",
        cycle,checks,failures,value.rss_kib,value.threads,value.fds,g_workers.size(),g_shared_clients.size(),net_pending());
    if(length<=0||static_cast<size_t>(length)>=sizeof line) { ++failures; return; }
    printf("SHARED_RESOURCE_CHECKPOINT %s",line); fflush(stdout);
    if(write(transfer_journal,line,static_cast<size_t>(length))!=length) ++failures;
}

int main(int argc, char **argv) {
    const unsigned cycles=argc>1?static_cast<unsigned>(strtoul(argv[1],nullptr,10)):100;
    if(cycles<5||cycles>100||argc<3) { fprintf(stderr,"usage: shared-transfer-stress 5..100 new-journal.jsonl\n"); return 2; }
    transfer_journal=open(argv[2],O_WRONLY|O_CREAT|O_EXCL,0600);
    if(transfer_journal<0) { perror("shared transfer journal"); return 2; }
    dom_init(); net_init(4); font_init(); js_global_init(argv[0]);
    WorkerPage page("http://lumen-transfer-stress.invalid");
    const std::string receiver=R"JS(
let endpoint;
onmessage=event=>{
  const data=event.data;
  if(data.op==='adopt'){
    endpoint=event.ports[0];endpoint.onmessage=message=>postMessage({kind:'relay',value:message.data});
    postMessage({kind:'adopted'});
  }else if(data.op==='send')endpoint.postMessage(data.value);
  else if(data.op==='return'){
    endpoint.onmessage=null;postMessage({kind:'returned',port:endpoint},[endpoint]);endpoint=null;
  }else if(data.op==='close'){
    endpoint.close();endpoint=null;postMessage({kind:'closed'});
  }else if(data.op==='ping')postMessage({kind:'pong'});
};
postMessage({kind:'bridge-ready'});
)JS";
    page.start(receiver);
    CHECK("persistent receiving worker starts",page.event("bridge-ready"));
    js_eval(page.ctx,R"JS(
globalThis.receiverURL=URL.createObjectURL(new Blob([__source],{type:'text/javascript'}));
globalThis.sharedURL=URL.createObjectURL(new Blob([`
onconnect=event=>{
 const port=event.ports[0];
 port.onmessage=message=>port.postMessage({kind:'echo',value:message.data,connection:1});
 port.postMessage({kind:'connected',connection:1});
};`],{type:'text/javascript'}));
)JS","test:transfer-stress-setup");
    TransferResources warm;
    for(unsigned cycle=1;cycle<=cycles&&!failures;++cycle) {
        printf("SHARED_TRANSFER_STRESS_CYCLE %u of %u\n",cycle,cycles);fflush(stdout);
        const std::string create="globalThis.events=[];globalThis.sharedEvents=[];globalThis.senderEvents=[];globalThis.returned=null;"
            "globalThis.shared=new SharedWorker(sharedURL,'transfer-lifetime-"+std::to_string(cycle)+"');"
            "shared.port.onmessage=e=>sharedEvents.push(e.data);"
            "globalThis.sender=new Worker(receiverURL);sender.onmessage=e=>{senderEvents.push(e.data);if(e.data.kind==='returned')returned=e.ports[0];};";
        js_eval(page.ctx,create.c_str(),"test:transfer-stress-create");
        CHECK("shared and sending worker start in actual isolates",page.spin([&]{return page.truth(
            "sharedEvents.some(e=>e.kind==='connected')&&senderEvents.some(e=>e.kind==='bridge-ready')");}));
        CHECK("one shared route and two dedicated workers are registered",g_shared_clients.size()==1&&g_workers.size()==3);
        std::weak_ptr<Worker> shared_native, sender_native;
        for(const auto &entry:g_workers) {
            if(entry.second->shared) shared_native=entry.second;
            else if(entry.second!=page.native) sender_native=entry.second;
        }
        js_eval(page.ctx,"sender.postMessage({op:'adopt',port:shared.port},[shared.port]);shared.port.close();", "test:transfer-stress-adopt");
        CHECK("detached sender close preserves the transferred endpoint",page.spin([&]{return page.truth("senderEvents.some(e=>e.kind==='adopted')");}));
        js_eval(page.ctx,"sender.postMessage({op:'send',value:41});","test:transfer-stress-echo-a");
        CHECK("first recipient echoes on the original shared connection",page.spin([&]{return page.truth(
            "senderEvents.some(e=>e.kind==='relay'&&e.value.kind==='echo'&&e.value.value===41&&e.value.connection===1)");}));
        js_eval(page.ctx,"sender.postMessage({op:'return'});","test:transfer-stress-return");
        CHECK("connection endpoint returns to the document",page.spin([&]{return page.truth("returned instanceof MessagePort");}));
        js_eval(page.ctx,"sender.terminate();returned.onmessage=e=>sharedEvents.push(e.data);returned.postMessage(42);","test:transfer-stress-terminate-former-owner");
        CHECK("returned endpoint survives former recipient termination",page.spin([&]{return page.truth(
            "sharedEvents.some(e=>e.kind==='echo'&&e.value===42&&e.connection===1)");}));
        CHECK("former recipient isolate is reclaimed before retransfer",page.spin([&]{return sender_native.expired()&&g_workers.size()==2;}));
        js_eval(page.ctx,"worker.postMessage({op:'adopt',port:returned},[returned]);returned.close();","test:transfer-stress-readopt");
        CHECK("persistent recipient accepts the returned endpoint",page.event("adopted"));
        js_eval(page.ctx,"worker.postMessage({op:'send',value:43});","test:transfer-stress-echo-b");
        CHECK("second recipient retains the shared connection",page.spin([&]{return page.truth(
            "events.some(e=>e.kind==='relay'&&e.value.kind==='echo'&&e.value.value===43&&e.value.connection===1)");}));
        js_eval(page.ctx,"worker.postMessage({op:'close'});","test:transfer-stress-close-last-client");
        CHECK("persistent recipient acknowledges last endpoint close",page.event("closed"));
        CHECK("last transferred endpoint releases native routes before receiver termination",page.spin([&]{
            return g_shared_clients.empty()&&g_workers.size()==1;
        }));
        CHECK("last transferred endpoint releases the native shared isolate",page.spin([&]{return shared_native.expired();}));
        CHECK("only the live receiver thread remains",page.spin([&]{
            WorkerThreads &threads=worker_threads();std::lock_guard<std::mutex> lock(threads.mu);return threads.running==1;
        }));
        js_eval(page.ctx,"worker.postMessage({op:'ping'});","test:transfer-stress-still-alive");
        CHECK("the persistent receiving worker is still responsive",page.event("pong"));
        CHECK("transfer cycle drains network finalizers",drain_network());
        js_eval(page.ctx,"shared=null;sender=null;returned=null;sharedEvents=[];senderEvents=[];events=[];","test:transfer-stress-release-wrappers");
        TransferResources current=transfer_resources();
        if(cycle>4&&(current.threads>warm.threads||current.fds>warm.fds)) {
            const double deadline=now_ms()+500;
            while(now_ms()<deadline&&(current.threads>warm.threads||current.fds>warm.fds)) {
                page.pump();std::this_thread::sleep_for(std::chrono::milliseconds(5));current=transfer_resources();
            }
        }
        CHECK("native transfer resource counters are readable",current.rss_kib>0&&current.threads>0&&current.fds>=0);
        if(cycle==4)warm=current;
        if(cycle>4) {
            CHECK("transferred endpoint worker threads do not accumulate",current.threads<=warm.threads);
            CHECK("transferred endpoint descriptors do not accumulate",current.fds<=warm.fds);
            CHECK("warm transfer RSS growth remains below64MiB",current.rss_kib-warm.rss_kib<=64*1024);
        }
        transfer_checkpoint(cycle,current);
    }
    page.dispose();
    CHECK("all transferred endpoint worker threads shut down",js_workers_shutdown(3000));
    CHECK("all transferred endpoint requests finalize",drain_network());
    printf("SHARED_TRANSFER_STRESS_IDLE_BEGIN\n");fflush(stdout);
    std::this_thread::sleep_for(std::chrono::seconds(12));
    transfer_checkpoint(cycles+1,transfer_resources());
    if(fsync(transfer_journal))++failures;
    close(transfer_journal);transfer_journal=-1;
    net_shutdown();
    printf("RESULTS shared-worker-transfer-stress %s (%d/%d checks)\n",failures?"FAIL":"PASS",checks-failures,checks);
    return failures?1:0;
}
