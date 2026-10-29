/* Real native transport/pool ownership regression. The loopback server and
 * existing cancellation controls are shared without changing their fixtures.
 * NET_BACKGROUND_BASELINE only omits the new opt-in field, permitting a RED
 * run against the original implementation; it does not replace the transport.
 */
#define main inherited_cancellation_regression_main
#include "test_net_cancel.c"
#undef main

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int done, released, heads, chunks;
    size_t bytes;
    uint64_t id;
    bool body_ok, reentrant_pending;
    pthread_t done_thread, released_thread, head_thread, chunk_thread;
} Completion;

static void completion_init(Completion *c) {
    memset(c,0,sizeof *c);
    pthread_mutex_init(&c->mu,NULL);pthread_cond_init(&c->cv,NULL);
}
static void completion_destroy(Completion *c) {
    pthread_cond_destroy(&c->cv);pthread_mutex_destroy(&c->mu);
}
static void delivered(NetRequest *rq,NetResponse *r,void *ud) {
    Completion *c=ud;
    // Reenter a network query: native completion must run with g_mu unlocked.
    bool pending=net_pending()>0;
    pthread_mutex_lock(&c->mu);
    c->done++;c->id=rq->id;c->done_thread=pthread_self();
    c->body_ok=r&&r->status==200&&r->body_len==2&&!memcmp(r->body,"OK",2);
    c->reentrant_pending=pending;
    pthread_cond_broadcast(&c->cv);pthread_mutex_unlock(&c->mu);
}
static void released(void *ud) {
    Completion *c=ud;
    pthread_mutex_lock(&c->mu);
    c->released++;c->released_thread=pthread_self();
    pthread_cond_broadcast(&c->cv);pthread_mutex_unlock(&c->mu);
}
static void received_head(NetRequest *rq,NetResponse *r,void *ud) {
    (void)rq;(void)r;Completion *c=ud;
    pthread_mutex_lock(&c->mu);c->heads++;c->head_thread=pthread_self();pthread_mutex_unlock(&c->mu);
}
static void received_chunk(NetRequest *rq,const char *bytes,size_t n,void *ud) {
    (void)rq;(void)bytes;Completion *c=ud;
    pthread_mutex_lock(&c->mu);c->chunks++;c->bytes+=n;c->chunk_thread=pthread_self();pthread_mutex_unlock(&c->mu);
}
static void set_background(NetRequest *rq) {
#ifndef NET_BACKGROUND_BASELINE
    rq->background_done=true;
#else
    (void)rq;
#endif
}
static void observe(NetRequest *rq,Completion *c,bool background) {
    rq->done=delivered;rq->release=released;rq->ud=c;
    if(background)set_background(rq);
}
static bool wait_release(Completion *c,int ms) {
    struct timespec until=deadline_ms(ms);
    pthread_mutex_lock(&c->mu);
    while(!c->released&&pthread_cond_timedwait(&c->cv,&c->mu,&until)!=ETIMEDOUT){}
    bool done=c->released>0;pthread_mutex_unlock(&c->mu);return done;
}
static bool pending_without_poll(int ms) {
    const double end=now_ms()+ms;
    do {
        if(!net_pending())return true;
        struct timespec wait={0,1000000};nanosleep(&wait,NULL);
    }while(now_ms()<end);
    return false;
}
static void no_main_poll(void) {
    printf("CASE background native completion without main net_poll\n");
    Server s;bool started=server_start(&s,STALL_HEAD);CHECK("background HTTP server starts",started);if(!started)return;
    Completion c;completion_init(&c);
    NetRequest *rq=request_for(&s);observe(rq,&c,true);uint64_t id=net_fetch(rq);
    CHECK("background request reaches real loopback HTTP server",server_ready(&s));
    server_release(&s);
    CHECK("background response and finalizer finish without net_poll",wait_release(&c,1200));
    CHECK("background request leaves no pending ownership without net_poll",pending_without_poll(300));
    // Reclaim old-baseline events after recording the unsupported behavior.
    CHECK("background test cleanup drains completely",drain(3000));
    server_finish(&s);
    CHECK("background completion and finalizer each run exactly once",c.done==1&&c.released==1);
    CHECK("background response preserves real HTTP payload and returned request ID",c.body_ok&&c.id==id&&id!=0);
    CHECK("background completion runs away from main thread",!pthread_equal(c.done_thread,pthread_self()));
    CHECK("background owner finalizer runs away from main thread",!pthread_equal(c.released_thread,pthread_self()));
    CHECK("background completion can query network state without lock inversion",c.reentrant_pending);
    completion_destroy(&c);
}
static void default_dispatch(bool streaming) {
    printf("CASE %s transport preserves main-thread dispatch\n",streaming?"streaming opt-in":"default");
    Server s;bool started=server_start(&s,streaming?STALL_FIXED:STALL_HEAD);CHECK("main dispatch HTTP server starts",started);if(!started)return;
    Completion c;completion_init(&c);
    NetRequest *rq=request_for(&s);observe(rq,&c,streaming);
    if(streaming){rq->head=received_head;rq->chunk=received_chunk;}
    uint64_t id=net_fetch(rq);
    CHECK("main dispatch request reaches real loopback server",server_ready(&s));
    server_release(&s);server_finish(&s);
    struct timespec settle={0,50000000};nanosleep(&settle,NULL);
    pthread_mutex_lock(&c.mu);bool quiet=c.done==0&&c.released==0&&c.heads==0&&c.chunks==0;pthread_mutex_unlock(&c.mu);
    CHECK("default and streaming callbacks wait for main net_poll",quiet);
    CHECK("main dispatch request drains after net_poll resumes",drain(3000));
    CHECK("main dispatch completion and finalizer run exactly once",c.done==1&&c.released==1);
    CHECK("main dispatch preserves request identity",c.id==id&&id!=0);
    CHECK("main dispatch callback and finalizer retain main thread affinity",pthread_equal(c.done_thread,pthread_self())&&pthread_equal(c.released_thread,pthread_self()));
    if(streaming){
        CHECK("streamed response keeps its actual head and body bytes",c.heads==1&&c.chunks>=1&&c.bytes==2);
        CHECK("streamed head and chunks retain main thread affinity",pthread_equal(c.head_thread,pthread_self())&&pthread_equal(c.chunk_thread,pthread_self()));
    }else CHECK("default response preserves real HTTP body",c.body_ok);
    completion_destroy(&c);
}
static void canceled_background(void) {
    printf("CASE background canceled socket retains exact-once finalization\n");
    Server s;bool started=server_start(&s,STALL_HEAD);CHECK("cancel background HTTP server starts",started);if(!started)return;
    Completion c;completion_init(&c);
    NetRequest *rq=request_for(&s);observe(rq,&c,true);uint64_t id=net_fetch(rq);
    CHECK("cancel background request reaches held HTTP response",server_ready(&s));
    net_cancel(id);net_cancel(id);
    CHECK("canceled background request finalizes without main net_poll",wait_release(&c,1000));
    CHECK("canceled background ownership drains without main net_poll",pending_without_poll(300));
    CHECK("cancel background cleanup drains queued baseline events",drain(3000));
    server_release(&s);server_finish(&s);
    CHECK("cancellation suppresses response delivery but finalizes once",c.done==0&&c.released==1);
    completion_destroy(&c);
}

