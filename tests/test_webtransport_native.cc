/* Real V8/native HTTP/3 integration against the independent local aioquic
 * endpoint. This does not replace the public WebTransport facade/page gate. */
#include "../src/js/js_int.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>

extern "C" void font_init(void);
static int checks, failures;
static std::string manifest, endpoint;
static uint64_t expected_datagram_bytes, expected_stream_bytes;
#define CHECK(name, condition) do { bool ok_=(condition); ++checks; printf("%s %s\n",ok_?"PASS":"FAIL",name); fflush(stdout); if(!ok_)++failures; } while(0)

static bool value(JsCtx *c, const char *source) {
    JS_ENTER(c); v8::TryCatch caught(iso);
    v8::Local<v8::Script> script; v8::Local<v8::Value> result;
    bool ok=v8::Script::Compile(ctx,jstr(iso,source)).ToLocal(&script)&&script->Run(ctx).ToLocal(&result);
    if(!ok&&caught.HasCaught()) fprintf(stderr,"WebTransport fixture exception: %s\n",jcstr(iso,caught.Exception()).c_str());
    iso->PerformMicrotaskCheckpoint(); return ok&&result->BooleanValue(iso);
}

static const char *helpers=R"JS(
globalThis.configText=JSON.stringify({url:publicFixtureEndpoint,certificateHashes:[JSON.parse(publicCertificateManifest).certificateHash]});
globalThis.operationId=0;
globalThis.run={error:null,done:false};
function startNative(text=configText){
 const t={id:0,ready:false,closed:false,terminal:null,pending:new Map(),callbackCount:0};
 t.id=__nativeWT.wtCreate(text,e=>{
  t.callbackCount++;
  if(e.type==='ready'){t.ready=true;t.readyEvent=e;return;}
  if(e.type==='closed'){
   t.closed=true;t.terminal=e;
   for(const p of t.pending.values()){const error=new Error(e.errorMessage||'Transport closed');error.name=e.errorName||'InvalidStateError';p.reject(error);}
   t.pending.clear();return;
  }
  const p=t.pending.get(e.requestId);if(!p)return;t.pending.delete(e.requestId);
  if(e.type==='error'){const error=new Error(e.message);error.name=e.name;p.reject(error);}else p.resolve(e);
 });
 return t;
}
function operation(t,stream,op,payload=null,code=0){
 const request=++operationId;
 return new Promise((resolve,reject)=>{
  t.pending.set(request,{resolve,reject});
  try{__nativeWT.wtCommand(t.id,stream,op,request,code,payload);}catch(e){t.pending.delete(request);reject(e);}
 });
}
function closeNative(t){__nativeWT.wtCommand(t.id,0,12,0,0,new ArrayBuffer(0));}
function sameBytes(a,b){return a.length===b.length&&a.every((x,i)=>x===b[i]);}
async function collectEOF(t,id){
 const result=[];
 for(let i=0;i<32;i++){
  const event=await operation(t,id,7);
  if(!(event.data instanceof ArrayBuffer))throw new Error('Read payload was not an owned ArrayBuffer');
  result.push(...new Uint8Array(event.data));if(result.length>4096)throw new Error('Unexpected stream payload bound');
  if(event.done)return {bytes:result,eof:true};
 }
 throw new Error('The stream did not reach genuine EOF');
}
async function exchange(t,nonce){
 const original=Array.from(new TextEncoder().encode(nonce));
 const mutable=new Uint8Array(original);
 const incoming=operation(t,0,2);
 const sent=operation(t,0,1,mutable.buffer);mutable.fill(0);
 await sent;const datagram=await incoming;
 const echoed=new Uint8Array(datagram.data);
 if(!sameBytes(Array.from(echoed),original))throw new Error('Fresh datagram or copy mismatch');
 const reliable=await streamExchange(t,nonce);
 return {datagram:true,...reliable};
}
async function streamExchange(t,nonce){
 const original=Array.from(new TextEncoder().encode(nonce));
 const stream=await operation(t,0,3);
 if(!Number.isSafeInteger(stream.streamId)||stream.streamId<=0||stream.direction!=='bidirectional')throw new Error('Invalid actual stream handle');
 const received=collectEOF(t,stream.streamId);
 await operation(t,stream.streamId,8,new Uint8Array(original).buffer);
 await operation(t,stream.streamId,9);
 const bytes=await received;
 if(!bytes.eof||!sameBytes(bytes.bytes,original))throw new Error('Reliable stream bytes or EOF mismatch');
 return {stream:true,eof:true,bytes:original.length};
}
function launchExchange(t,nonce){run.done=false;run.error=null;exchange(t,nonce).then(result=>{run.result=result;run.done=true;},e=>run.error=e.name+': '+e.message);}
true
)JS";

