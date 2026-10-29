/* Native lifecycle coverage for the measured queued-message return defect.
 * The 120ms script barriers intentionally hold a recipient while the independent
 * SharedWorker produces25 messages. This is not a scrolling CPU workload. */
#define main worker_async_reference_main
#include "test_worker_async_lifecycle.cc"
#undef main
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#ifdef __APPLE__
#include <libproc.h>
#endif

struct QueueResources { long rss_kib=-1, threads=-1, fds=-1; };
static QueueResources queue_resources() {
    QueueResources value;
#ifdef __APPLE__
    proc_taskinfo task{};
    if(proc_pidinfo(getpid(),PROC_PIDTASKINFO,0,&task,sizeof task)==sizeof task) {
        value.rss_kib=static_cast<long>(task.pti_resident_size/1024);value.threads=task.pti_threadnum;
    }
    const int capacity=proc_pidinfo(getpid(),PROC_PIDLISTFDS,0,nullptr,0);
    if(capacity>=0) {
        std::vector<proc_fdinfo> files(static_cast<size_t>(capacity)/sizeof(proc_fdinfo)+8);
        const int size=proc_pidinfo(getpid(),PROC_PIDLISTFDS,0,files.data(),static_cast<int>(files.size()*sizeof(proc_fdinfo)));
        if(size>=0)value.fds=size/sizeof(proc_fdinfo);
    }
#else
    std::ifstream input("/proc/self/status");std::string line;
    while(std::getline(input,line)) {
        if(line.rfind("VmRSS:",0)==0)value.rss_kib=strtol(line.c_str()+6,nullptr,10);
        if(line.rfind("Threads:",0)==0)value.threads=strtol(line.c_str()+8,nullptr,10);
    }
    DIR *directory=opendir("/proc/self/fd");
    if(directory) { value.fds=-1;while(dirent *entry=readdir(directory))if(entry->d_name[0]!='.')++value.fds;closedir(directory); }
#endif
    return value;
}

static int queue_journal=-1;
struct NativeQueueCounts { size_t routes=0,moving=0,input=0,output=0; };
static NativeQueueCounts native_queue_counts() {
    NativeQueueCounts counts;
    for(const auto &entry:g_workers) {
        std::lock_guard<std::mutex> lock(entry.second->mu);
        counts.routes+=entry.second->moving_ports.size();
        for(const auto &route:entry.second->moving_ports)counts.moving+=route.second.size();
        counts.input+=entry.second->in.size();counts.output+=entry.second->out.size();
    }
    return counts;
}
static void queue_checkpoint(unsigned cycle,const QueueResources &value) {
    const NativeQueueCounts queues=native_queue_counts();
    char line[512];
    const int length=snprintf(line,sizeof line,
        "{\"cycle\":%u,\"checks\":%d,\"failures\":%d,\"rss_kib\":%ld,\"threads\":%ld,\"fds\":%ld,\"workers\":%zu,\"clients\":%zu,\"pending\":%d,\"movingRoutes\":%zu,\"movingMessages\":%zu,\"queuedInput\":%zu,\"queuedOutput\":%zu}\n",
        cycle,checks,failures,value.rss_kib,value.threads,value.fds,g_workers.size(),g_shared_clients.size(),net_pending(),queues.routes,queues.moving,queues.input,queues.output);
    if(length<=0||static_cast<size_t>(length)>=sizeof line) { ++failures;return; }
    printf("SHARED_RESOURCE_CHECKPOINT %s",line);fflush(stdout);
    if(write(queue_journal,line,static_cast<size_t>(length))!=length)++failures;
}

