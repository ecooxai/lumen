#ifndef LUMEN_JS_URL_PARTS_H
#define LUMEN_JS_URL_PARTS_H
#include "js_int.h"
extern "C" {
#include "../base/url.h"
}

/* Main realms and workers expose exactly the same native URL record fields.
 * The caller owns the record and may free it after this function returns. */
inline v8::Local<v8::Array> jsg_url_parts(v8::Isolate *iso, v8::Local<v8::Context> ctx, const URL &u) {
    char *href = url_to_string(&u);
    std::string user, pass;
    if (u.userinfo) { const char *col = strchr(u.userinfo, ':'); user = col ? std::string(u.userinfo, (size_t)(col - u.userinfo)) : u.userinfo; if (col) pass = col + 1; }
    std::string hostname = u.host ? u.host : "";
    std::string port = u.has_port ? std::to_string(u.port) : "";
    std::string host = hostname + (port.empty() ? "" : ":" + port);
    std::string origin = "null";
    { char *o = url_origin(&u); if (o) { origin = o; free(o); } }
    std::string f[11] = { href ? href : "", std::string(u.scheme) + ":", user, pass, host, hostname, port, u.path ? u.path : "",
                          u.query && *u.query ? std::string("?") + u.query : "", u.fragment && *u.fragment ? std::string("#") + u.fragment : "", origin };
    free(href);
    v8::Local<v8::Array> r = v8::Array::New(iso, 11);
    for (int i = 0; i < 11; i++) (void)r->Set(ctx, i, jstr(iso, f[i].c_str(), (int)f[i].size()));
    return r;
}
#endif
