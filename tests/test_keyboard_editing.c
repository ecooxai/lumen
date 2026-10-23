#include "../src/js/js.h"
#include "../src/net/net.h"
#include "../src/text/font.h"
#include <assert.h>

static void expect(JsCtx *c, Document *d, const char *expression) {
    char script[4096];
    snprintf(script, sizeof script, "document.documentElement.setAttribute('data-result',String(Boolean(%s)))", expression);
    node_set_attr(d->html, "data-result", "unset");
    js_eval(c, script, "keyboard:assert");
    const char *result = node_attr(d->html, "data-result");
    if (!result || strcmp(result, "true")) { fprintf(stderr, "FAILED: %s\n", expression); abort(); }
}

int main(int argc, char **argv) {
    (void)argc;
    dom_init(); net_init(1); font_init(); js_global_init(argv[0]);
    Document *d = doc_new("https://keyboard.test/");
    const char *html = "<!doctype html><html><body><div id='editor' contenteditable='true'></div></body></html>";
    html_parse(d, html, strlen(html));
    JsHost host = {0}; JsCtx *c = js_new(d, &host);
    Node *editor = doc_get_element_by_id(d, "editor");
    js_eval(c, "globalThis.editor=document.getElementById('editor');globalThis.last=null;"
        "editor.addEventListener('keydown',e=>{last=e;if(e.keyCode>=33&&e.keyCode<=46)e.preventDefault();});",
        "keyboard:setup");
    const char *keys[] = {".","'","(",")","!",",","?",";","/","-","=","[","]","\\","`","a","A","7"," "};
    const int codes[] = {190,222,57,48,49,188,191,186,191,189,187,219,221,220,192,65,65,55,32};
    for (size_t i = 0; i < ARRLEN(keys); i++) {
        assert(js_dispatch(c, editor, "keydown", "KeyboardEvent", true, true, 0, 0, 0, keys[i]));
        js_dispatch(c, editor, "lumenedit", "KeyboardEvent", false, false, 0, 0, 0, keys[i]);
        char assertion[128]; snprintf(assertion, sizeof assertion, "last.isTrusted && last.keyCode===%d && last.which===%d", codes[i], codes[i]);
        expect(c, d, assertion);
    }
    char *text = node_text_content(editor);
    /* the trailing typed space stays visible as a no-break space, as in Chrome */
    if (strcmp(text, ".'()!,?;/-=[]\\`aA7\xC2\xA0") && strcmp(text, ".'()!,?;/-=[]\\`aA7 ")) { fprintf(stderr, "FAILED: typed text is \"%s\"\n", text); abort(); }
    free(text);
    assert(!js_dispatch(c, editor, "keydown", "KeyboardEvent", true, true, 0, 0, 0, "Delete"));
    expect(c, d, "last.key==='Delete' && last.keyCode===46 && last.code==='Delete'");
    expect(c, d, "(()=>{const e=new KeyboardEvent('keydown',{key:'.'});return e.keyCode===0&&e.which===0&&e.charCode===0})()");
    expect(c, d, "(()=>{const e=new KeyboardEvent('keydown',{key:'.',keyCode:190,which:190});return e.keyCode===190&&e.which===190})()");
    js_release_pins(); js_free(c); doc_free(d); net_shutdown();
    return 0;
}
