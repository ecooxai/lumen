#include "../src/js/js_int.h"
extern "C" {
#include "../src/text/font.h"
extern long g_nodes_live;
}
#include <cassert>
#include <algorithm>
#include <unistd.h>

static void check(JsCtx *c, const char *code) {
    JS_ENTER(c);
    v8::TryCatch caught(iso); v8::Local<v8::Script> script; v8::Local<v8::Value> value;
    bool ok = v8::Script::Compile(ctx, jstr(iso, code)).ToLocal(&script) && script->Run(ctx).ToLocal(&value) && value->BooleanValue(iso);
    if (!ok) fprintf(stderr, "lifetime check failed: %s\n%s\n", code, caught.HasCaught() ? jcstr(iso,caught.Exception()).c_str() : "false");
    assert(ok);
}
struct ReleaseProbe { Node *surviving_parent_node; Document *owner; unsigned *released; };
static void released(void *raw) {
    auto *p = static_cast<ReleaseProbe *>(raw);
    /* A parent-realm wrapper can still refer to this adopted document root.
       The retiring context tree must clear every wrapper before freeing it. */
    assert(p->surviving_parent_node->js == nullptr);
    assert(p->owner && p->owner->url && !strcmp(p->owner->url,"about:blank"));
    ++*p->released; delete p;
}
int main(int argc,char **argv) {
    (void)argc; alarm(30);
    dom_init(); net_init(1); font_init(); js_global_init(argv[0]);
    long before = g_nodes_live;
    Document *d = doc_new("https://lifetime.test/");
    const char *html = "<!doctype html><html><body><div id=sentinel></div><iframe id=a></iframe><iframe id=b></iframe></body></html>";
    html_parse(d,html,strlen(html)); JsHost host{}; JsCtx *c=js_new(d,&host);
    js_tick(c);
    check(c,R"JS((()=>{
      const a=document.getElementById('a').contentDocument,b=document.getElementById('b').contentDocument;
      a.open();a.write('<!doctype html><html><head></head><body><div id=old>old</div></body></html>');a.close();
      a.body.innerHTML='<span data-value="detached">value</span>';
      globalThis.oldNode=a.body.firstChild;a.body.innerHTML='';
      const xml=new DOMParser().parseFromString('<html xmlns="http://www.w3.org/1999/xhtml" data-owner="parent"><head/><body><div id="adopted" data-value="original"/></body></html>','text/xml');
      const original=xml.documentElement,clone=original.cloneNode(true);
      a.replaceChild(original,a.documentElement);b.replaceChild(clone,b.documentElement);
      globalThis.heldOriginal=original;globalThis.heldClone=clone;globalThis.sentinel=document.getElementById('sentinel');
      clone.setAttribute('data-owner','clone');
      return original.ownerDocument===a && clone.ownerDocument===b && original.getAttribute('data-owner')==='parent' && clone.getAttribute('data-owner')==='clone' && oldNode.ownerDocument===a && oldNode.textContent==='value';
    })())JS");
    Node *sentinel=doc_get_element_by_id(d,"sentinel"); assert(sentinel && sentinel->js);
    unsigned count=0;
    for(const char *id:{"a","b"}) {
        JsCtx *child=js_frame_ctx(doc_get_element_by_id(d,id)); assert(child);
        Node *root=child->doc->node.first; while(root && root->type!=NODE_ELEMENT) root=root->next; assert(root);
        assert(std::find(c->wrapped.begin(),c->wrapped.end(),root)!=c->wrapped.end());
        assert(root->js && root->doc==child->doc && !root->ext);
        root->ext=new ReleaseProbe{sentinel,child->doc,&count};root->ext_free=released;
    }
    JsCtx *retiring=js_frame_ctx(doc_get_element_by_id(d,"a")); assert(retiring && retiring->xref);
    if (argc > 1 && !strcmp(argv[1],"child")) {
        js_free(retiring);   /* shared wrappers keep this inactive child until its tab retires */
        assert(retiring->dead && count==0);
        check(c,"heldOriginal.ownerDocument===oldNode.ownerDocument && heldOriginal.getAttribute('data-owner')==='parent' && oldNode.textContent==='value'");
    }
    js_release_pins();js_free(c);assert(count==2);
    doc_free(d);assert(g_nodes_live==before);net_shutdown();
    puts("cross-realm node and ownerDocument lifetime PASS; all nodes released");
}
