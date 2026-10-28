/* Deep cloning must not eagerly materialize wrappers for plain parsed trees.
 * Count actual native wrappers, independently of heap/GC timing. */
#include "../src/js/js_int.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" void font_init(void);

static int checks, failures;
#define CHECK(name, condition) do { bool ok_ = (condition); ++checks; \
    printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); if (!ok_) ++failures; } while (0)

static bool evaluate(JsCtx *c, const char *source) {
    JS_ENTER(c);
    v8::TryCatch tc(iso);
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> result;
    bool ok = v8::Script::Compile(ctx, jstr(iso, source)).ToLocal(&script) &&
              script->Run(ctx).ToLocal(&result) && result->BooleanValue(iso);
    if (!ok && tc.HasCaught()) {
        v8::String::Utf8Value error(iso, tc.Exception());
        fprintf(stderr, "clone-allocation JavaScript: %s\n", *error ? *error : "unknown error");
    }
    return ok;
}

static Node *global_node(JsCtx *c, const char *name) {
    JS_ENTER(c);
    v8::Local<v8::Value> value;
    return ctx->Global()->Get(ctx, jstr(iso, name)).ToLocal(&value) ? junwrap(value) : nullptr;
}

static size_t tree_count(Node *root, bool only_wrapped) {
    if (!root) return 0;
    size_t count = 0;
    std::vector<Node *> pending = {root};
    while (!pending.empty()) {
        Node *node = pending.back(); pending.pop_back();
        if (!only_wrapped || node->js) count++;
        for (Node *child = node->first; child; child = child->next) pending.push_back(child);
        if (node->template_content) pending.push_back(node->template_content);
    }
    return count;
}

static std::string repeated_markup(void) {
    std::string markup;
    for (int i = 0; i < 512; i++) markup += "<span data-copy='value'>text</span>";
    return markup;
}

struct Fixture {
    Document *document;
    JsCtx *context;
    explicit Fixture(const std::string &body) {
        std::string html = "<!doctype html><html><head></head><body>" + body + "</body></html>";
        document = doc_new("https://clone-allocation.example/");
        html_parse(document, html.data(), html.size());
        JsHost host{};
        context = js_new(document, &host);
    }
    ~Fixture() { js_free(context); doc_free(document); }
};

static void plain_tree(void) {
    Fixture fixture("<div id='source'>" + repeated_markup() + "</div>");
    JsCtx *c = fixture.context;
    CHECK("plain source lookup", evaluate(c, "globalThis.source=document.getElementById('source'); !!source"));
    Node *source = global_node(c, "source");
    CHECK("parsed source contains 1025 native nodes", tree_count(source, false) == 1025);
    CHECK("parsed descendants begin without wrappers", tree_count(source, true) == 1);
    size_t before = c->wrapped.size();
    CHECK("two deep clones execute", evaluate(c,
        "globalThis.copy=source.cloneNode(true); globalThis.copy2=source.cloneNode(true); true"));
    size_t growth = c->wrapped.size() - before;
    printf("WRAPPER_COUNT plain clones=2 descendants_per_tree=1024 growth=%zu\n", growth);
    CHECK("plain deep clones allocate only their root wrappers", growth == 2);
    CHECK("plain source descendants remain unwrapped", tree_count(source, true) == 1);
    Node *copy = global_node(c, "copy");
    CHECK("deep clone keeps every native descendant", tree_count(copy, false) == 1025);
    CHECK("plain clone descendants remain lazy", tree_count(copy, true) == 1);
    CHECK("cloned native attributes and text remain intact", copy && copy->first && copy->first->first &&
        node_attr(copy->first, "data-copy") && copy->first->first->text &&
        !strcmp(node_attr(copy->first, "data-copy"), "value") && !strcmp(copy->first->first->text, "text"));
}

