/* JS bridge for camera/microphone capture, permissions and fixed geolocation */
#include "js_int.h"
#include <cstdlib>
#include <string>
extern "C" {
int cap_cameras(char (*names)[128], int max); int cap_cam_open(int idx, int w, int h, int fps); void cap_cam_info(int id, int *w, int *h, int *fps);
void cap_cam_bind(int id, Node *n); void cap_cam_close(int id); int cap_mics(char (*names)[128], int max); int cap_mic_open(int rate, int ch);
int cap_mic_read(int id, float *out, int max); void cap_mic_close(int id); int cap_perm_state(const char *kind, const char *origin); int cap_ask(const char *kind, const char *origin);
}
using FCI = v8::FunctionCallbackInfo<v8::Value>;
#define FN(nm) static void n_##nm(const FCI &a)
#define CTX v8::Isolate *iso = a.GetIsolate(); v8::Local<v8::Context> ctx = iso->GetCurrentContext()
#define INT(i) (int)a[i]->NumberValue(ctx).FromMaybe(0)
static void names(const FCI &a, int (*f)(char (*)[128], int)) { CTX; char nm[16][128]; int n = f(nm, 16); v8::Local<v8::Array> r = v8::Array::New(iso, n); for (int i = 0; i < n; i++) (void)r->Set(ctx, i, jstr(iso, nm[i])); a.GetReturnValue().Set(r); }
FN(capCameras) { names(a, cap_cameras); }
FN(capMics) { names(a, cap_mics); }
FN(capCamOpen) { CTX; a.GetReturnValue().Set(cap_cam_open(INT(0), INT(1), INT(2), INT(3))); }
FN(capCamInfo) { CTX; int w, h, f; cap_cam_info(INT(0), &w, &h, &f); v8::Local<v8::Array> r = v8::Array::New(iso, 3); (void)r->Set(ctx, 0, v8::Integer::New(iso, w)); (void)r->Set(ctx, 1, v8::Integer::New(iso, h)); (void)r->Set(ctx, 2, v8::Integer::New(iso, f)); a.GetReturnValue().Set(r); }
FN(capCamBind) { CTX; cap_cam_bind(INT(0), a[1]->IsObject() ? junwrap(a[1]) : nullptr); }
FN(capCamClose) { CTX; cap_cam_close(INT(0)); }
FN(capMicOpen) { CTX; a.GetReturnValue().Set(cap_mic_open(INT(0), INT(1))); }
FN(capMicClose) { CTX; cap_mic_close(INT(0)); }
FN(capMicRead) { CTX; static float buf[48000 * 2]; int n = cap_mic_read(INT(0), buf, 48000 * 2); if (n < 0) return;
    v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(iso, (size_t)n * 4); if (n) memcpy(ab->GetBackingStore()->Data(), buf, (size_t)n * 4); a.GetReturnValue().Set(v8::Float32Array::New(ab, 0, n)); }
FN(capState) { CTX; a.GetReturnValue().Set(cap_perm_state(jcstr(iso, a[0]).c_str(), jcstr(iso, a[1]).c_str())); }
FN(capAsk) { CTX; a.GetReturnValue().Set(cap_ask(jcstr(iso, a[0]).c_str(), jcstr(iso, a[1]).c_str()) != 0); }
FN(geoFixed) { CTX; const char *g = getenv("LUMEN_GEO"); a.GetReturnValue().Set(jstr(iso, g ? g : "")); }
void js_install_capture(JsCtx *c, v8::Local<v8::Object> N) {
    v8::Isolate *iso = c->iso; v8::Local<v8::Context> ctx = iso->GetCurrentContext();
#define REG(nm) (void)N->Set(ctx, jstr(iso, #nm), v8::Function::New(ctx, n_##nm).ToLocalChecked())
    REG(capCameras); REG(capMics); REG(capCamOpen); REG(capCamInfo); REG(capCamBind); REG(capCamClose); REG(capMicOpen); REG(capMicClose); REG(capMicRead); REG(capState); REG(capAsk); REG(geoFixed);
#undef REG
}
