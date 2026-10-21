/* JS bridge for camera/microphone capture, permissions and fixed geolocation */
#include "js_int.h"
#include <cstdlib>
#include <string>
extern "C" {
int cap_cameras(char (*names)[128], int max); int cap_cam_open(int idx, int w, int h, int fps); void cap_cam_info(int id, int *w, int *h, int *fps);
void cap_cam_bind(int id, Node *n); void cap_cam_close(int id); int cap_mics(char (*names)[128], int max); int cap_mic_open(int rate, int ch);
int cap_mic_read(int id, float *out, int max); int cap_screen_open(void); int cap_rec_start(int src, int bps); void cap_rec_pause(int id, bool on); uint8_t *cap_rec_stop(int id, int *len); void cap_mic_close(int id); int cap_perm_state(const char *kind, const char *origin); int cap_ask(const char *kind, const char *origin);
}
using FCI = v8::FunctionCallbackInfo<v8::Value>;
#define FN(nm) static void n_##nm(const FCI &a)
#define CTX v8::Isolate *iso = a.GetIsolate(); v8::Local<v8::Context> ctx = iso->GetCurrentContext()
#define INT(i) (int)a[i]->NumberValue(ctx).FromMaybe(0)
static void names(const FCI &a, int (*f)(char (*)[128], int)) { CTX; char nm[16][128]; int n = f(nm, 16); v8::Local<v8::Array> r = v8::Array::New(iso, n); for (int i = 0; i < n; i++) (void)r->Set(ctx, i, jstr(iso, nm[i])); a.GetReturnValue().Set(r); }
static bool allowed(const FCI &a, const char *kind) { return js_capture_allowed(jctx(a.GetIsolate()), kind); }
static bool granted(const FCI &a, const char *kind) { JsCtx *c=jctx(a.GetIsolate()); return js_capture_allowed(c,kind) && cap_perm_state(kind,ctx_origin(c).c_str())>0; }
FN(capAllowed) { a.GetReturnValue().Set(allowed(a,jcstr(a.GetIsolate(),a[0]).c_str())); }
FN(rtcLoopbackEnabled) { const char *s=getenv("LUMEN_EXPERIMENTAL_RTC_LOOPBACK");a.GetReturnValue().Set(s && !strcmp(s,"1")); }
FN(capCameras) { if(allowed(a,"camera")) names(a, cap_cameras); else a.GetReturnValue().Set(v8::Array::New(a.GetIsolate())); }
FN(capMics) { if(allowed(a,"microphone")) names(a, cap_mics); else a.GetReturnValue().Set(v8::Array::New(a.GetIsolate())); }
FN(capCamOpen) { CTX; int id=granted(a,"camera")?cap_cam_open(INT(0), INT(1), INT(2), INT(3)):0; if(id>0)jctx(iso)->capture_cameras[id]=1; a.GetReturnValue().Set(id); }
FN(capCamInfo) { CTX; int w, h, f; cap_cam_info(INT(0), &w, &h, &f); v8::Local<v8::Array> r = v8::Array::New(iso, 3); (void)r->Set(ctx, 0, v8::Integer::New(iso, w)); (void)r->Set(ctx, 1, v8::Integer::New(iso, h)); (void)r->Set(ctx, 2, v8::Integer::New(iso, f)); a.GetReturnValue().Set(r); }
FN(capCamBind) { CTX; int id=INT(0); if(!id || jctx(iso)->capture_cameras.count(id))cap_cam_bind(id, a[1]->IsObject() ? junwrap(a[1]) : nullptr); }
FN(capCamRetain) { CTX; auto &m=jctx(iso)->capture_cameras;auto i=m.find(INT(0)); if(i!=m.end())++i->second; a.GetReturnValue().Set(i!=m.end()); }
FN(capCamClose) { CTX; auto &m=jctx(iso)->capture_cameras;auto i=m.find(INT(0)); if(i!=m.end() && --i->second==0){ cap_cam_close(i->first);m.erase(i); } }
FN(recStart) { CTX; JsCtx *c=jctx(iso);int id=c->capture_cameras.count(INT(0))?cap_rec_start(INT(0), INT(1)):0; if(id)c->capture_recorders.insert(id);a.GetReturnValue().Set(id); }
FN(recPause) { CTX; if(jctx(iso)->capture_recorders.count(INT(0)))cap_rec_pause(INT(0), a[1]->BooleanValue(iso)); (void)ctx; }
FN(recStop) { CTX; int n = 0; uint8_t *b = jctx(iso)->capture_recorders.erase(INT(0))?cap_rec_stop(INT(0), &n):nullptr; v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(iso, (size_t)n);
    if (n) memcpy(ab->GetBackingStore()->Data(), b, (size_t)n); free(b); a.GetReturnValue().Set(ab); }