static void expose_test_primitives(JsCtx *c) {
    JS_ENTER(c); auto native=v8::Object::New(iso);js_install_webtransport(c,native);
    (void)ctx->Global()->CreateDataProperty(ctx,jstr(iso,"__nativeWT"),native);
    (void)ctx->Global()->CreateDataProperty(ctx,jstr(iso,"publicCertificateManifest"),jstr(iso,manifest.c_str()));
    (void)ctx->Global()->CreateDataProperty(ctx,jstr(iso,"publicFixtureEndpoint"),jstr(iso,endpoint.c_str()));
}
struct Fixture {
    Document *doc=nullptr;JsCtx *context=nullptr;
    explicit Fixture(const char *url="http://127.0.0.1:8765/") {
        doc=doc_new(url);const char *html="<!doctype html><html><body></body></html>";
        html_parse(doc,html,strlen(html));JsHost host{};context=js_new(doc,&host);
        expose_test_primitives(context);value(context,helpers);
    }
    JsCtx *frame(const char *url) {
        Node *element=node_new_element(doc,"iframe",NS_HTML);node_set_attr(element,"src",url);node_append(doc->body,element);
        Document *child=doc_new(url);const char *html="<!doctype html><html><body></body></html>";html_parse(child,html,strlen(html));
        JsHost host{};JsCtx *realm=js_new_ex(child,&host,context,element);expose_test_primitives(realm);value(realm,helpers);return realm;
    }
    void close(){if(context){js_free(context);context=nullptr;}}
    ~Fixture(){close();doc_free(doc);}
};

static bool until(Fixture &one,Fixture *two,const char *expression,int milliseconds=10000) {
    for(int i=0;i<milliseconds/5;++i){
        if(one.context)js_tick(one.context);if(two&&two->context)js_tick(two->context);
        if(one.context&&value(one.context,expression))return true;usleep(5000);
    }
    if(one.context)value(one.context,"console.error('Native WebTransport bounded wait failed',run.error);true");return false;
}
static bool resources_empty() {
    uint64_t counts[6];js_webtransport_resource_counts(counts);
    for(uint64_t count:counts)if(count)return false;return true;
}
static void put_number(JsCtx *c,const char *name,double number) {
    JS_ENTER(c);(void)ctx->Global()->CreateDataProperty(ctx,jstr(iso,name),v8::Number::New(iso,number));
}
static double number(JsCtx *c,const char *source) {
    JS_ENTER(c);v8::Local<v8::Script>script;v8::Local<v8::Value>result;
    if(!v8::Script::Compile(ctx,jstr(iso,source)).ToLocal(&script)||!script->Run(ctx).ToLocal(&result)||!result->IsNumber())return 0;
    return result.As<v8::Number>()->Value();
}

