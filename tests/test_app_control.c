/* The same default-edit functions that SDL text/key handlers call. No injected
 * OS input, no network, and no substitute text-editor implementation. */
#include "../src/app/control_edit.h"
#include <stdio.h>
extern long g_nodes_live;

static int checks, failures;
#define CHECK(expr) do { checks++; if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); failures++; } } while (0)
typedef struct {
    Document *doc; Node *field, *other;
    AppControlHost host;
    int before, input, selection, mode;
    char type[40], data[80]; bool null_data, changed_page;
} Fixture;
static bool active(void *context, Node *n) {
    Fixture *f = context;
    return !f->changed_page && n->doc == f->doc && f->doc->focus == n && (n->flags & NF_CONNECTED);
}
static bool event(void *context, Node *n, const char *type, const char *input_type, const char *data) {
    Fixture *f = context;
    snprintf(f->type, sizeof f->type, "%s", input_type);
    f->null_data = !data; snprintf(f->data, sizeof f->data, "%s", data ? data : "");
    if (!strcmp(type, "input")) { f->input++; return true; }
    CHECK(!strcmp(type, "beforeinput")); f->before++;
    if (f->mode == 1) return false;
    if (f->mode == 2) f->doc->focus = f->other;
    if (f->mode == 3) { node_remove(n); node_free_tree(n); } /* edit helper's pin must survive removal */
    if (f->mode == 4) node_set_attr(n, "readonly", "");
    if (f->mode == 5) node_set_attr(n, "disabled", "");
    if (f->mode == 6) { control_set_value_utf8(n, "cd"); control_set_selection(n, 0, 1, CONTROL_DIR_NONE); }
    if (f->mode == 7) f->changed_page = true;
    if (f->mode == 8) node_set_attr(n, "type", "checkbox");
    return true;
}
static void selection(void *context, Node *n) { Fixture *f = context; CHECK(n == f->field); f->selection++; }
static void init(Fixture *f, const char *tag, const char *type, const char *value) {
    memset(f, 0, sizeof *f); f->doc = doc_new("https://native-control.test/");
    f->field = node_new_element(f->doc, tag, NS_HTML); node_append(&f->doc->node, f->field); node_retain(f->field);
    f->other = node_new_element(f->doc, "input", NS_HTML); node_append(&f->doc->node, f->other);
    if (type) node_set_attr(f->field, "type", type);
    control_set_value_utf8(f->field, value); f->doc->focus = f->field;
    f->host = (AppControlHost){f, active, event, selection};
}
static bool value_is(Fixture *f, const char *expected) { char *s = control_value_utf8(f->field); bool same = !strcmp(s, expected); free(s); return same; }
static void finish(Fixture *f) { node_remove(f->field); node_release(f->field); doc_free(f->doc); }
static void ordinary_edit(void) {
    Fixture f; init(&f, "textarea", NULL, "");
    CHECK(app_control_edit(&f.host, f.field, APP_CONTROL_INSERT, "ab", "insertText"));
    CHECK(value_is(&f, "ab")); CHECK(f.before == 1 && f.input == 1 && !f.null_data && !strcmp(f.data, "ab"));
    CHECK(app_control_move(&f.host, f.field, CONTROL_LEFT, false));
    CHECK(control_selection(f.field).start == 1 && control_selection(f.field).end == 1);
    CHECK(app_control_edit(&f.host, f.field, APP_CONTROL_INSERT, "X", "insertText"));
    CHECK(value_is(&f, "aXb")); CHECK(control_selection(f.field).start == 2);
    CHECK(app_control_edit(&f.host, f.field, APP_CONTROL_DELETE, NULL, "deleteContentForward"));
    CHECK(value_is(&f, "aX")); CHECK(f.null_data && !strcmp(f.type, "deleteContentForward"));
    CHECK(app_control_edit(&f.host, f.field, APP_CONTROL_BACKSPACE, NULL, "deleteContentBackward"));
    CHECK(value_is(&f, "a"));
    CHECK(app_control_select(&f.host, f.field, 0, UINT32_MAX, CONTROL_DIR_NONE));
    CHECK(app_control_edit(&f.host, f.field, APP_CONTROL_INSERT, "lumen ipad 123", "insertFromPaste"));
    CHECK(value_is(&f, "lumen ipad 123"));
    CHECK(app_control_select(&f.host, f.field, 0, UINT32_MAX, CONTROL_DIR_NONE));
    CHECK(app_control_edit(&f.host, f.field, APP_CONTROL_BACKSPACE, NULL, "deleteContentBackward"));
    CHECK(value_is(&f, ""));
    int input = f.input;
    CHECK(!app_control_edit(&f.host, f.field, APP_CONTROL_BACKSPACE, NULL, "deleteContentBackward"));
    CHECK(f.input == input); /* no input event for a boundary no-op */
    CHECK(app_control_edit(&f.host, f.field, APP_CONTROL_INSERT, "one\ntwo", "insertFromPaste"));
    CHECK(value_is(&f, "one\ntwo"));
    CHECK(app_control_move(&f.host, f.field, CONTROL_LEFT, true));
    CHECK(app_control_edit(&f.host, f.field, APP_CONTROL_INSERT, "X", "insertText"));
    CHECK(value_is(&f, "one\ntwX"));
    CHECK(app_control_edit(&f.host, f.field, APP_CONTROL_INSERT, "\n", "insertLineBreak"));
    CHECK(value_is(&f, "one\ntwX\n")); CHECK(f.null_data); CHECK(!strcmp(f.type, "insertLineBreak"));
    finish(&f);
}
static void unicode_and_email(void) {
    Fixture f; init(&f, "input", "text", "a\xf0\x9f\x99\x82" "b");
    CHECK(app_control_move(&f.host, f.field, CONTROL_LEFT, false));
    CHECK(control_selection(f.field).start == 3); /* emoji is two UTF-16 units */
    CHECK(app_control_edit(&f.host, f.field, APP_CONTROL_BACKSPACE, NULL, "deleteContentBackward"));
    CHECK(value_is(&f, "ab")); CHECK(control_selection(f.field).start == 1);
    finish(&f);
    init(&f, "input", "email", "ab@example.test");
    CHECK(!control_supports_selection(f.field)); CHECK(control_user_selectable(f.field));
    CHECK(app_control_select(&f.host, f.field, 1, 1, CONTROL_DIR_NONE));
    CHECK(app_control_edit(&f.host, f.field, APP_CONTROL_INSERT, "X", "insertText"));
    CHECK(value_is(&f, "aXb@example.test"));
    node_set_attr(f.field, "readonly", "");
    CHECK(app_control_move(&f.host, f.field, CONTROL_LEFT, false));
    int before = f.before;
    CHECK(!app_control_edit(&f.host, f.field, APP_CONTROL_INSERT, "!", "insertText")); CHECK(f.before == before);
    node_set_attr(f.field, "disabled", "");
    CHECK(!app_control_move(&f.host, f.field, CONTROL_RIGHT, false));
    CHECK(!app_control_select(&f.host, f.field, 0, 1, CONTROL_DIR_NONE));
    finish(&f);
}
static void callbacks(void) {
    for (int mode = 1; mode <= 8; mode++) {
        Fixture f; init(&f, "input", "text", "ab"); f.mode = mode;
        bool changed = app_control_edit(&f.host, f.field, APP_CONTROL_INSERT, "X", "insertText");
        CHECK(changed == (mode == 6)); CHECK(f.before == 1); CHECK(f.input == (mode == 6));
        if (mode != 8) CHECK(value_is(&f, mode == 6 ? "Xd" : "ab"));
        if (mode == 3) CHECK(f.field->refcount == 1 && !(f.field->flags & NF_CONNECTED));
        finish(&f);
    }
}
int main(void) {
    dom_init(); long live = g_nodes_live;
    ordinary_edit(); unicode_and_email(); callbacks();
    CHECK(g_nodes_live == live);
    printf("RESULTS app-control %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