FN(capScreenOpen) { JsCtx *c=jctx(a.GetIsolate());int id=c->capture_screen_authorized && allowed(a,"screen")?cap_screen_open():0;c->capture_screen_authorized=false;if(id)c->capture_cameras[id]=1;a.GetReturnValue().Set(id); }
FN(capMicOpen) { CTX; int id=granted(a,"microphone")?cap_mic_open(INT(0), INT(1)):0; if(id)jctx(iso)->capture_mics[id]=1;a.GetReturnValue().Set(id); }
FN(capMicRetain) { CTX;auto &m=jctx(iso)->capture_mics;auto i=m.find(INT(0));if(i!=m.end())++i->second;a.GetReturnValue().Set(i!=m.end()); }
FN(capMicClose) { CTX; auto &m=jctx(iso)->capture_mics;auto i=m.find(INT(0));if(i!=m.end() && --i->second==0){cap_mic_close(i->first);m.erase(i);} }
FN(capMicRead) { CTX; static float buf[48000 * 2]; int n = jctx(iso)->capture_mics.count(INT(0))?cap_mic_read(INT(0), buf, 48000 * 2):-1; if (n < 0) return;
    v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(iso, (size_t)n * 4); if (n) memcpy(ab->GetBackingStore()->Data(), buf, (size_t)n * 4); a.GetReturnValue().Set(v8::Float32Array::New(ab, 0, n)); }
FN(capState) { CTX;JsCtx *c=jctx(iso);std::string k=jcstr(iso,a[0]);a.GetReturnValue().Set(js_capture_allowed(c,k.c_str())?cap_perm_state(k.c_str(),ctx_origin(c).c_str()):-1); }
FN(capAsk) { CTX;JsCtx *c=jctx(iso);std::string k=jcstr(iso,a[0]);bool screen=k=="screen";
    bool ok=js_capture_allowed(c,k.c_str()) && (!screen || js_windows_has_activation(c)) && cap_ask(k.c_str(),ctx_origin(c).c_str())!=0;
    if(screen)c->capture_screen_authorized=ok;a.GetReturnValue().Set(ok); }
FN(geoFixed) { CTX; const char *g = getenv("LUMEN_GEO"); a.GetReturnValue().Set(jstr(iso, g ? g : "")); }
void js_capture_close(JsCtx *c) {
    for(int id:c->capture_recorders){int len=0;free(cap_rec_stop(id,&len));}c->capture_recorders.clear();
    for(auto &entry:c->capture_cameras)cap_cam_close(entry.first);c->capture_cameras.clear();
    for(auto &entry:c->capture_mics)cap_mic_close(entry.first);c->capture_mics.clear();c->capture_screen_authorized=false;
}
void js_blob_mod(const std::string &u, const std::string *src);
FN(blobMod) {
    v8::Isolate *iso = a.GetIsolate();
    std::string u = jcstr(iso, a[0]);
    if (a.Length() > 1 && a[1]->IsString()) { std::string src = jcstr(iso, a[1]); js_blob_mod(u, &src); }
    else js_blob_mod(u, nullptr);
}
void js_install_capture(JsCtx *c, v8::Local<v8::Object> N) {
    v8::Isolate *iso = c->iso; v8::Local<v8::Context> ctx = iso->GetCurrentContext();
#define REG(nm) (void)N->Set(ctx, jstr(iso, #nm), v8::Function::New(ctx, n_##nm).ToLocalChecked())
    REG(capAllowed); REG(rtcLoopbackEnabled); REG(capCamRetain); REG(capMicRetain); REG(blobMod); REG(capCameras); REG(capMics); REG(capCamOpen); REG(capCamInfo); REG(capCamBind); REG(capCamClose); REG(capScreenOpen); REG(recStart); REG(recPause); REG(recStop); REG(capMicOpen); REG(capMicClose); REG(capMicRead); REG(capState); REG(capAsk); REG(geoFixed);
#undef REG
}
