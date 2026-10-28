#pragma once
#include <v8.h>
#include <functional>
#include <memory>
#include <string>

struct JsCtx;
class JsForegroundTasks;
struct JsBroadcastContext;
struct JsBroadcastIdentity {
    std::string origin, origin_key, site, top_site, cluster;
    bool cross_site_ancestor = false;
    bool origin_sandboxed = false;
    std::string storage_key() const;
};
bool js_broadcast_has_psl();
std::string js_broadcast_site_key(const std::string &origin, const std::string &opaque_key);
JsBroadcastIdentity js_broadcast_identity(JsCtx *);
JsBroadcastIdentity js_broadcast_worker_identity(const JsBroadcastIdentity &, const std::string &url);
std::shared_ptr<JsBroadcastContext> js_broadcast_install(
    v8::Local<v8::Context>, v8::Local<v8::Object>, const JsBroadcastIdentity &,
    std::shared_ptr<JsForegroundTasks>, std::function<bool()> eligible);
/* Deactivate at document/worker shutdown, before its native owner can disappear.
   The inert object stays alive until its V8 context is released. */
void js_broadcast_deactivate(const std::shared_ptr<JsBroadcastContext> &);
