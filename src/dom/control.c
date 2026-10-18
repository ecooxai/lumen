#include "control.h"

typedef struct {
    uint16_t *value;
    size_t length;
    ControlSelection selection;
    const char *projection; /* non-owning alias of Node.value_override */
    uint64_t version;
    bool initialized;
} ControlState;

static void control_free(void *data) { ControlState *s = data; free(s->value); free(s); }
static bool is_input(const Node *n) { return n && n->type == NODE_ELEMENT && n->ns == NS_HTML && n->tag == A_input; }
static bool is_textarea(const Node *n) { return n && n->type == NODE_ELEMENT && n->ns == NS_HTML && n->tag == A_textarea; }
static const char *input_type(const Node *n) {
    const char *type = node_attr(n, "type");
    if (!type || !*type) return "text";
    static const char *const types[] = {"hidden","text","search","tel","url","email","password","date","month","week","time","datetime-local","number","range","color","checkbox","radio","file","submit","image","reset","button"};
    for (size_t i = 0; i < ARRLEN(types); i++) if (str_ieq(type, types[i])) return types[i];
    return "text"; /* invalid-value default */
}
bool control_supports_selection(const Node *n) {
    if (is_textarea(n)) return true;
    if (!is_input(n)) return false;
    const char *type = input_type(n);
    return !strcmp(type, "text") || !strcmp(type, "search") || !strcmp(type, "url") || !strcmp(type, "tel") || !strcmp(type, "password");
}
bool control_is_text(const Node *n) {
    return control_supports_selection(n) || (is_input(n) && (!strcmp(input_type(n), "email") || !strcmp(input_type(n), "number")));
}
static bool control_disabled(const Node *n) {
    if ((n->flags & NF_DISABLED) || node_has_attr(n, "disabled")) return true;
    for (const Node *p = n->parent; p; p = p->parent) {
        if (!node_is(p, A_fieldset) || !node_has_attr(p, "disabled")) continue;
        const Node *legend = p->first;
        while (legend && !node_is(legend, A_legend)) legend = legend->next;
        if (!legend || !node_is_inclusive_ancestor(legend, n)) return true;
    }
    return false;
}
bool control_user_selectable(const Node *n) { return control_is_text(n) && !control_disabled(n); }
bool control_user_editable(const Node *n) { return control_user_selectable(n) && !node_has_attr(n, "readonly"); }

