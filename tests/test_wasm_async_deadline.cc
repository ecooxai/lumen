/* Real V8 asynchronous compilation must wake a dormant window promptly.
 * This harness follows the application's deadline-driven sleep, including its
 * one-second maximum idle wait. It does not force a periodic test tick. */
#include "../src/js/js_int.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unistd.h>
extern "C" void font_init(void);
static int checks=0, failures=0;
#define CHECK(label, value) do { const bool ok=(value); ++checks; if(!ok)++failures; printf("%s %s\n",ok?"PASS":"FAIL",label); } while(0)
static bool evaluate(JsCtx *c, const char *source) {
    JS_ENTER(c); v8::TryCatch caught(iso); v8::Local<v8::Script> script; v8::Local<v8::Value> result;
    const bool ok=v8::Script::Compile(ctx,jstr(iso,source)).ToLocal(&script)&&script->Run(ctx).ToLocal(&result);
    if(!ok&&caught.HasCaught())fprintf(stderr,"script: %s\n",jcstr(iso,caught.Exception()).c_str());
    iso->PerformMicrotaskCheckpoint(); return ok&&result->BooleanValue(iso);
}
static void settle(JsCtx *c) { for(int i=0;i<25;++i){js_tick(c);usleep(4000);} }
int main(int argc,char **argv) {
    (void)argc;dom_init();net_init(2);font_init();js_global_init(argv[0]);
    Document *doc=doc_new("http://127.0.0.1/wasm-async-deadline");
    const char *html="<!doctype html><html><body></body></html>";html_parse(doc,html,strlen(html));
    JsHost host{};JsCtx *c=js_new(doc,&host);settle(c);
    CHECK("WebAssembly is available in the actual V8 context",evaluate(c,"typeof WebAssembly==='object'&&typeof WebAssembly.instantiate==='function'"));
    CHECK("idle window has no forced short polling deadline",js_next_deadline(c)-now_ms()>500);
    CHECK("async compilation sequence starts",evaluate(c,R"JS(
      globalThis.wasmDone=false;globalThis.wasmError='';globalThis.wasmCount=0;
      globalThis.wasmValues=[];
      const leb=n=>{const a=[];do{let b=n&127;n>>>=7;if(n)b|=128;a.push(b);}while(n);return a;};
      const section=(id,payload)=>[id,...leb(payload.length),...payload];
      const count=2000;
      const code=[...leb(count)];for(let n=0;n<count;n++)code.push(4,0,65,42,11);
      globalThis.wasmBytes=new Uint8Array([0,97,115,109,1,0,0,0,
        ...section(1,[1,96,0,1,127]),...section(3,[...leb(count),...Array(count).fill(0)]),
        ...section(7,[1,6,97,110,115,119,101,114,0,0]),...section(10,code)]);
      (async()=>{for(let n=0;n<8;n++){const r=await WebAssembly.instantiate(wasmBytes);wasmValues.push(r.instance.exports.answer());wasmCount++;}wasmDone=true;})()
        .catch(e=>{wasmError=String(e);wasmDone=true;});true;
    )JS"));
    const bool pending=c->iso->HasPendingBackgroundTasks();
    const double first=js_next_deadline(c)-now_ms();
    printf("DIAGNOSTIC pending_background=%d initial_deadline_ms=%.3f\n",pending,first);
    CHECK("real async WebAssembly work is pending before its foreground completion",pending);
    CHECK("pending V8 work contributes a bounded foreground deadline",std::isfinite(first)&&first>=-1&&first<=20);
    const double started=now_ms();int turns=0;
    while(!evaluate(c,"wasmDone")&&now_ms()-started<12000){
        js_tick(c);++turns;if(evaluate(c,"wasmDone"))break;
        const double wait=std::min(1000.0,std::max(0.0,js_next_deadline(c)-now_ms()));
        if(wait>0)usleep((useconds_t)(ceil(wait)*1000));
    }
    const double elapsed=now_ms()-started;
    printf("DIAGNOSTIC compile_sequence_ms=%.3f foreground_turns=%d\n",elapsed,turns);
    CHECK("eight real asynchronous instances return the expected computation",evaluate(c,"wasmDone&&wasmError===''&&wasmCount===8&&wasmValues.every(v=>v===42)"));
    CHECK("async work is not delayed by the one-second idle sleep",elapsed<350);
    settle(c);
    CHECK("completed compilation removes its short polling deadline",!c->iso->HasPendingBackgroundTasks()&&js_next_deadline(c)-now_ms()>500);
    CHECK("ordinary timers retain their earlier requested deadline",evaluate(c,"globalThis.timerDone=false;setTimeout(()=>timerDone=true,25);true")&&js_next_deadline(c)-now_ms()<35);
    js_free(c);doc_free(doc);js_workers_shutdown(5000);net_shutdown();
    printf("RESULTS wasm-async-deadline %s (%d/%d checks)\n",failures?"FAIL":"PASS",checks-failures,checks);return failures?1:0;
}
