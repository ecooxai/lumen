/* Real V8/prelude context construction, with independent per-context DOM
 * capability/prototype state. This is a native regression, not a WPT score. */
#include "js/js.h"
#include "layout/layout.h"
#include "net/net.h"
#include "base/util.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#error "Assertions must remain enabled"
#endif
static void viewport(void *ud,float *w,float *h,float *sx,float *sy,float *dpr) {
    (void)ud;*w=800;*h=600;*sx=*sy=0;*dpr=1;
}
static void check(JsCtx *c,const char *script,const char *label) {
    bool failed=false;char *r=js_eval_result(c,script,"context-isolation-regression",&failed);
    if(failed||!r||strcmp(r,"true")){fprintf(stderr,"FAIL %s: failed=%d result=%s\n",label,failed,r?r:"<undefined>");abort();}free(r);
}
int main(int argc,char **argv) {
    int count=argc>1?atoi(argv[1]):32;assert(count>0&&count<=160);
    dom_init();font_init();net_init(2);js_global_init(argv[0]);
    double all=now_ms();size_t checks=0;
    for(int round=0;round<3;round++) {
        Document *doc=doc_new("https://context-isolation.test/root");assert(doc);
        const char *markup="<!doctype html><html><body></body></html>";html_parse(doc,markup,strlen(markup));
        JsHost host={0};host.viewport=viewport;double start=now_ms();JsCtx *root=js_new(doc,&host);assert(root);
        check(root,"typeof Object.prototype.__lumenContextTag === 'undefined' && typeof HTMLElement.prototype.__lumenContextTag === 'undefined'","new-root-clean");checks++;
        check(root,"(()=>{Object.prototype.__lumenContextTag='root';HTMLElement.prototype.__lumenContextTag='root';globalThis.__lumenContextTag='root';return document.body.ownerDocument===document})()","root-setup");checks++;
        for(int i=0;i<count;i++) {
            char script[1536],id[64];snprintf(id,sizeof id,"frame-%d",i);
            snprintf(script,sizeof script,"(()=>{const f=document.createElement('iframe');f.id='%s';document.body.appendChild(f);return true})()",id);
            check(root,script,"insert-blank-child");checks++;
            Node *frame=doc_get_element_by_id(doc,id);assert(frame);JsCtx *child=js_frame_ctx(frame);assert(child);
            snprintf(script,sizeof script,"(()=>{if(typeof Object.prototype.__lumenContextTag!=='undefined'||typeof HTMLElement.prototype.__lumenContextTag!=='undefined'||typeof globalThis.__lumenContextTag!=='undefined')return false;Object.prototype.__lumenContextTag=%d;HTMLElement.prototype.__lumenContextTag=%d;globalThis.__lumenContextTag=%d;const e=document.createElement('div');e.id='owned';e.textContent='child-%d';document.body.appendChild(e);return e.ownerDocument===document && Object.getPrototypeOf(e)===HTMLDivElement.prototype && e.__lumenContextTag===%d && document.getElementById('owned')===e})()",i,i,i,i,i);
            check(child,script,"child-private-state-and-document");checks++;
            js_release_pins();
        }
        for(int i=0;i<count;i++) {
            char script[1024],id[64];snprintf(id,sizeof id,"frame-%d",i);JsCtx *child=js_frame_ctx(doc_get_element_by_id(doc,id));assert(child);
            snprintf(script,sizeof script,"globalThis.__lumenContextTag===%d && Object.prototype.__lumenContextTag===%d && HTMLElement.prototype.__lumenContextTag===%d && document.getElementById('owned').textContent==='child-%d'",i,i,i,i);
            check(child,script,"earlier-child-state-retained");checks++;
        }
        check(root,"globalThis.__lumenContextTag==='root' && Object.prototype.__lumenContextTag==='root' && HTMLElement.prototype.__lumenContextTag==='root' && document.getElementById('owned')===null","parent-state-isolated");checks++;
        size_t heap=0,external=0;js_mem_stats(&heap,&external);double built=now_ms();js_release_pins();js_free(root);doc_free(doc);js_release_pins();double end=now_ms();
        fprintf(stderr,"CONTEXT_METRIC {\"round\":%d,\"children\":%d,\"construct_check_ms\":%.3f,\"teardown_ms\":%.3f,\"heap_bytes\":%zu,\"external_bytes\":%zu}\n",round,count,built-start,end-built,heap,external);
    }
    net_shutdown();fprintf(stderr,"context-isolation: %zu strict checks PASS; elapsed_ms=%.3f\n",checks,now_ms()-all);return 0;
}
