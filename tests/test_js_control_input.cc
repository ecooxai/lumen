/* Native trusted input bridge and reentrant DOM changes, without SDL or a site. */
#include "../src/js/js_int.h"
#include "../src/dom/control.h"
#include <cstdio>
#include <cstring>
extern "C" void font_init(void);
static int checks, failures;
#define CHECK(name, condition) do { ++checks; if (!(condition)) { ++failures; fprintf(stderr, "FAIL %s\n", name); } } while (0)
static bool evaluate(JsCtx *c, const char *source) {
    JS_ENTER(c); v8::TryCatch caught(iso); v8::Local<v8::Script> script; v8::Local<v8::Value> value;
    return v8::Script::Compile(ctx, jstr(iso, source)).ToLocal(&script) && script->Run(ctx).ToLocal(&value) && value->BooleanValue(iso);
}
int main(int argc, char **argv) {
    (void)argc; dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    Document *d = doc_new("https://input.test/"); const char *html = "<!doctype html><html><body><textarea id='field'></textarea><input id='other'></body></html>";
    html_parse(d, html, strlen(html)); JsHost host{}; JsCtx *c = js_new(d, &host);
    Node *field = doc_get_element_by_id(d, "field"), *other = doc_get_element_by_id(d, "other");
    CHECK("install reentrant input handlers", evaluate(c,
        "globalThis.field=document.getElementById('field'); globalThis.other=document.getElementById('other');"
        "globalThis.mode=''; globalThis.seen=[]; field.value='ab'; field.setSelectionRange(1,1); field.focus();"
        "function record(e){seen.push({type:e.type,inputType:e.inputType,data:e.data,trusted:e.isTrusted,bubbles:e.bubbles,cancelable:e.cancelable,composed:e.composed,tag:e.target.tagName});}"
        "field.addEventListener('beforeinput',e=>{record(e);"
        "if(mode==='cancel')e.preventDefault();"
        "else if(mode==='range')field.setSelectionRange(0,1);"
        "else if(mode==='value'){field.value='abcd';field.setSelectionRange(2,3);}"
        "else if(mode==='readonly')field.readOnly=true;"
        "else if(mode==='focus')other.focus();"
        "else if(mode==='detach'){field.remove();globalThis.field=null;}});"
        "field.addEventListener('input',e=>{record(e);e.preventDefault();}); true"));
    CHECK("beforeinput returns cancellation", evaluate(c, "mode='cancel';seen=[];true") && !js_dispatch_input(c, field, "beforeinput", "insertText", "X"));
    CHECK("cancelled beforeinput has trusted exact metadata", evaluate(c, "seen.length===1 && seen[0].type==='beforeinput' && seen[0].inputType==='insertText' && seen[0].data==='X' && seen[0].trusted && seen[0].bubbles && seen[0].cancelable && seen[0].composed && field.value==='ab' && field.selectionStart===1"));
    CHECK("input notification cannot be cancelled", js_dispatch_input(c, field, "input", "insertText", "X"));
    CHECK("input notification carries noncancelable metadata", evaluate(c, "seen.length===2 && seen[1].type==='input' && seen[1].inputType==='insertText' && seen[1].data==='X' && seen[1].trusted && !seen[1].cancelable && seen[1].composed"));
    CHECK("deletion beforeinput carries null data", evaluate(c, "mode='';seen=[];true") && js_dispatch_input(c, field, "beforeinput", "deleteContentBackward", nullptr) && evaluate(c, "seen[0].inputType==='deleteContentBackward' && seen[0].data===null"));
    CHECK("beforeinput handler can replace current selection", evaluate(c, "mode='range';field.value='ab';field.setSelectionRange(1,1);true") && js_dispatch_input(c, field, "beforeinput", "insertText", "X") && control_insert_utf8(field, "X") && evaluate(c, "field.value==='Xb' && field.selectionStart===1 && field.selectionEnd===1"));
    CHECK("beforeinput handler can replace value and selection", evaluate(c, "mode='value';true") && js_dispatch_input(c, field, "beforeinput", "insertText", "X") && control_insert_utf8(field, "X") && evaluate(c, "field.value==='abXd' && field.selectionStart===3"));
    CHECK("readonly changed in beforeinput prevents native mutation", evaluate(c, "mode='readonly';field.value='abc';true") && js_dispatch_input(c, field, "beforeinput", "insertText", "X") && !control_insert_utf8(field, "X") && evaluate(c, "field.value==='abc'"));
    CHECK("focus changes are observable before the native default action", evaluate(c, "mode='focus';field.readOnly=false;field.focus();true") && js_dispatch_input(c, field, "beforeinput", "insertText", "X") && d->focus == other && evaluate(c, "document.activeElement===other"));
    CHECK("detachment clears native focus while target stays pinned", evaluate(c, "mode='detach';field.focus();true") && js_dispatch_input(c, field, "beforeinput", "insertText", "X") && !field->parent && d->focus != field && field->refcount > 0);
    { JS_ENTER(c); iso->LowMemoryNotification(); }
    CHECK("detached target survives callback-triggered collection until unpin", !field->parent && field->doc == d && field->refcount > 0);
    js_release_pins(); /* field may be destroyed here; no subsequent use */
    js_free(c); doc_free(d); net_shutdown();
    printf("RESULTS native-control-input %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
