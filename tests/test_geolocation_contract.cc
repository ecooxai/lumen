/* Geolocation timing/cancellation with an explicit fixed native test provider.
 * These tests do not claim GPS, CoreLocation, or physical position accuracy. */
#include "../src/js/js_int.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
extern "C" void font_init(void);
static int checks,failures;
#define CHECK(name,condition) do{bool ok_=(condition);++checks;printf("%s %s\n",ok_?"PASS":"FAIL",name);if(!ok_)++failures;}while(0)
static bool value(JsCtx *c,const char *code){JS_ENTER(c);v8::TryCatch caught(iso);v8::Local<v8::Script>s;v8::Local<v8::Value>v;bool ok=v8::Script::Compile(ctx,jstr(iso,code)).ToLocal(&s)&&s->Run(ctx).ToLocal(&v);iso->PerformMicrotaskCheckpoint();return ok&&v->BooleanValue(iso);}
static void pump(JsCtx *c){for(int i=0;i<25;++i){js_tick(c);usleep(1000);}}
struct Fixture{Document*d;JsCtx*c;Fixture(){d=doc_new("https://location.example/");const char*h="<!doctype html><html><body></body></html>";html_parse(d,h,strlen(h));JsHost host{};c=js_new(d,&host);}~Fixture(){js_free(c);doc_free(d);}};
int main(int argc,char**argv){
 (void)argc;setenv("LUMEN_PERMISSIONS","grant",1);setenv("LUMEN_GEO","30,110,10",1);dom_init();net_init(2);font_init();js_global_init(argv[0]);
 {
  Fixture f;
  CHECK("success callback is required synchronously",value(f.c,"(()=>{try{navigator.geolocation.getCurrentPosition(null);return false;}catch(e){return e.name==='TypeError';}})()"));
  CHECK("watch callback is required synchronously",value(f.c,"(()=>{try{navigator.geolocation.watchPosition(null);return false;}catch(e){return e.name==='TypeError';}})()"));
 }
 {
  Fixture f;
  value(f.c,"globalThis.geoResult='pending';navigator.geolocation.getCurrentPosition(()=>geoResult='success',e=>geoResult=e.code,{timeout:0});true");pump(f.c);
  CHECK("timeout zero fails when no acceptable cache exists",value(f.c,"geoResult===3"));
 }
 {
  Fixture f;
  value(f.c,"globalThis.firstPosition=null;navigator.geolocation.getCurrentPosition(p=>firstPosition=p);true");pump(f.c);
  CHECK("fixed native provider yields supplied coordinates",value(f.c,"firstPosition.coords.latitude===30 && firstPosition.coords.longitude===110 && firstPosition.coords.accuracy===10"));
  setenv("LUMEN_GEO","31,111,20",1);
  value(f.c,"globalThis.cachedPosition=null;navigator.geolocation.getCurrentPosition(p=>cachedPosition=p,null,{maximumAge:60000,timeout:0});true");pump(f.c);
  CHECK("maximumAge reuses a sufficiently fresh position",value(f.c,"cachedPosition && cachedPosition.coords.latitude===30"));
  CHECK("cached position retains original acquisition timestamp",value(f.c,"cachedPosition && cachedPosition.timestamp===firstPosition.timestamp"));
  value(f.c,"globalThis.freshPosition=null;navigator.geolocation.getCurrentPosition(p=>freshPosition=p,null,{maximumAge:0});true");pump(f.c);
  CHECK("maximumAge zero acquires a new position",value(f.c,"freshPosition && freshPosition.coords.latitude===31"));
  size_t timers=f.c->timers.size();
  value(f.c,"navigator.geolocation.getCurrentPosition(()=>{},()=>{},{timeout:20000});true");pump(f.c);
  CHECK("successful acquisition clears its timeout timer",f.c->timers.size()==timers);
 }
 {
  Fixture f;size_t timers=f.c->timers.size();
  value(f.c,"globalThis.watchCalls=0;globalThis.watchId=navigator.geolocation.watchPosition(()=>watchCalls++,()=>watchCalls++);navigator.geolocation.clearWatch(watchId);true");pump(f.c);
  CHECK("clearWatch suppresses a queued initial callback",value(f.c,"watchCalls===0"));
  CHECK("clearWatch releases all native JS timers",f.c->timers.size()==timers);
 }
 {
  Fixture f;setenv("LUMEN_PERMISSIONS","deny",1);size_t timers=f.c->timers.size();
  value(f.c,"globalThis.denied=0;navigator.geolocation.watchPosition(()=>{},e=>denied=e.code);true");pump(f.c);
  CHECK("permission denial reports PERMISSION_DENIED",value(f.c,"denied===1"));
  CHECK("denied watch does not keep a background polling interval",f.c->timers.size()==timers);setenv("LUMEN_PERMISSIONS","grant",1);
 }
 {
  Fixture f;setenv("LUMEN_GEO","999,0,10",1);
  value(f.c,"globalThis.invalid='pending';navigator.geolocation.getCurrentPosition(()=>invalid='success',e=>invalid=e.code);true");pump(f.c);
  CHECK("invalid provider coordinates are POSITION_UNAVAILABLE",value(f.c,"invalid===2"));
  setenv("LUMEN_GEO","0,0,0",1);
  value(f.c,"globalThis.zero=null;navigator.geolocation.getCurrentPosition(p=>zero=p);true");pump(f.c);
  CHECK("zero coordinates and explicit zero accuracy are preserved",value(f.c,"zero && zero.coords.latitude===0 && zero.coords.longitude===0 && zero.coords.accuracy===0"));
 }
 unsetenv("LUMEN_GEO");net_shutdown();printf("RESULTS geolocation-contract %s (%d/%d checks; fixed provider fixture)\n",failures?"FAIL":"PASS",checks-failures,checks);return failures?1:0;
}
