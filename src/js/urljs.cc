/* URL parsing natives shared by page and worker contexts: urlParse(input, base) and urlSet(href, key, value, strict) return the
   URL's parts [href, protocol, username, password, host, hostname, port, pathname, search, hash, origin] or null. */
#include "js_int.h"
extern "C" {
#include "../base/url.h"
#include "../base/encoding.h"
}
#include <algorithm>

namespace {
typedef v8::FunctionCallbackInfo<v8::Value> FCI;
v8::Local<v8::Value> parts(v8::Isolate *iso, const URL &u) {
    char *href = url_to_string(&u), *origin = url_origin_web(&u);
    std::string user, pass;
    if (u.userinfo) { const char *col = strchr(u.userinfo, ':'); user = col ? std::string(u.userinfo, (size_t)(col - u.userinfo)) : u.userinfo; if (col) pass = col + 1; }
    std::string hostname = u.host ? u.host : "", port = u.has_port ? std::to_string(u.port) : "";
    std::string f[11] = { href, std::string(u.scheme) + ":", user, pass, hostname + (port.empty() ? "" : ":" + port), hostname, port, u.path ? u.path : "",
                          u.query && *u.query ? std::string("?") + u.query : "", u.fragment && *u.fragment ? std::string("#") + u.fragment : "", origin };
    free(href); free(origin);
    v8::Local<v8::Context> ctx = iso->GetCurrentContext();
    v8::Local<v8::Array> r = v8::Array::New(iso, 11);
    for (int i = 0; i < 11; i++) (void)r->Set(ctx, i, jstr(iso, f[i].c_str(), (int)f[i].size()));
    return r;
}
void url_parse_fn(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate();
    std::string s = jcstr(iso, a[0]);
    URL u = {}, b = {};
    bool ok;
    int enc = 0;   /* urlParse(input, base, encoding): a document's URLs encode their query in its encoding */
    if (a.Length() > 2 && a[2]->IsString()) { std::string l = jcstr(iso, a[2]); enc = std::max(0, enc_label(l.data(), l.size())); }
    if (a[1]->IsNullOrUndefined()) ok = url_parse_enc(s.data(), s.size(), nullptr, &u, enc);
    else {
        std::string bs = jcstr(iso, a[1]);
        ok = url_parse_n(bs.data(), bs.size(), nullptr, &b) && url_parse_enc(s.data(), s.size(), &b, &u, enc);
        url_free(&b);
    }
    if (ok && u.scheme) a.GetReturnValue().Set(parts(iso, u)); else a.GetReturnValue().SetNull();
    url_free(&u);
}
void url_set_fn(const FCI &a) {
    v8::Isolate *iso = a.GetIsolate();
    std::string href = jcstr(iso, a[0]), key = jcstr(iso, a[1]), v = jcstr(iso, a[2]);
    URL u = {};
    if (!url_parse_n(href.data(), href.size(), nullptr, &u)) { a.GetReturnValue().SetNull(); url_free(&u); return; }
    /* urlSet(href, key, value, true): null when the setter's parse fails (URLPattern's canonicalization) */
    if (!url_set(&u, key.c_str(), v.data(), v.size()) && a.Length() > 3 && a[3]->IsTrue()) a.GetReturnValue().SetNull();
    else a.GetReturnValue().Set(parts(iso, u));
    url_free(&u);
}
}

void js_url_install(v8::Local<v8::Context> ctx, v8::Local<v8::Object> target) {
    v8::Isolate *iso = v8::Isolate::GetCurrent();
    (void)target->Set(ctx, jstr(iso, "urlParse"), v8::Function::New(ctx, url_parse_fn).ToLocalChecked());
    (void)target->Set(ctx, jstr(iso, "urlSet"), v8::Function::New(ctx, url_set_fn).ToLocalChecked());
}