int main(int argc,char **argv) {
    const unsigned cycles=argc>1?static_cast<unsigned>(strtoul(argv[1],nullptr,10)):100;
    if(cycles<5||cycles>100||argc<3) { fprintf(stderr,"usage: shared-queue-stress 5..100 new-journal.jsonl\n");return 2; }
    queue_journal=open(argv[2],O_WRONLY|O_CREAT|O_EXCL,0600);
    if(queue_journal<0) { perror("shared queue journal");return 2; }
    dom_init();net_init(4);font_init();js_global_init(argv[0]);
    WorkerPage page("http://lumen-queue-stress.invalid");
    const std::string recipient=R"JS(
let endpoint;
onmessage=event=>{
  const data=event.data;
  if(data.op==='adopt'){
    endpoint=event.ports[0];endpoint.onmessage=message=>postMessage({kind:'relay',value:message.data});
    postMessage({kind:'adopted'});
  }else if(data.op==='send')endpoint.postMessage(data.value);
  else if(data.op==='returnBusy'||data.op==='closeBusy'){
    postMessage({kind:'busy',op:data.op});const end=performance.now()+120;while(performance.now()<end){}
    if(data.op==='returnBusy'){
      endpoint.onmessage=null;postMessage({kind:'returned',port:endpoint},[endpoint]);
    }else{endpoint.close();postMessage({kind:'closed'});}
    endpoint=null;
  }else if(data.op==='ping')postMessage({kind:'pong'});
};postMessage({kind:'bridge-ready'});
)JS";
    page.start(recipient);
    CHECK("queue stress persistent receiver starts",page.event("bridge-ready"));
    js_eval(page.ctx,R"JS(
globalThis.recipientURL=URL.createObjectURL(new Blob([__source],{type:'text/javascript'}));
globalThis.sharedURL=URL.createObjectURL(new Blob([`
let connectionCount=0;const ports=new Map();
onconnect=event=>{
 const port=event.ports[0],connection=++connectionCount;ports.set(connection,port);
 port.onmessage=message=>{
   const data=message.data;
   if(data&&data.op==='burst'){
     const target=ports.get(data.target);
     for(let number=0;number<data.count;number++)target.postMessage({kind:'burst',phase:data.phase,number});
     port.postMessage({kind:'producer-complete',phase:data.phase,count:data.count});
   }else port.postMessage({kind:'echo',value:data,connection});
 };
 port.postMessage({kind:'connected',connection});
};`],{type:'text/javascript'}));
)JS","test:queue-stress-setup");
    QueueResources warm;
    for(unsigned cycle=1;cycle<=cycles&&!failures;++cycle) {
        printf("SHARED_QUEUE_STRESS_CYCLE %u of %u\n",cycle,cycles);fflush(stdout);
        const std::string setup="globalThis.events=[];globalThis.sharedEvents=[];globalThis.controlEvents=[];globalThis.senderEvents=[];globalThis.returned=null;"
            "globalThis.shared=new SharedWorker(sharedURL,'queue-lifetime-"+std::to_string(cycle)+"');"
            "globalThis.control=new SharedWorker(sharedURL,'queue-lifetime-"+std::to_string(cycle)+"');"
            "shared.port.onmessage=e=>sharedEvents.push(e.data);control.port.onmessage=e=>controlEvents.push(e.data);"
            "globalThis.sender=new Worker(recipientURL);sender.onmessage=e=>{senderEvents.push(e.data);if(e.data.kind==='returned')returned=e.ports[0];};";
        js_eval(page.ctx,setup.c_str(),"test:queue-stress-create");
        CHECK("two shared connections and recipient are ready",page.spin([&]{return page.truth(
            "sharedEvents.some(e=>e.kind==='connected'&&e.connection===1)&&controlEvents.some(e=>e.kind==='connected'&&e.connection===2)&&senderEvents.some(e=>e.kind==='bridge-ready')");}));
        CHECK("two routes share one native isolate",g_shared_clients.size()==2&&g_workers.size()==3);
        std::weak_ptr<Worker> shared_native,sender_native;
        for(const auto &entry:g_workers) {
            if(entry.second->shared)shared_native=entry.second;
            else if(entry.second!=page.native)sender_native=entry.second;
        }
        js_eval(page.ctx,"sender.postMessage({op:'adopt',port:shared.port},[shared.port]);shared.port.close();","test:queue-stress-adopt");
        CHECK("intermediate recipient adopts the endpoint",page.spin([&]{return page.truth("senderEvents.some(e=>e.kind==='adopted')");}));
        js_eval(page.ctx,"sender.postMessage({op:'send',value:41});","test:queue-stress-echo");
        CHECK("original shared connection echoes before queue migration",page.spin([&]{return page.truth("senderEvents.some(e=>e.kind==='relay'&&e.value.kind==='echo'&&e.value.value===41&&e.value.connection===1)");}));
        js_eval(page.ctx,"sender.postMessage({op:'returnBusy'});","test:queue-stress-return-barrier");
        CHECK("intermediate recipient enters declared120ms return barrier",page.spin([&]{return page.truth("senderEvents.some(e=>e.kind==='busy'&&e.op==='returnBusy')");}));
        js_eval(page.ctx,"control.port.postMessage({op:'burst',target:1,phase:'return',count:25});","test:queue-stress-produce-return");
        CHECK("producer confirms25 messages before return",page.spin([&]{return page.truth("controlEvents.some(e=>e.kind==='producer-complete'&&e.phase==='return'&&e.count===25)");}));
        CHECK("return barrier is established before endpoint migration",page.truth("!senderEvents.some(e=>e.kind==='returned')"));
        CHECK("queued endpoint returns to document",page.spin([&]{return page.truth("returned instanceof MessagePort");}));
        js_eval(page.ctx,"returned.onmessage=e=>sharedEvents.push(e.data);sender.terminate();","test:queue-stress-kill-former-owner");
        CHECK("all25 queued messages follow the returned endpoint",page.spin([&]{return page.truth("sharedEvents.filter(e=>e.kind==='burst'&&e.phase==='return').length>=25");}));
        CHECK("returned queued messages preserve exact order without duplicates",page.truth(
            "sharedEvents.filter(e=>e.kind==='burst').length===25&&sharedEvents.filter(e=>e.kind==='burst').every((e,i)=>e.phase==='return'&&e.number===i)&&!senderEvents.some(e=>e.kind==='relay'&&e.value.kind==='burst')"));
        CHECK("terminated intermediate owner has no retained native isolate",page.spin([&]{return sender_native.expired()&&g_workers.size()==2;}));
        js_eval(page.ctx,"worker.postMessage({op:'adopt',port:returned},[returned]);returned.close();","test:queue-stress-retransfer");
        CHECK("persistent recipient adopts the returned endpoint",page.event("adopted"));
        js_eval(page.ctx,"worker.postMessage({op:'send',value:43});","test:queue-stress-retransfer-echo");
        CHECK("retransferred connection remains live",page.spin([&]{return page.truth("events.some(e=>e.kind==='relay'&&e.value.kind==='echo'&&e.value.value===43&&e.value.connection===1)");}));
        js_eval(page.ctx,"worker.postMessage({op:'closeBusy'});","test:queue-stress-close-barrier");
        CHECK("persistent receiver enters declared120ms close barrier",page.spin([&]{return page.truth("events.some(e=>e.kind==='busy'&&e.op==='closeBusy')");}));
        js_eval(page.ctx,"control.port.postMessage({op:'burst',target:1,phase:'close',count:25});","test:queue-stress-produce-close");
        CHECK("producer confirms25 messages before close",page.spin([&]{return page.truth("controlEvents.some(e=>e.kind==='producer-complete'&&e.phase==='close'&&e.count===25)");}));
        CHECK("close barrier is established with pending queued messages",page.truth("!events.some(e=>e.kind==='closed')"));
        CHECK("persistent recipient closes queued endpoint",page.event("closed"));
        CHECK("closing queued endpoint releases its route while control remains",page.spin([&]{return g_shared_clients.size()==1&&g_workers.size()==2;}));
        CHECK("closed queued endpoint did not dispatch its pending messages",page.truth("!events.some(e=>e.kind==='relay'&&e.value.kind==='burst')"));
        js_eval(page.ctx,"control.port.close();","test:queue-stress-close-control");
        CHECK("all shared routes release before persistent receiver termination",page.spin([&]{return g_shared_clients.empty()&&g_workers.size()==1;}));
        CHECK("shared isolate and its pending payloads are reclaimed",page.spin([&]{return shared_native.expired();}));
        CHECK("only the intentional persistent receiver thread remains",page.spin([&]{WorkerThreads &threads=worker_threads();std::lock_guard<std::mutex> lock(threads.mu);return threads.running==1;}));
        js_eval(page.ctx,"worker.postMessage({op:'ping'});","test:queue-stress-persistent-ping");
        CHECK("persistent receiver stays responsive after queue closure",page.event("pong"));
        CHECK("native migrated payload queues and forwarding routes are empty",page.spin([&]{
            const NativeQueueCounts counts=native_queue_counts();
            return !counts.routes&&!counts.moving&&!counts.input&&!counts.output;
        }));
        CHECK("queue lifecycle drains final network ownership",drain_network());
        js_eval(page.ctx,"shared=null;control=null;sender=null;returned=null;sharedEvents=[];controlEvents=[];senderEvents=[];events=[];","test:queue-stress-clear");
        QueueResources current=queue_resources();
        if(cycle>4&&(current.threads>warm.threads||current.fds>warm.fds)) {
            const double began=now_ms(),deadline=began+500;
            while(now_ms()<deadline&&(current.threads>warm.threads||current.fds>warm.fds)) {
                page.pump();std::this_thread::sleep_for(std::chrono::milliseconds(5));current=queue_resources();
            }
            printf("SHARED_QUEUE_STRESS_SETTLE cycle=%u milliseconds=%.3f threads=%ld fds=%ld\n",cycle,now_ms()-began,current.threads,current.fds);
        }
        CHECK("queue lifecycle OS resource counters are readable",current.rss_kib>0&&current.threads>0&&current.fds>=0);
        if(cycle==4)warm=current;
        if(cycle>4) {
            CHECK("queue lifecycle worker threads do not accumulate",current.threads<=warm.threads);
            CHECK("queue lifecycle descriptors do not accumulate",current.fds<=warm.fds);
            CHECK("warm queue lifecycle RSS growth remains below64MiB",current.rss_kib-warm.rss_kib<=64*1024);
        }
        queue_checkpoint(cycle,current);
    }
    page.dispose();
    CHECK("all queue lifecycle workers shut down",js_workers_shutdown(3000));
    CHECK("all queue lifecycle requests finalize",drain_network());
    printf("SHARED_QUEUE_STRESS_IDLE_BEGIN\n");fflush(stdout);
    std::this_thread::sleep_for(std::chrono::seconds(12));
    queue_checkpoint(cycles+1,queue_resources());
    if(fsync(queue_journal))++failures;
    close(queue_journal);queue_journal=-1;
    net_shutdown();
    printf("RESULTS shared-worker-queue-lifetime-stress %s (%d/%d checks)\n",failures?"FAIL":"PASS",checks-failures,checks);
    return failures?1:0;
}
