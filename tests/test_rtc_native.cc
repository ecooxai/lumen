/* Real native ICE/DTLS/SCTP loopback and independent document lifetimes.
 * This is not the separate Chrome interoperability acceptance test. */
#include "../src/js/js_int.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
extern "C" void font_init(void);
static int checks, failures;
#define CHECK(name, condition) do { bool ok_=(condition); ++checks; printf("%s %s\n",ok_?"PASS":"FAIL",name); if(!ok_)++failures; } while(0)
static bool value(JsCtx *c,const char *source) {
    JS_ENTER(c); v8::TryCatch caught(iso); v8::Local<v8::Script> script; v8::Local<v8::Value> result;
    bool ok=v8::Script::Compile(ctx,jstr(iso,source)).ToLocal(&script)&&script->Run(ctx).ToLocal(&result);
    if(!ok&&caught.HasCaught())fprintf(stderr,"RTC test JS: %s\n",jcstr(iso,caught.Exception()).c_str());
    iso->PerformMicrotaskCheckpoint(); return ok&&result->BooleanValue(iso);
}
struct Fixture {
    Document *doc; JsCtx *context;
    explicit Fixture(const char *url) {
        doc=doc_new(url); const char *html="<!doctype html><html><body></body></html>";
        html_parse(doc,html,strlen(html)); JsHost host{}; context=js_new(doc,&host);
    }
    void close(){if(context){js_free(context);context=nullptr;}}
    ~Fixture(){close();doc_free(doc);}
};
static const char *begin = R"JS(
globalThis.run={phase:'starting',error:null,echoes:[],binary:[],offerStaged:false};
globalThis.left=new RTCPeerConnection({iceServers:[]});
globalThis.right=new RTCPeerConnection({iceServers:[]});
globalThis.outgoing=left.createDataChannel('lumen-native',{ordered:true});
outgoing.binaryType='arraybuffer';
outgoing.onmessage=e=>{if(typeof e.data==='string')run.echoes.push(e.data);else run.binary.push(Array.from(new Uint8Array(e.data)));};
right.ondatachannel=e=>{globalThis.incoming=e.channel;incoming.binaryType='arraybuffer';incoming.onmessage=m=>incoming.send(m.data);};
function gathered(p){if(p.iceGatheringState==='complete')return Promise.resolve();return new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error('ICE gathering timeout')),8000);const changed=()=>{if(p.iceGatheringState==='complete'){clearTimeout(timer);p.removeEventListener('icegatheringstatechange',changed);resolve();}};p.addEventListener('icegatheringstatechange',changed);changed();});}
(async()=>{
 const offer=await left.createOffer();
 run.offerStaged=left.localDescription===null&&left.signalingState==='stable';
 await left.setLocalDescription(offer);await gathered(left);
 run.gatheredOffer=/^a=candidate:/m.test(left.localDescription.sdp);
 await right.setRemoteDescription(left.localDescription);
 await right.setLocalDescription(await right.createAnswer());await gathered(right);
 await left.setRemoteDescription(right.localDescription);
 run.phase='negotiated';
})().catch(e=>{run.error=e.name+': '+e.message;});
true
)JS";
static bool until(Fixture &one,Fixture *two,const char *expression,int milliseconds=12000) {
    int rounds=milliseconds/5;
    for(int i=0;i<rounds;++i){if(one.context)js_tick(one.context);if(two&&two->context)js_tick(two->context);if(one.context&&value(one.context,expression))return true;usleep(5000);}
    if(one.context)value(one.context,"console.error('RTC state',JSON.stringify(run));true");return false;
}
int main(int argc,char **argv) {
    (void)argc;unsetenv("LUMEN_EXPERIMENTAL_RTC_LOOPBACK");
    dom_init();net_init(2);font_init();js_global_init(argv[0]);
    {
        Fixture first("https://rtc-one.example/"),second("https://rtc-two.example/");
        bool available=value(first.context,"typeof RTCPeerConnection==='function' && navigator.lumenCapabilities.webrtcBackend==='libdatachannel-0.24.6' && navigator.lumenCapabilities.webrtcNetworkTransport===true");
        CHECK("real native transport is exposed explicitly",available);
        if(!available){net_shutdown();printf("RESULTS rtc-native FAIL (backend unavailable)\n");return 1;}
        CHECK("RTP transport is explicitly unsupported",value(first.context,"navigator.lumenCapabilities.webrtcMediaTransport===false && typeof RTCRtpSender==='undefined'"));
        CHECK("first document creates actual native peers",value(first.context,begin));
        CHECK("second document creates independent native peers",value(second.context,begin));
        CHECK("first native ICE/DTLS/SCTP pair connects",until(first,&second,"!run.error && left.connectionState==='connected' && right.connectionState==='connected' && outgoing.readyState==='open' && globalThis.incoming?.readyState==='open'"));
        CHECK("second native ICE/DTLS/SCTP pair connects",until(second,&first,"!run.error && left.connectionState==='connected' && right.connectionState==='connected' && outgoing.readyState==='open' && globalThis.incoming?.readyState==='open'"));
        CHECK("createOffer leaves local state staged",value(first.context,"run.offerStaged"));
        CHECK("gathered SDP contains real host candidates",value(first.context,"run.gatheredOffer"));
        value(first.context,"outgoing.send('first-document-nonce');true");
        value(second.context,"outgoing.send('second-document-before-close');true");
        CHECK("first document exchanges actual data payload",until(first,&second,"run.echoes.includes('first-document-nonce')"));
        CHECK("second document exchanges actual data payload",until(second,&first,"run.echoes.includes('second-document-before-close')"));
        CHECK("unrepresentable zero packet lifetime rejects",value(second.context,"(()=>{try{left.createDataChannel('zero',{maxPacketLifeTime:0});return false}catch(e){return e.name==='NotSupportedError'}})()"));
        CHECK("oversized send fails before enqueue",value(second.context,"(()=>{try{outgoing.send(new Uint8Array(1048577));return false}catch(e){return e.name==='TypeError'}})()"));
        CHECK("native registry owns four real peers across documents",js_rtc_live_peers()==4);
        first.close();
        CHECK("document teardown preserves the other two native peers",js_rtc_live_peers()==2);
        value(second.context,"outgoing.send('second-document-after-close');true");
        CHECK("closing one document leaves another transport alive",until(second,nullptr,"run.echoes.includes('second-document-after-close') && left.connectionState==='connected'"));
        value(second.context,"const mutable=new Uint8Array([17,0,255,92]);outgoing.send(mutable);mutable.fill(0);true");
        CHECK("binary sends copy bytes independently of JS mutation",until(second,nullptr,"run.binary.some(v=>v.join(',')==='17,0,255,92')"));
        value(second.context,"globalThis.actualStats=null;left.getStats().then(s=>actualStats=Array.from(s.values()));true");
        CHECK("stats report actual channel payload counters",until(second,nullptr,"actualStats && actualStats.some(s=>s.type==='data-channel'&&s.bytesSent>0&&s.bytesReceived>0)"));
        CHECK("stats do not fabricate DTLS wire counters",value(second.context,"!actualStats.some(s=>s.type==='transport'||s.type==='certificate')"));
        value(second.context,"globalThis.budgetChannel=left.createDataChannel('budget-pressure');true");
        CHECK("second real channel opens for aggregate backpressure",until(second,nullptr,"budgetChannel.readyState==='open'"));
        value(second.context,R"JS(
            globalThis.pressure={error:null,peak:0,accepted:0};
            const payload=new Uint8Array(1048576);
            for(let i=0;i<64;i++) {
                try {(i%2?budgetChannel:outgoing).send(payload);pressure.accepted++;}
                catch(e){pressure.error=e.name;break;}
                pressure.peak=Math.max(pressure.peak,outgoing.bufferedAmount+budgetChannel.bufferedAmount);
            }
            console.log('RTC real backpressure',JSON.stringify(pressure));true
        )JS");
        CHECK("real multi-channel pressure reaches bounded rejection",value(second.context,"pressure.error==='OperationError' && pressure.accepted>0"));
        CHECK("aggregate queued payload remains within document budget",value(second.context,"pressure.peak<=8388608 && outgoing.bufferedAmount+budgetChannel.bufferedAmount<=8388608"));
        CHECK("native callbacks close with queued tasks",value(second.context,"left.close();right.close();left.connectionState==='closed'&&outgoing.readyState==='closed'"));
        for(int i=0;i<8;++i){js_tick(second.context);usleep(1000);}
        CHECK("closed peer operations reject",value(second.context,"globalThis.closedRejection=false;left.createOffer().catch(e=>closedRejection=e.name==='InvalidStateError');true"));
        CHECK("closed peer rejection is delivered",until(second,nullptr,"closedRejection",100));
    }
    {
        Fixture quota("https://rtc-quota.example/");
        CHECK("native peer accepts its bounded channel allocation",value(quota.context,"globalThis.quotaPeer=new RTCPeerConnection();globalThis.quotaChannels=[];for(let i=0;i<128;i++)quotaChannels.push(quotaPeer.createDataChannel('quota-'+i));quotaChannels.length===128"));
        CHECK("channel129 is rejected by the actual native limit",value(quota.context,"(()=>{try{quotaPeer.createDataChannel('quota-overflow');return false}catch(e){return e.name==='OperationError'}})()"));
        CHECK("native document accepts its bounded peer allocation",value(quota.context,"globalThis.quotaPeers=[quotaPeer];for(let i=1;i<32;i++)quotaPeers.push(new RTCPeerConnection());quotaPeers.length===32"));
        CHECK("peer33 is rejected by the actual native document limit",value(quota.context,"(()=>{try{new RTCPeerConnection();return false}catch(e){return e.name==='OperationError'}})()"));
    }
    CHECK("final document teardown releases all native peers",js_rtc_live_peers()==0);
    net_shutdown();printf("RESULTS rtc-native %s (%d/%d checks; real native sockets, local loopback only)\n",failures?"FAIL":"PASS",checks-failures,checks);return failures?1:0;
}