static uint16_t *from_utf8(const char *text, size_t *length) {
    size_t bytes = strlen(text), used = 0;
    uint16_t *out = xmalloc((bytes + 1) * sizeof *out);
    for (size_t i = 0; i < bytes;) {
        uint32_t cp; i += (size_t)utf8_decode(text + i, bytes - i, &cp);
        if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) cp = 0xfffd;
        if (cp > 0xffff) { cp -= 0x10000; out[used++] = (uint16_t)(0xd800 + (cp >> 10)); out[used++] = (uint16_t)(0xdc00 + (cp & 1023)); }
        else out[used++] = (uint16_t)cp;
    }
    out[used] = 0; *length = used; return out;
}
static uint32_t projected_cp(const uint16_t *value, size_t length, size_t *index) {
    uint32_t cp = value[(*index)++];
    if (cp >= 0xd800 && cp <= 0xdbff && *index < length && value[*index] >= 0xdc00 && value[*index] <= 0xdfff)
        cp = 0x10000 + ((cp - 0xd800) << 10) + (value[(*index)++] - 0xdc00);
    /* The DOM retains NUL/lone surrogates exactly; the NUL-terminated UTF-8
       paint/native projection uses a replacement glyph for those code units. */
    if (!cp || (cp >= 0xd800 && cp <= 0xdfff)) cp = 0xfffd;
    return cp;
}
static char *to_utf8(const uint16_t *value, size_t length) {
    SB out; sb_init(&out);
    for (size_t i = 0; i < length;) sb_utf8(&out, projected_cp(value, length, &i));
    return sb_take(&out);
}
static uint16_t *normalize(const Node *n, const uint16_t *value, size_t length, size_t *out_length) {
    uint16_t *out = xmalloc((length + 1) * sizeof *out); size_t used = 0;
    bool textarea = is_textarea(n);
    for (size_t i = 0; i < length; i++) {
        uint16_t cp = value[i];
        if (cp == '\r') { if (textarea) { out[used++] = '\n'; if (i + 1 < length && value[i + 1] == '\n') i++; } }
        else if (cp != '\n' || textarea) out[used++] = cp;
    }
    if (is_input(n) && (!strcmp(input_type(n), "url") || !strcmp(input_type(n), "email"))) {
        size_t first = 0, last = used;
        while (first < last && (out[first] == 9 || out[first] == 10 || out[first] == 12 || out[first] == 13 || out[first] == 32)) first++;
        while (last > first && (out[last - 1] == 9 || out[last - 1] == 10 || out[last - 1] == 12 || out[last - 1] == 13 || out[last - 1] == 32)) last--;
        used = last - first; if (first) memmove(out, out + first, used * sizeof *out);
    }
    out[used] = 0; *out_length = used; return out;
}
static bool same_value(const ControlState *s, const uint16_t *value, size_t length) {
    return s->length == length && (!length || !memcmp(s->value, value, length * sizeof *value));
}
static void clamp_selection(ControlState *s) {
    if (s->selection.end > s->length) s->selection.end = (uint32_t)s->length;
    if (s->selection.start > s->selection.end) s->selection.start = s->selection.end;
}
static ControlState *state(Node *n) {
    if (!control_is_text(n)) return NULL;
    ControlState *s = n->ext_free == control_free ? n->ext : NULL;
    if (!s) {
        if (n->ext) return NULL; /* another owner must never be overwritten */
        s = xcalloc(1, sizeof *s); n->ext = s; n->ext_free = control_free;
    }
    uint64_t version = n->doc ? n->doc->dom_version : 0;
    if (!s->initialized || s->projection != n->value_override || (!n->value_override && s->version != version)) {
        char *owned = NULL; const char *raw = n->value_override;
        if (!raw) raw = is_textarea(n) ? (owned = node_text_content(n)) : node_attr(n, "value");
        size_t length, normalized; uint16_t *value = from_utf8(raw ? raw : "", &length);
        uint16_t *clean = normalize(n, value, length, &normalized); free(value); free(owned);
        free(s->value); s->value = clean; s->length = normalized;
        s->projection = n->value_override; s->version = version; s->initialized = true;
        clamp_selection(s);
    }
    return s;
}
const uint16_t *control_value_utf16(Node *n, size_t *length) {
    static const uint16_t empty[1]; ControlState *s = state(n);
    *length = s ? s->length : 0; return s ? s->value : empty;
}
char *control_value_utf8(Node *n) { ControlState *s = state(n); return s ? to_utf8(s->value, s->length) : xstrdup(""); }
ControlSelection control_selection(Node *n) { ControlState *s = state(n); return s ? s->selection : (ControlSelection){0}; }
static bool select_state(ControlState *s, uint32_t start, uint32_t end, ControlDirection direction) {
    if (end > s->length) end = (uint32_t)s->length;
    if (start > end) start = end;
    if (direction != CONTROL_DIR_FORWARD && direction != CONTROL_DIR_BACKWARD) direction = CONTROL_DIR_NONE;
    bool changed = start != s->selection.start || end != s->selection.end || direction != s->selection.direction;
    s->selection = (ControlSelection){start, end, direction}; return changed;
}
bool control_set_selection(Node *n, uint32_t start, uint32_t end, ControlDirection direction) {
    ControlState *s = state(n); if (!s) return false;
    bool changed = select_state(s, start, end, direction);
    if (changed) doc_mark_style_dirty(n->doc, n);
    return changed;
}
static void publish(Node *n, ControlState *s) {
    char *projection = to_utf8(s->value, s->length);
    free(n->value_override); n->value_override = projection; s->projection = projection;
}
void control_set_value_utf16(Node *n, const uint16_t *value, size_t length) {
    ControlState *s = state(n); if (!s || length > UINT32_MAX) return;
    if (n->value_override && same_value(s, value, length)) return;
    size_t used; uint16_t *clean = normalize(n, value, length, &used);
    bool changed = !same_value(s, clean, used);
    free(s->value); s->value = clean; s->length = used;
    if (changed) s->selection = (ControlSelection){(uint32_t)used, (uint32_t)used, CONTROL_DIR_NONE};
    publish(n, s); /* setting even the same default value sets the dirty-value flag */
    if (changed) doc_mark_dirty(n->doc, n);
}
void control_set_value_utf8(Node *n, const char *value) {
    if (!value) {
        ControlState *s = state(n); if (!s) return;
        free(n->value_override); n->value_override = NULL; s->initialized = false;
        (void)state(n); doc_mark_dirty(n->doc, n); return;
    }
    size_t length; uint16_t *text = from_utf8(value, &length);
    control_set_value_utf16(n, text, length); free(text);
}
bool control_replace_range_utf16(Node *n, const uint16_t *replacement, size_t length, uint32_t start, uint32_t end, ControlSelectionMode mode) {
    ControlState *s = state(n); if (!s || length > UINT32_MAX) return false;
    if (!n->value_override) publish(n, s); /* dirty flag precedes the IndexSizeError check */
    if (start > end) return false;
    if (start > s->length) start = (uint32_t)s->length;
    if (end > s->length) end = (uint32_t)s->length;
    size_t total = s->length - (end - start) + length; if (total > UINT32_MAX) return false;
    uint16_t *text = xmalloc((total + 1) * sizeof *text);
    memcpy(text, s->value, start * sizeof *text);
    if (length) memcpy(text + start, replacement, length * sizeof *text);
    memcpy(text + start + length, s->value + end, (s->length - end) * sizeof *text);
    text[total] = 0;
    size_t used; uint16_t *clean = normalize(n, text, total, &used); free(text);
    bool changed = !same_value(s, clean, used);
    ControlSelection old = s->selection;
    free(s->value); s->value = clean; s->length = used; publish(n, s);
    uint32_t new_end = start + (uint32_t)length, a = start, b = new_end;
    if (mode == CONTROL_START) b = a;
    else if (mode == CONTROL_END) a = b;
    else if (mode == CONTROL_PRESERVE) {
        int64_t delta = (int64_t)length - (end - start);
        a = old.start > end ? (uint32_t)((int64_t)old.start + delta) : old.start > start ? start : old.start;
        b = old.end > end ? (uint32_t)((int64_t)old.end + delta) : old.end > start ? new_end : old.end;
    }
    bool selection_changed = select_state(s, a, b, CONTROL_DIR_NONE);
    if (changed) doc_mark_dirty(n->doc, n);
    else if (selection_changed) doc_mark_style_dirty(n->doc, n);
    return changed || selection_changed;
}
bool control_insert_utf8(Node *n, const char *text) {
    if (!control_user_editable(n) || !text || !*text) return false;
    ControlSelection selection = control_selection(n); size_t length; uint16_t *value = from_utf8(text, &length);
    bool changed = control_replace_range_utf16(n, value, length, selection.start, selection.end, CONTROL_END);
    free(value); return changed;
}
static uint32_t previous_point(const ControlState *s, uint32_t position) {
    if (!position) return 0;
    position--;
    if (position && s->value[position] >= 0xdc00 && s->value[position] <= 0xdfff && s->value[position - 1] >= 0xd800 && s->value[position - 1] <= 0xdbff) position--;
    return position;
}
static uint32_t next_point(const ControlState *s, uint32_t position) {
    if (position >= s->length) return (uint32_t)s->length;
    uint16_t first = s->value[position++];
    if (first >= 0xd800 && first <= 0xdbff && position < s->length && s->value[position] >= 0xdc00 && s->value[position] <= 0xdfff) position++;
    return position;
}
bool control_delete(Node *n, bool backward) {
    if (!control_user_editable(n)) return false;
    ControlState *s = state(n); if (!s) return false;
    uint32_t start = s->selection.start, end = s->selection.end;
    if (start == end) { if (backward) start = previous_point(s, start); else end = next_point(s, end); }
    if (start == end) return false;
    return control_replace_range_utf16(n, NULL, 0, start, end, CONTROL_END);
}
bool control_move(Node *n, ControlMove move, bool extend) {
    ControlState *s = state(n); if (!s || control_disabled(n)) return false;
    ControlSelection selection = s->selection;
    uint32_t anchor = selection.direction == CONTROL_DIR_BACKWARD ? selection.end : selection.start;
    uint32_t focus = selection.direction == CONTROL_DIR_BACKWARD ? selection.start : selection.end;
    if (move == CONTROL_HOME) focus = 0;
    else if (move == CONTROL_END_OF_VALUE) focus = (uint32_t)s->length;
    else if (!extend && selection.start != selection.end) focus = move == CONTROL_LEFT ? selection.start : selection.end;
    else focus = move == CONTROL_LEFT ? previous_point(s, focus) : next_point(s, focus);
    if (!extend) return control_set_selection(n, focus, focus, CONTROL_DIR_NONE);
    return control_set_selection(n, LMIN(anchor, focus), LMAX(anchor, focus), focus < anchor ? CONTROL_DIR_BACKWARD : CONTROL_DIR_FORWARD);
}
size_t control_byte_offset(Node *n, uint32_t offset) {
    ControlState *s = state(n); if (!s) return 0;
    size_t bytes = 0;
    for (size_t i = 0; i < s->length && i < offset;) {
        size_t next = i; uint32_t cp = projected_cp(s->value, s->length, &next);
        if (next > offset) break; /* an interior surrogate position uses the glyph's start */
        char encoded[4]; bytes += (size_t)utf8_encode(cp, encoded); i = next;
    }
    return bytes;
}
uint32_t control_utf16_offset(Node *n, size_t offset) {
    ControlState *s = state(n); if (!s) return 0;
    size_t bytes = 0, i = 0;
    while (i < s->length) {
        size_t next = i; uint32_t cp = projected_cp(s->value, s->length, &next); char encoded[4];
        size_t size = (size_t)utf8_encode(cp, encoded);
        if (offset < bytes + size) break;
        bytes += size; i = next;
    }
    return (uint32_t)i;
}
void control_clone_state(Node *to, const Node *from) {
    if (to == from || !from->value_override) return;
    const ControlState *s = from->ext_free == control_free ? from->ext : NULL;
    if (s && control_is_text(to)) {
        control_set_value_utf16(to, s->value, s->length);
        ControlState *copy = state(to); if (copy) copy->selection = (ControlSelection){0};
    } else { free(to->value_override); to->value_override = xstrdup(from->value_override); }
}