enum { BURST_COUNT=4000 };
typedef struct { Completion *states;uint64_t *ids; } Burst;
static void *produce(void *arg) {
    Burst *b=arg;
    for(int i=0;i<BURST_COUNT;i++){
        NetRequest *rq=net_request_new("GET","data:text/plain,OK");
        observe(rq,&b->states[i],true);
        b->ids[i]=net_fetch(rq);
    }
    return NULL;
}
static void concurrent_request_ids(void) {
    printf("CASE concurrent producer/poller fast-completion request identity\n");
    Completion *states=calloc(BURST_COUNT,sizeof *states);
    uint64_t *ids=calloc(BURST_COUNT,sizeof *ids);
    CHECK("burst ownership allocation succeeds",states&&ids);if(!states||!ids){free(states);free(ids);return;}
    for(int i=0;i<BURST_COUNT;i++)completion_init(&states[i]);
    Burst burst={states,ids};pthread_t producer;
    bool started=pthread_create(&producer,NULL,produce,&burst)==0;
    CHECK("concurrent transport producer starts",started);
    if(started){
        // A separate producer and completion pump exercise the hand-off where
        // net_fetch must return its copied ID, never read a freed request.
        const double end=now_ms()+12000;
        while(now_ms()<end){
            net_poll();
            pthread_mutex_lock(&states[BURST_COUNT-1].mu);
            bool last=states[BURST_COUNT-1].released!=0;
            pthread_mutex_unlock(&states[BURST_COUNT-1].mu);
            if(last)break;
            struct timespec pause={0,10000};nanosleep(&pause,NULL);
        }
        pthread_join(producer,NULL);
        CHECK("all fast transports finalize before the deadline",drain(4000));
        bool once=true,identities=true,bodies=true;
        for(int i=0;i<BURST_COUNT;i++){
            once=once&&states[i].done==1&&states[i].released==1;
            identities=identities&&ids[i]!=0&&ids[i]==states[i].id&&(i==0||ids[i]>ids[i-1]);
            bodies=bodies&&states[i].body_ok;
        }
        CHECK("all4000 completions and finalizers run exactly once",once);
        CHECK("all4000 returned IDs match their completion despite concurrent finalization",identities);
        CHECK("all4000 response bodies remain intact",bodies);
    }
    for(int i=0;i<BURST_COUNT;i++)completion_destroy(&states[i]);
    free(states);free(ids);
}
int main(void) {
    alarm(45);net_init(2);
    no_main_poll();default_dispatch(false);default_dispatch(true);canceled_background();concurrent_request_ids();
    CHECK("background tests leave no pending native requests",drain(3000));
    net_shutdown();alarm(0);
    printf("net background completion tests: %d/%d passed\n",checks-failures,checks);
    return failures?1:0;
}
