/* Real V8/DOM snapshots and native player lifetime, without a fake quality API. */
#include "../src/js/js_int.h"
#include <cstdio>
#include <cstring>
extern "C" void font_init(void);
static int checks,failures;
#define CHECK(name,condition) do { bool ok_=(condition);checks++;printf("%s %s\n",ok_?"PASS":"FAIL",name);if(!ok_)failures++; } while(0)
static bool value(JsCtx *c,const char *source) {
    JS_ENTER(c);v8::TryCatch caught(iso);
    v8::Local<v8::Script> script;v8::Local<v8::Value> result;
    bool ok=v8::Script::Compile(ctx,jstr(iso,source)).ToLocal(&script)&&script->Run(ctx).ToLocal(&result);
    if(!ok&&caught.HasCaught())fprintf(stderr,"JS exception: %s\n",jcstr(iso,caught.Exception()).c_str());
    iso->PerformMicrotaskCheckpoint();return ok&&result->BooleanValue(iso);
}
int main(int argc,char **argv) {
    (void)argc;dom_init();net_init(2);font_init();js_global_init(argv[0]);
    Document *document=doc_new("https://quality.test/");
    const char *html="<!doctype html><html><body><video id=v></video></body></html>";
    html_parse(document,html,strlen(html));JsHost host{};JsCtx *c=js_new(document,&host);
    CHECK("real DOM video exposes an unloaded zero-valued snapshot",value(c,
        "globalThis.v=document.getElementById('v');globalThis.q=v.getVideoPlaybackQuality();"
        "q.totalVideoFrames===0&&q.droppedVideoFrames===0&&q.corruptedVideoFrames===0"));
    CHECK("the exposed interface brands actual quality snapshots",value(c,
        "typeof VideoPlaybackQuality==='function'&&q instanceof VideoPlaybackQuality&&Object.prototype.toString.call(q)==='[object VideoPlaybackQuality]'"));
    CHECK("quality creationTime is a real high-resolution snapshot time",value(c,
        "Number.isFinite(q.creationTime)&&q.creationTime>=0&&q.creationTime<=performance.now()+1"));
    CHECK("all quality attributes have readonly prototype accessors",value(c,
        "['creationTime','totalVideoFrames','droppedVideoFrames','corruptedVideoFrames'].every(k=>{"
        "const d=Object.getOwnPropertyDescriptor(Object.getPrototypeOf(q),k);"
        "return d&&typeof d.get==='function'&&d.set===undefined&&d.enumerable===true;})"));
    CHECK("strict assignment cannot replace any quality attribute",value(c,
        "(()=>{'use strict';return ['creationTime','totalVideoFrames','droppedVideoFrames','corruptedVideoFrames'].every(k=>{"
        "const before=q[k];let rejected=false;try{q[k]=12345;}catch(e){rejected=e instanceof TypeError;}return rejected&&q[k]===before;});})()"));
    CHECK("quality constructor rejects script-created native snapshots",value(c,
        "(()=>{if(typeof VideoPlaybackQuality!=='function')return false;try{new VideoPlaybackQuality();return false;}catch(e){return e instanceof TypeError;}})()"));
    CHECK("quality getters reject unbranded receiver objects",value(c,
        "(()=>{const d=Object.getOwnPropertyDescriptor(Object.getPrototypeOf(q),'totalVideoFrames');"
        "if(!d||!d.get)return false;try{d.get.call({});return false;}catch(e){return e instanceof TypeError;}})()"));
    CHECK("video quality method requires an actual video receiver",value(c,
        "(()=>{try{HTMLVideoElement.prototype.getVideoPlaybackQuality.call({});return false;}catch(e){return e instanceof TypeError;}})()"));
    CHECK("each call returns a fresh stable snapshot",value(c,
        "globalThis.creation=q.creationTime;globalThis.q2=v.getVideoPlaybackQuality();"
        "q2!==q&&q.creationTime===creation&&q2.creationTime>=creation&&q.totalVideoFrames===0"));
    CHECK("MediaSource attachment creates a real native player",value(c,
        "v.srcObject=new MediaSource();v.getVideoPlaybackQuality().totalVideoFrames===0")&&c->players.size()==1);
    CHECK("seeking a real player cannot fabricate displayed or dropped frames",value(c,
        "v.currentTime=1000;globalThis.q3=v.getVideoPlaybackQuality();"
        "q3.totalVideoFrames===0&&q3.droppedVideoFrames===0&&q3.corruptedVideoFrames===0"));
    CHECK("the media load algorithm resets native player lifetime",value(c,
        "v.srcObject=null;v.load();globalThis.q4=v.getVideoPlaybackQuality();"
        "q4.totalVideoFrames===0&&q4.droppedVideoFrames===0&&q4.corruptedVideoFrames===0")&&c->players.empty());
    CHECK("older snapshots remain unchanged after seek and load",value(c,
        "q.creationTime===creation&&q.totalVideoFrames===0&&q3.totalVideoFrames===0&&q4!==q3"));
    js_release_pins();js_free(c);doc_free(document);net_shutdown();
    printf("RESULTS video-quality-js %d/%d (%d failures)\n",checks-failures,checks,failures);
    return failures?1:0;
}