enum { TYPE_SELECTABLE = 1, TYPE_VALUE = 2, TYPE_FILENAME = 4, TYPE_DEFAULT = 8 };
unsigned control_type_snapshot(const Node *n) {
    if (!is_input(n)) return 0;
    const char *type = input_type(n);
    unsigned flags = control_supports_selection(n) ? TYPE_SELECTABLE : 0;
    if (!strcmp(type, "file")) return flags | TYPE_FILENAME;
    if (!strcmp(type, "hidden") || !strcmp(type, "checkbox") || !strcmp(type, "radio") || !strcmp(type, "button") || !strcmp(type, "submit") || !strcmp(type, "image") || !strcmp(type, "reset")) return flags | TYPE_DEFAULT;
    return flags | TYPE_VALUE;
}
void control_type_changed(Node *n, unsigned previous) {
    if (!is_input(n)) return;
    unsigned current = control_type_snapshot(n);
    ControlState *s = n->ext_free == control_free ? n->ext : NULL;
    if ((previous & TYPE_VALUE) && (current & TYPE_DEFAULT)) {
        char *value = s && s->initialized && s->projection == n->value_override ? to_utf8(s->value, s->length) : xstrdup(n->value_override ? n->value_override : node_attr(n, "value") ? node_attr(n, "value") : "");
        if (*value) node_set_attr(n, "value", value);
        free(value); free(n->value_override); n->value_override = NULL;
        if (s) s->initialized = false;
    } else if (!(previous & TYPE_VALUE) && (current & TYPE_VALUE)) {
        free(n->value_override); n->value_override = NULL;
        if (s) s->initialized = false;
    } else if (!(previous & TYPE_FILENAME) && (current & TYPE_FILENAME)) {
        free(n->value_override); n->value_override = NULL;
        if (s) { free(s->value); s->value = NULL; s->length = 0; s->selection = (ControlSelection){0}; s->initialized = false; }
    }
    if (s && control_is_text(n)) {
        if (s->initialized && s->projection == n->value_override) {
            size_t length; uint16_t *clean = normalize(n, s->value, s->length, &length);
            free(s->value); s->value = clean; s->length = length; clamp_selection(s);
            if (n->value_override) publish(n, s);
        } else s = state(n);
        if (s && !(previous & TYPE_SELECTABLE) && (current & TYPE_SELECTABLE)) s->selection = (ControlSelection){0};
    }
}
