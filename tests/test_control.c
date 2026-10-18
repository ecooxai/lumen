#include "../src/dom/control.h"
#include <stdio.h>

static int checks, failures, foreign_freed;
#define CHECK(name, condition) do { ++checks; if (!(condition)) { ++failures; fprintf(stderr, "FAIL %s\n", name); } } while (0)
static bool selection(Node *n, uint32_t start, uint32_t end, ControlDirection direction) {
    ControlSelection s = control_selection(n); return s.start == start && s.end == end && s.direction == direction;
}
static bool value(Node *n, const char *expected) {
    char *text = control_value_utf8(n); bool same = !strcmp(text, expected); free(text); return same;
}
static Node *field(Document *d, const char *tag, const char *type, const char *text) {
    Node *n = node_new_element(d, tag, NS_HTML); if (type) node_set_attr(n, "type", type);
    node_append(&d->node, n); if (text) control_set_value_utf8(n, text); return n;
}
static void foreign_free(void *p) { foreign_freed++; free(p); }
int main(void) {
    dom_init(); Document *d = doc_new("https://control.test/");
    Node *plain = node_new_element(d, "div", NS_HTML); node_append(&d->node, plain);
    CHECK("unrelated DOM nodes allocate no control state", !control_is_text(plain) && plain->ext == NULL);
    Node *n = field(d, "textarea", NULL, NULL);
    node_set_text_content(n, "default");
    CHECK("initial default caret starts at zero", selection(n, 0, 0, CONTROL_DIR_NONE));
    CHECK("initial default value is visible", value(n, "default") && n->value_override == NULL);
    control_set_value_utf8(n, "ab");
    CHECK("changed value resets caret", selection(n, 2, 2, CONTROL_DIR_NONE));
    CHECK("ArrowLeft changes caret", control_move(n, CONTROL_LEFT, false) && selection(n, 1, 1, CONTROL_DIR_NONE));
    CHECK("native insertion uses actual caret", control_insert_utf8(n, "X") && value(n, "aXb") && selection(n, 2, 2, CONTROL_DIR_NONE));
    CHECK("Backspace deletes before caret", control_delete(n, true) && value(n, "ab") && selection(n, 1, 1, CONTROL_DIR_NONE));
    CHECK("Delete removes after caret", control_delete(n, false) && value(n, "a") && selection(n, 1, 1, CONTROL_DIR_NONE));
    CHECK("Delete at end is unchanged", !control_delete(n, false));
    control_set_value_utf8(n, "abcdef"); control_set_selection(n, 2, 5, CONTROL_DIR_BACKWARD);
    CHECK("selection replacement is bounded", control_insert_utf8(n, "Z") && value(n, "abZf") && selection(n, 3, 3, CONTROL_DIR_NONE));
    control_set_value_utf8(n, "abcdef"); control_set_selection(n, 2, 5, CONTROL_DIR_BACKWARD);
    size_t length; const uint16_t *same = control_value_utf16(n, &length);
    control_set_value_utf8(n, "abcdef");
    CHECK("same-value assignment preserves native caret and backing store", selection(n, 2, 5, CONTROL_DIR_BACKWARD) && control_value_utf16(n, &length) == same);
    CHECK("left collapses an existing range", control_move(n, CONTROL_LEFT, false) && selection(n, 2, 2, CONTROL_DIR_NONE));
    CHECK("shift-left creates a backward range", control_move(n, CONTROL_LEFT, true) && selection(n, 1, 2, CONTROL_DIR_BACKWARD));
    CHECK("shift-right contracts toward anchor", control_move(n, CONTROL_RIGHT, true) && selection(n, 2, 2, CONTROL_DIR_FORWARD));
    CHECK("shift-right crosses the anchor", control_move(n, CONTROL_RIGHT, true) && selection(n, 2, 3, CONTROL_DIR_FORWARD));
    CHECK("home and end move to value boundaries", control_move(n, CONTROL_HOME, false) && selection(n, 0, 0, CONTROL_DIR_NONE) && control_move(n, CONTROL_END_OF_VALUE, false) && selection(n, 6, 6, CONTROL_DIR_NONE));

    control_set_value_utf8(n, "A\xf0\x9f\x98\x80" "B");
    CHECK("UTF8 emoji has four UTF16 units with surrounding letters", selection(n, 4, 4, CONTROL_DIR_NONE));
    CHECK("UTF16-to-byte offset preserves multibyte boundaries", control_byte_offset(n, 0) == 0 && control_byte_offset(n, 1) == 1 && control_byte_offset(n, 2) == 1 && control_byte_offset(n, 3) == 5 && control_byte_offset(n, 4) == 6);
    CHECK("byte-to-UTF16 offset preserves multibyte boundaries", control_utf16_offset(n, 0) == 0 && control_utf16_offset(n, 1) == 1 && control_utf16_offset(n, 3) == 1 && control_utf16_offset(n, 5) == 3 && control_utf16_offset(n, 6) == 4);
    control_move(n, CONTROL_LEFT, false);
    CHECK("left before emoji reaches its trailing edge", selection(n, 3, 3, CONTROL_DIR_NONE));
    CHECK("left skips a complete surrogate pair", control_move(n, CONTROL_LEFT, false) && selection(n, 1, 1, CONTROL_DIR_NONE));
    CHECK("right skips a complete surrogate pair", control_move(n, CONTROL_RIGHT, false) && selection(n, 3, 3, CONTROL_DIR_NONE));
    CHECK("Backspace does not leave half an emoji", control_delete(n, true) && value(n, "AB") && selection(n, 1, 1, CONTROL_DIR_NONE));
    control_set_value_utf8(n, "A\xf0\x9f\x98\x80" "B"); control_set_selection(n, 1, 1, CONTROL_DIR_NONE);
    CHECK("forward Delete removes a complete emoji", control_delete(n, false) && value(n, "AB"));
    uint16_t unusual[] = {'A', 0, 0xd800, 'B'};
    control_set_value_utf16(n, unusual, ARRLEN(unusual));
    const uint16_t *exact = control_value_utf16(n, &length);
    CHECK("canonical DOM value retains NUL and lone surrogate", length == 4 && !memcmp(exact, unusual, sizeof unusual));
    CHECK("native projection remains valid terminated UTF8", value(n, "A\xef\xbf\xbd\xef\xbf\xbd" "B") && strlen(n->value_override) == 8);
    CHECK("projection offsets account for replacement glyphs", control_byte_offset(n, 3) == 7 && control_utf16_offset(n, 7) == 3);
    Node *clone = node_clone(n, true, d); node_append(&d->node, clone);
    control_set_value_utf8(n, "other"); exact = control_value_utf16(clone, &length);
    CHECK("clone has independent exact UTF16 state", length == 4 && !memcmp(exact, unusual, sizeof unusual) && selection(clone, 0, 0, CONTROL_DIR_NONE));

    Node *email = field(d, "input", "email", "ab@example.test");
    CHECK("email keeps native edit state but no public selection API", control_is_text(email) && control_user_editable(email) && !control_supports_selection(email));
    control_set_selection(email, 1, 1, CONTROL_DIR_NONE);
    CHECK("email edits in the middle", control_insert_utf8(email, "X") && value(email, "aXb@example.test") && selection(email, 2, 2, CONTROL_DIR_NONE));
    Node *number = field(d, "input", "number", "123"); control_set_selection(number, 1, 1, CONTROL_DIR_NONE);
    CHECK("number keeps a native caret independent of public API", !control_supports_selection(number) && control_insert_utf8(number, "4") && value(number, "1423"));
    node_set_attr(email, "type", "text");
    CHECK("newly supported selection type starts at zero", control_supports_selection(email) && selection(email, 0, 0, CONTROL_DIR_NONE));
    control_set_value_utf8(email, "  ab@example.test  "); control_set_selection(email, 2, 4, CONTROL_DIR_BACKWARD);
    node_set_attr(email, "type", "email");
    CHECK("type change reapplies value sanitization", value(email, "ab@example.test"));
    node_remove_attr(email, "type");
    CHECK("removing email type restores default text selection", control_supports_selection(email) && selection(email, 0, 0, CONTROL_DIR_NONE));
    node_set_attr(email, "type", "password"); control_set_selection(email, 2, 4, CONTROL_DIR_BACKWARD);
    node_set_attr(email, "type", "text");
    CHECK("two selectable types preserve existing selection", selection(email, 2, 4, CONTROL_DIR_BACKWARD));
    Node *file = field(d, "input", "text", "temporary"); node_set_attr(file, "type", "file");
    Node *file_copy = node_clone(file, true, d); node_append(&d->node, file_copy);
    CHECK("filename transition discards the old text projection", file->value_override == NULL && file_copy->value_override == NULL);
    node_set_attr(file_copy, "type", "text");
    CHECK("filename clone returns to its own empty default text value", value(file_copy, "") && selection(file_copy, 0, 0, CONTROL_DIR_NONE));

    control_set_value_utf8(n, "abc"); control_set_selection(n, 1, 2, CONTROL_DIR_NONE); node_set_attr(n, "readonly", "");
    CHECK("readonly blocks native insertion and deletion", !control_user_editable(n) && !control_insert_utf8(n, "X") && !control_delete(n, false) && value(n, "abc"));
    CHECK("readonly still permits caret movement", control_move(n, CONTROL_LEFT, false) && selection(n, 1, 1, CONTROL_DIR_NONE));
    uint16_t replacement = 'X';
    CHECK("readonly permits script range replacement", control_replace_range_utf16(n, &replacement, 1, 1, 2, CONTROL_END) && value(n, "aXc"));
    node_remove_attr(n, "readonly"); node_set_attr(n, "disabled", "");
    CHECK("disabled blocks native editing and movement", !control_insert_utf8(n, "X") && !control_delete(n, true) && !control_move(n, CONTROL_HOME, false));
    node_remove_attr(n, "disabled");
    Node *fieldset = node_new_element(d, "fieldset", NS_HTML); node_set_attr(fieldset, "disabled", ""); node_append(&d->node, fieldset);
    Node *legend = node_new_element(d, "legend", NS_HTML); node_append(fieldset, legend);
    Node *allowed = node_new_element(d, "input", NS_HTML); node_append(legend, allowed);
    Node *blocked = node_new_element(d, "textarea", NS_HTML); node_append(fieldset, blocked);
    CHECK("first disabled-fieldset legend remains editable", control_user_editable(allowed) && control_insert_utf8(allowed, "ok"));
    CHECK("other disabled-fieldset controls reject edits", !control_user_editable(blocked) && !control_insert_utf8(blocked, "bad"));
    Node *legend2 = node_new_element(d, "legend", NS_HTML); node_append(fieldset, legend2);
    Node *blocked2 = node_new_element(d, "input", NS_HTML); node_append(legend2, blocked2);
    CHECK("second legend is not a disabled-fieldset exception", !control_user_editable(blocked2));

    control_set_value_utf8(n, NULL); control_set_selection(n, 1, 6, CONTROL_DIR_BACKWARD);
    node_set_text_content(n, "xy");
    CHECK("reset value tracks default content and clamps selection", value(n, "xy") && selection(n, 1, 2, CONTROL_DIR_BACKWARD));
    control_set_value_utf8(n, "xy"); node_set_text_content(n, "changed default");
    CHECK("same-value assignment still sets dirty-value flag", value(n, "xy"));
    control_set_value_utf8(n, NULL); control_set_selection(n, 1, 2, CONTROL_DIR_NONE);
    CHECK("invalid range sets dirty flag before returning", !control_replace_range_utf16(n, &replacement, 1, 9, 2, CONTROL_END) && n->value_override != NULL);
    node_set_text_content(n, "later default"); CHECK("invalid range leaves frozen prior value", value(n, "changed default"));

    Node *foreign = field(d, "textarea", NULL, NULL); foreign->ext = xmalloc(1); foreign->ext_free = foreign_free;
    CHECK("control state never overwrites another extension owner", !control_set_selection(foreign, 0, 0, CONTROL_DIR_NONE) && foreign->ext_free == foreign_free);
    doc_free(d); CHECK("foreign extension is destroyed once", foreign_freed == 1);
    extern long g_nodes_live;
    CHECK("document and clone node ownership balances", g_nodes_live == 0);
    for (int i = 0; i < 200; i++) {
        Document *document = doc_new("about:blank"); Node *control = field(document, "textarea", NULL, "A\xf0\x9f\x98\x80" "B");
        for (int j = 0; j < 20; j++) { control_set_selection(control, 1, 3, CONTROL_DIR_BACKWARD); control_insert_utf8(control, "\xf0\x9f\x98\x80"); }
        Node *copy = node_clone(control, true, document); node_append(&document->node, copy); doc_free(document);
    }
    CHECK("repeated replacement/clone/destruction balances nodes", g_nodes_live == 0);
    printf("RESULTS control-core %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    return failures ? 1 : 0;
}
