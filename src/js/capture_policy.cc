/* Camera, microphone and location policy is derived from immutable native
 * document principals. Script-writable location/window properties never grant it. */
#include "js_int.h"
#include <cctype>
#include <sstream>

namespace {
std::string trim(std::string s) {
    size_t a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");
    return a==std::string::npos ? std::string() : s.substr(a,b-a+1);
}
std::string feature(const char *name) {
    if (!name) return {};
    if (!strcmp(name,"location")) return "geolocation";
    if (!strcmp(name,"screen")) return "display-capture";
    return name;
}
std::string canonical_origin(const std::string &text) {
    URL url{}; if(!url_parse(text.c_str(),&url)) return {};
    char *s=url_origin(&url); std::string out=s?s:"";free(s);url_free(&url);return out;
}
bool origin_token(std::string item,const std::string &self,const std::string &target,const std::string &src) {
    if(item=="*" || item=="\"*\"")return true;
    if(item=="self" || item=="'self'")return self==target;
    if(item=="src" || item=="'src'")return !src.empty() && src==target;
    if(item.size()>1 && ((item.front()=='\"' && item.back()=='\"') || (item.front()=='\'' && item.back()=='\'')))item=item.substr(1,item.size()-2);
    if(item=="none" || item.empty())return false;
    /* Invalid/unsupported wildcard origins do not expand a grant. */
    return item.find('*')==std::string::npos && canonical_origin(item)==target;
}
bool list_allows(const std::string &list,const std::string &self,const std::string &target,const std::string &src) {
    std::istringstream in(list); std::string item;bool allowed=false;
    while(in>>item){ if(item=="'none'" || item=="none")return false;allowed=allowed||origin_token(item,self,target,src); }
    return allowed;
}
bool header_allows(const char *text,const std::string &name,const std::string &self,const std::string &target) {
    if(!text || !*text)return true;
    std::istringstream in(text);std::string directive;
    while(std::getline(in,directive,',')){
        size_t eq=directive.find('=');if(eq==std::string::npos || trim(directive.substr(0,eq))!=name)continue;
        std::string value=trim(directive.substr(eq+1));
        if(value=="*")continue;
        if(value.size()<2 || value.front()!='(' || value.back()!=')')return false;
        if(!list_allows(value.substr(1,value.size()-2),self,target,{}))return false;
    }
    return true;
}
std::string source_origin(const JsCtx *child) {
    const char *src=child->frame_el?node_attr(child->frame_el,"src"):nullptr;
    if(!src || !*src || !strncmp(src,"about:blank",11) || !strncmp(src,"about:srcdoc",12))return child->parent->origin;
    char *resolved=url_join(child->parent->doc->url,src);std::string out=resolved?canonical_origin(resolved):std::string();free(resolved);return out;
}
bool frame_allows(const JsCtx *child,const std::string &name) {
    const JsCtx *parent=child->parent;
    const char *text=child->frame_el?node_attr(child->frame_el,"allow"):nullptr;
    bool matched=false;
    if(text){
        std::istringstream in(text);std::string directive;
        while(std::getline(in,directive,';')){
            directive=trim(directive);size_t space=directive.find_first_of(" \t\r\n");
            if(directive.substr(0,space)!=name)continue;matched=true;
            std::string list=space==std::string::npos?"'src'":trim(directive.substr(space));
            if(list.empty())list="'src'";
            if(!list_allows(list,parent->origin,child->origin,source_origin(child)))return false;
        }
    }
    return matched || js_windows_same_origin(parent,child);
}
}

bool js_capture_allowed(const JsCtx *context,const char *kind) {
    const std::string name=feature(kind);
    if(name!="camera" && name!="microphone" && name!="geolocation" && name!="display-capture")return false;
    if(!context || context->dead || context->opaque_origin || !js_windows_is_secure(context))return false;
    for(const JsCtx *c=context;c;c=c->parent){
        if(c->opaque_origin || !header_allows(c->doc->permissions_policy,name,c->origin,context->origin))return false;
        if(c->parent && !frame_allows(c,name))return false;
    }
    return true;
}