int main(int argc,char **argv) {
    if(argc!=3 || !*argv[2] || strlen(argv[2])>5 || strspn(argv[2],"0123456789")!=strlen(argv[2])){
        fprintf(stderr,"Usage: test_webtransport_native local-public-certificate-manifest.json loopback-udp-port\n");return 2;
    }
    unsigned long port=strtoul(argv[2],nullptr,10);
    if(!port || port>65535){fprintf(stderr,"A valid explicit loopback UDP port is required\n");return 2;}
    endpoint="https://127.0.0.1:"+std::to_string(port)+"/echo";
    std::ifstream file(argv[1]);std::ostringstream input;input<<file.rdbuf();manifest=input.str();
    if(!file||manifest.empty()||manifest.size()>4096){fprintf(stderr,"A bounded local public certificate manifest is required\n");return 2;}
    dom_init();net_init(2);font_init();js_global_init(argv[0]);
    bool compiled=false;
    {
        Fixture available;compiled=value(available.context,"__nativeWT.wtNativeAvailable()===true");
        CHECK("the native HTTP/3 backend is actually linked",compiled);
    }
    if(!compiled){net_shutdown();printf("RESULTS webtransport-native FAIL (backend disabled)\n");return 1;}
    {
        Fixture insecure("http://untrusted.example/");
        CHECK("insecure native context hides backend availability",value(insecure.context,"!__nativeWT.wtNativeAvailable()"));
        value(insecure.context,"globalThis.isSecureContext=true;true");
        CHECK("spoofed JS security state cannot create native transport",value(insecure.context,"(()=>{try{startNative();return false}catch(e){return e.name==='SecurityError'}})()"));
        JsCtx *child=insecure.frame("https://secure-child.example/");
        CHECK("a secure child cannot bypass its insecure ancestor",value(child,"(()=>{try{startNative();return false}catch(e){return e.name==='SecurityError'}})()"));
    }
    {
        Fixture policy;policy.doc->csp=strdup("connect-src 'none'");
        CHECK("native connect-src rejects before network allocation",value(policy.context,"(()=>{try{startNative();return false}catch(e){return e.name==='SecurityError'}})()"));
    }
    CHECK("policy rejection leaves no owner or network resources",resources_empty());
    {
        Fixture invalid;
        CHECK("non-HTTPS endpoint is rejected before allocation",value(invalid.context,"(()=>{try{startNative(JSON.stringify({url:'http://127.0.0.1:4433/echo'}));return false}catch(e){return e.name==='TypeError'}})()"));
        CHECK("credential-bearing endpoint is rejected before allocation",value(invalid.context,"(()=>{try{startNative(JSON.stringify({url:'https://user:password@127.0.0.1:4433/echo'}));return false}catch(e){return e.name==='TypeError'}})()"));
        CHECK("malformed JSON rejects rather than leaving a pending promise",value(invalid.context,"(()=>{try{startNative('{');return false}catch(e){return e.name==='TypeError'}})()"));
        CHECK("certificate pin count is bounded before native allocation",value(invalid.context,"(()=>{const c=JSON.parse(configText);c.certificateHashes=Array(9).fill(Array(32).fill(0));try{startNative(JSON.stringify(c));return false}catch(e){return e.name==='TypeError'}})()"));
        CHECK("an empty native pin array rejects before owner allocation",value(invalid.context,"(()=>{const c=JSON.parse(configText);c.certificateHashes=[];try{startNative(JSON.stringify(c));return false}catch(e){return e.name==='TypeError'}})()"));
        CHECK("endpoint URL UTF-8 length is bounded before owner allocation",value(invalid.context,"(()=>{const c=JSON.parse(configText);c.url+='?'+('a'.repeat(4096));try{startNative(JSON.stringify(c));return false}catch(e){return e.name==='TypeError'}})()"));
        CHECK("rejected input never reserves a native owner",value(invalid.context,"__nativeWT.wtResourceCounts().owners===0 && __nativeWT.wtResourceCounts().sessions===0"));
    }
    CHECK("invalid configuration leaves no owner or network resources",resources_empty());
    {
        Fixture first,second;
        CHECK("native configuration never invokes page toJSON hooks",value(first.context,R"JS(
const poisoned=JSON.parse(configText);poisoned.origin='https://spoof.invalid';
const serialized=JSON.stringify(poisoned);
globalThis.toJSONCalls=0;Object.prototype.toJSON=function(){toJSONCalls++;return {url:publicFixtureEndpoint,origin:'https://spoof.invalid'};};
try{globalThis.session=startNative(serialized);}finally{delete Object.prototype.toJSON;}
toJSONCalls===0
)JS"));
        CHECK("a second document reserves a separate real connection",value(second.context,"globalThis.session=startNative();true"));
        CHECK("the independent server accepts the immutable native Origin",until(first,&second,"session.ready && session.readyEvent.alpn==='h3'"));
        CHECK("the second document negotiates actual HTTP/3",until(second,&first,"session.ready && session.readyEvent.alpn==='h3'"));
        std::string fresh="native-wt-"+std::to_string(getpid())+"-"+std::to_string(now_ms());
        expected_datagram_bytes=(fresh+"-one").size()+(fresh+"-two").size()+(fresh+"-after-other-document-close").size();
        expected_stream_bytes=expected_datagram_bytes+(fresh+"-after-route-cancel").size();
        std::string start="launchExchange(session,'"+fresh+"-one');true";value(first.context,start.c_str());
        start="launchExchange(session,'"+fresh+"-two');true";value(second.context,start.c_str());
        CHECK("fresh native datagram and reliable stream bytes reach real EOF",until(first,&second,"run.done && !run.error && run.result.datagram && run.result.stream && run.result.eof"));
        CHECK("a separate document exchanges independently copied payload",until(second,&first,"run.done && !run.error && run.result.bytes>0"));
        put_number(second.context,"otherDocumentSession",number(first.context,"session.id"));
        CHECK("one document cannot command another document's native handle",value(second.context,"(()=>{try{__nativeWT.wtCommand(otherDocumentSession,0,13,900000,0,null);return false}catch(e){return e.name==='InvalidStateError'}})()"));
        CHECK("native payload bound rejects a 64KiB-plus-one chunk",value(second.context,"(()=>{try{__nativeWT.wtCommand(session.id,0,1,900001,0,new ArrayBuffer(65537));return false}catch(e){return e.name==='QuotaExceededError'}})()"));
        CHECK("out-of-range operation code is rejected by the owning bridge",value(second.context,"(()=>{try{__nativeWT.wtCommand(session.id,0,17,900003,0,null);return false}catch(e){return e.name==='TypeError'}})()"));
        value(second.context,"globalThis.actualStats=null;operation(session,0,13).then(e=>actualStats=e,e=>run.error=e.name);true");
        CHECK("stats distinguish measured UDP and application payload bytes",until(second,&first,"actualStats && actualStats.udpBytesSent>0 && actualStats.udpBytesReceived>0 && actualStats.datagramPayloadBytesSent>0 && actualStats.datagramPayloadBytesReceived>0 && actualStats.streamPayloadBytesSent>0 && actualStats.streamPayloadBytesReceived>0"));
        first.close();
        start="launchExchange(session,'"+fresh+"-after-other-document-close');true";value(second.context,start.c_str());
        CHECK("document teardown preserves another document's actual transport",until(second,nullptr,"run.done && !run.error && !session.closed && run.result.eof"));
        value(second.context,R"JS(
globalThis.routeCancelled={datagram:false,bi:false,uni:false};
globalThis.routeCompletions=0;
operation(session,0,2).catch(e=>routeCancelled.datagram=e.name==='AbortError');
operation(session,0,5).catch(e=>routeCancelled.bi=e.name==='AbortError');
operation(session,0,6).catch(e=>routeCancelled.uni=e.name==='AbortError');
true
)JS");
        for(int i=0;i<20;++i){js_tick(second.context);usleep(5000);}
        CHECK("native datagram and both incoming stream credits remain genuinely pending",value(second.context,"session.pending.size===3 && !routeCancelled.datagram && !routeCancelled.bi && !routeCancelled.uni"));
        value(second.context,"for(const op of [14,15,16])operation(session,0,op).then(()=>routeCompletions++,e=>run.error=e.name);true");
        CHECK("route cancellation rejects pending credits and acknowledges all three cancellations",until(second,nullptr,"routeCancelled.datagram && routeCancelled.bi && routeCancelled.uni && routeCompletions===3 && session.pending.size===0 && !session.closed"));
        start="run.done=false;run.error=null;streamExchange(session,'"+fresh+"-after-route-cancel').then(r=>{run.result=r;run.done=true},e=>run.error=e.name);true";value(second.context,start.c_str());
        CHECK("cancelling receive routes preserves outgoing reliable stream I/O",until(second,nullptr,"run.done && !run.error && run.result.eof && !session.closed"));
        value(second.context,R"JS(
globalThis.pendingReadRejected=false;globalThis.closeStreamReady=false;
operation(session,0,3).then(s=>{operation(session,s.streamId,7).catch(()=>pendingReadRejected=true);closeStreamReady=true},e=>run.error=e.name);
true
)JS");
        CHECK("a stream read is outstanding before session close",until(second,nullptr,"closeStreamReady && session.pending.size===1 && !pendingReadRejected"));
        value(second.context,"closeNative(session);true");
        CHECK("explicit close finishes endpoint cleanup and rejects pending reads",until(second,nullptr,"session.closed && session.terminal.clean===true && session.terminal.localCloseRequested===true && pendingReadRejected && session.pending.size===0"));
        CHECK("closed handles cannot issue another network operation",value(second.context,"(()=>{try{__nativeWT.wtCommand(session.id,0,13,900002,0,null);return false}catch(e){return e.name==='InvalidStateError'}})()"));
    }
    CHECK("all positive-case owners, sessions, streams, tasks and buffers drain",resources_empty());
    {
        Fixture quota;
        CHECK("four native session reservations are accepted",value(quota.context,"globalThis.sessions=Array.from({length:4},()=>startNative());sessions.length===4"));
        CHECK("the fifth session rejects at the document bound",value(quota.context,"(()=>{try{startNative();return false}catch(e){return e.name==='QuotaExceededError'}})()"));
        quota.close();
    }
    CHECK("teardown cancels pending handshakes within native bounds",resources_empty());
    {
        Fixture negative;
        value(negative.context,"const bad=JSON.parse(configText);bad.certificateHashes[0][0]^=255;globalThis.session=startNative(JSON.stringify(bad));true");
        CHECK("wrong pin fails for the actual certificate reason",until(negative,nullptr,"session.closed && !session.ready && session.terminal.errorName==='SecurityError' && /certificate/i.test(session.terminal.errorMessage||'')"));
        CHECK("negative TLS result never claims clean local closure",value(negative.context,"session.terminal.clean===false && session.terminal.localCloseRequested===false"));
    }
    {
        Fixture negative;
        value(negative.context,"globalThis.session=startNative(JSON.stringify({url:publicFixtureEndpoint}));true");
        CHECK("normal native roots reject the local self-signed certificate",until(negative,nullptr,"session.closed && !session.ready && session.terminal.errorName==='SecurityError' && /certificate/i.test(session.terminal.errorMessage||'')"));
    }
    CHECK("negative TLS cases release every native resource",resources_empty());
    printf("CORRELATION webtransport-native datagramBytes=%llu streamBytes=%llu\n",(unsigned long long)expected_datagram_bytes,(unsigned long long)expected_stream_bytes);
    net_shutdown();printf("RESULTS webtransport-native %s (%d/%d checks; independent local HTTP/3 endpoint)\n",failures?"FAIL":"PASS",checks-failures,checks);return failures?1:0;
}