static void template_tree(void) {
    Fixture fixture("<template id='source'><section>" + repeated_markup() + "</section></template>");
    JsCtx *c = fixture.context;
    CHECK("template source lookup", evaluate(c, "globalThis.source=document.getElementById('source'); !!source"));
    Node *source = global_node(c, "source");
    CHECK("parsed template has native content", source && source->template_content && source->template_content->first);
    CHECK("template content begins without wrappers", tree_count(source, true) == 1);
    size_t before = c->wrapped.size();
    CHECK("deep and shallow template clones execute", evaluate(c,
        "globalThis.copy=source.cloneNode(true); globalThis.shallow=source.cloneNode(false); true"));
    size_t growth = c->wrapped.size() - before;
    printf("WRAPPER_COUNT template clones=2 growth=%zu\n", growth);
    CHECK("plain template cloning allocates only root wrappers", growth == 2);
    CHECK("source template content remains unwrapped", tree_count(source, true) == 1);
    Node *copy = global_node(c, "copy"), *shallow = global_node(c, "shallow");
    CHECK("deep template native contents are complete", tree_count(copy, false) == tree_count(source, false));
    CHECK("deep template descendants remain lazy", tree_count(copy, true) == 1);
    CHECK("shallow template content remains empty", shallow && shallow->template_content && !shallow->template_content->first);
}

static void empty_template(void) {
    Fixture fixture("");
    JsCtx *c = fixture.context;
    CHECK("unopened empty template creation", evaluate(c, "globalThis.source=document.createElement('template'); true"));
    Node *source = global_node(c, "source");
    CHECK("empty template content is initially lazy", source && !source->template_content);
    size_t before = c->wrapped.size();
    CHECK("unopened empty template clone executes", evaluate(c, "globalThis.copy=source.cloneNode(true); true"));
    Node *copy = global_node(c, "copy");
    CHECK("metadata inspection does not create template content", source && copy && !source->template_content && !copy->template_content);
    CHECK("unopened template cloning adds only its root wrapper", c->wrapped.size() - before == 1);
}

static void sparse_metadata(void) {
    Fixture fixture("<div id='source'>" + repeated_markup() + "</div>");
    JsCtx *c = fixture.context;
    CHECK("sparse namespace metadata setup", evaluate(c,
        "globalThis.source=document.getElementById('source');"
        "globalThis.special=document.createElementNS('urn:allocation','q:Leaf');"
        "globalThis.attribute=document.createAttributeNS('urn:attribute','a:key'); attribute.value='kept';"
        "special.setAttributeNodeNS(attribute); source.appendChild(special); true"));
    Node *source = global_node(c, "source");
    size_t before = c->wrapped.size(), wrapped_source = tree_count(source, true);
    CHECK("sparse metadata clone executes", evaluate(c, "globalThis.copy=source.cloneNode(true); true"));
    size_t growth = c->wrapped.size() - before;
    printf("WRAPPER_COUNT sparse metadata_nodes=1 growth=%zu\n", growth);
    CHECK("only root and metadata-bearing clone nodes are wrapped", growth == 2);
    CHECK("unrelated source nodes remain unwrapped", tree_count(source, true) == wrapped_source);
    CHECK("qualified name and Attr metadata remain observable", evaluate(c,
        "copy.lastChild.namespaceURI==='urn:allocation' && copy.lastChild.prefix==='q' &&"
        "copy.lastChild.localName==='Leaf' && copy.lastChild.ownerDocument===document &&"
        "copy.lastChild.getAttributeNode('a:key').namespaceURI==='urn:attribute' &&"
        "copy.lastChild.getAttributeNode('a:key').value==='kept' &&"
        "copy.lastChild.getAttributeNode('a:key').ownerElement===copy.lastChild"));
}

static void poisoned_array_prototype(void) {
    Fixture fixture("");
    JsCtx *c = fixture.context;
    CHECK("array-prototype regression source setup", evaluate(c,
        "globalThis.source=document.createElementNS('urn:poison','q:Leaf'); true"));
    CHECK("numeric Array.prototype setter cannot intercept clone metadata", evaluate(c,
        "(() => { const descriptor=Object.getOwnPropertyDescriptor(Array.prototype,'0');"
        "try { Object.defineProperty(Array.prototype,'0',{configurable:true,set(){throw new Error('internal array escaped')}});"
        "const copy=source.cloneNode(true);"
        "return copy.namespaceURI==='urn:poison' && copy.prefix==='q' && copy.localName==='Leaf';"
        "} finally { if(descriptor) Object.defineProperty(Array.prototype,'0',descriptor); else delete Array.prototype[0]; } })()"));
}

int main(int argc, char **argv) {
    (void)argc;
    dom_init(); net_init(2); font_init(); js_global_init(argv[0]);
    plain_tree(); template_tree(); empty_template(); sparse_metadata(); poisoned_array_prototype();
    net_shutdown();
    printf("RESULTS clone-metadata-allocation %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
