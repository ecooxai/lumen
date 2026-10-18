#include "control.h"
#include <float.h>
#include <math.h>

static const Box *control_text_box(const Box *b) {
    for (const Box *t = b->first; t; t = t->next) if (t->kind == BX_TEXT) return t;
    return NULL;
}
/* Match the layout's prep_text projection without allocating on a caret blink.
 * Password bullets, expanded tabs and collapsed spaces do not have the same
 * byte offsets as the editable value. Text-transform only changes ASCII case. */
size_t control_paint_byte(const Box *b, uint32_t index) {
    if (!b || !b->node || b->placeholder) return 0;
    size_t length, out = 0; const uint16_t *value = control_value_utf16(b->node, &length);
    const char *type = node_attr(b->node, "type");
    bool password = b->ctl == CTL_TEXT && type && str_ieq(type, "password");
    int ws = b->st->white_space;
    bool collapse = ws == WS_NORMAL || ws == WS_NOWRAP || ws == WS_PRE_LINE;
    bool newline = ws == WS_PRE || ws == WS_PRE_WRAP || ws == WS_PRE_LINE || ws == WS_BREAK_SPACES;
    bool space = false;
    for (size_t i = 0; i < length && i < index;) {
        uint32_t cp = value[i++];
        if (cp >= 0xd800 && cp <= 0xdbff && i < length && value[i] >= 0xdc00 && value[i] <= 0xdfff) {
            if (i >= index) break; /* an interior surrogate offset paints at its glyph's start */
            cp = 0x10000 + ((cp - 0xd800) << 10) + (value[i++] - 0xdc00);
        }
        if (!cp || (cp >= 0xd800 && cp <= 0xdfff)) cp = 0xfffd;
        if (password) { out += 3; continue; }
        if (cp == '\r') continue;
        if (cp == '\n' && newline) { out++; space = true; continue; }
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\f') {
            bool before_newline = false;
            if (ws == WS_PRE_LINE) {
                size_t next = i;
                while (next < length && (value[next] == ' ' || value[next] == '\t' || value[next] == '\r' || value[next] == '\f')) next++;
                before_newline = next < length && value[next] == '\n';
            }
            if (collapse) { if (!space && !before_newline) out++; space = true; }
            else out += cp == '\t' ? 4 : 1;
            continue;
        }
        space = false;
        out += cp && cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    }
    const Box *t = control_text_box(b);
    return t ? LMIN(out, (size_t)t->text_len) : 0;
}
uint32_t control_paint_index(const Box *b, size_t byte) {
    if (!b || !b->node || b->placeholder) return 0;
    size_t length; const uint16_t *value = control_value_utf16(b->node, &length);
    uint32_t lo = 0, hi = (uint32_t)length;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (control_paint_byte(b, mid) < byte) lo = mid + 1; else hi = mid;
    }
    if (lo) {
        uint32_t previous = lo - 1;
        if (previous && value[previous] >= 0xdc00 && value[previous] <= 0xdfff && value[previous - 1] >= 0xd800 && value[previous - 1] <= 0xdbff) previous--;
        size_t before = control_paint_byte(b, previous), after = control_paint_byte(b, lo);
        if (byte >= before && after >= byte && byte - before < after - byte) return previous;
    }
    return lo;
}
static bool text_frag(const TextFrag *f) { return f->box->kind == BX_TEXT && f->g0 < f->g1; }
static size_t frag_end(const TextFrag *f) {
    const Box *t = f->box;
    return f->g1 < t->sh.n ? t->sh.g[f->g1].cluster : (size_t)t->text_len;
}
static float frag_caret_x(const TextFrag *f, size_t byte) {
    const Box *t = f->box;
    float x = f->x + f->w;
    for (int i = f->g0; i < f->g1; i++) {
        const GlyphPos *g = &t->sh.g[i];
        if (g->cluster >= byte) return f->x + g->x - t->sh.g[f->g0].x;
    }
    return x;
}
static void empty_caret(const Box *b, ControlCaret *caret) {
    *caret = (ControlCaret){b->x + b->b[3] + b->p[3], b->y + b->b[0] + b->p[0], b->st->font_size * 1.2f};
    float height = b->h - b->b[0] - b->b[2] - b->p[0] - b->p[2];
    if (b->ctl == CTL_TEXT && height > caret->h) caret->y += (height - caret->h) / 2;
}
bool control_paint_caret(const Box *b, uint32_t index, ControlCaret *caret) {
    if (!b || !b->node || !control_is_text(b->node)) return false;
    empty_caret(b, caret);
    if (b->placeholder) {
        if (b->nfrags) { const TextFrag *f = &b->frags[0]; caret->x = f->x; caret->y = f->y; caret->h = f->h; }
        return true;
    }
    const Box *t = control_text_box(b);
    if (!t || !b->nlines) return true;
    size_t byte = control_paint_byte(b, index), cursor = 0;
    for (int li = 0; li < b->nlines; li++) {
        const Line *line = &b->lines[li]; const TextFrag *first = NULL, *last = NULL;
        for (int i = line->f0; i < line->f0 + line->nf; i++) if (text_frag(&b->frags[i])) { if (!first) first = &b->frags[i]; last = &b->frags[i]; }
        size_t end = last ? frag_end(last) : cursor;
        size_t next = end + (end < (size_t)t->text_len && t->text[end] == '\n' ? 1 : 0);
        bool trailing_newline = next > end;
        if (byte < next || (byte == end && trailing_newline) || (li == b->nlines - 1 && !trailing_newline)) {
            caret->y = first ? first->y : line->y; caret->h = first ? first->h : line->h;
            if (first) {
                caret->x = first->x;
                for (int i = line->f0; i < line->f0 + line->nf; i++) {
                    const TextFrag *f = &b->frags[i]; if (!text_frag(f)) continue;
                    caret->x = frag_caret_x(f, byte);
                    if (byte < frag_end(f)) break;
                }
            }
            return true;
        }
        cursor = next;
        caret->y = line->y + line->h; caret->h = line->h;
    }
    /* The inline layout does not emit a final empty line after a terminal LF. */
    return true;
}
uint32_t control_paint_hit(const Box *b, float x, float y) {
    if (!b || !b->node || b->placeholder || !b->nlines) return 0;
    int chosen = 0; float distance = FLT_MAX;
    for (int li = 0; li < b->nlines; li++) {
        const Line *line = &b->lines[li];
        float d = y < line->y ? line->y - y : y > line->y + line->h ? y - line->y - line->h : 0;
        if (d < distance) { distance = d; chosen = li; }
    }
    const Box *t = control_text_box(b);
    const Line *line = &b->lines[chosen]; size_t byte = 0, end = 0;
    float best = FLT_MAX;
    for (int i = 0; i < line->f0 + line->nf; i++) if (text_frag(&b->frags[i])) end = frag_end(&b->frags[i]);
    if (chosen == b->nlines - 1 && t && end < (size_t)t->text_len && t->text[end] == '\n' && y >= line->y + line->h)
        return control_paint_index(b, (size_t)t->text_len);
    if (!line->nf) {
        size_t preceding = 0;
        for (int li = 0; li < chosen; li++) {
            const Line *p = &b->lines[li];
            for (int i = p->f0; i < p->f0 + p->nf; i++) if (text_frag(&b->frags[i])) preceding = frag_end(&b->frags[i]);
            if (t && preceding < (size_t)t->text_len && t->text[preceding] == '\n') preceding++;
        }
        return control_paint_index(b, preceding);
    }
    for (int i = line->f0; i < line->f0 + line->nf; i++) {
        const TextFrag *f = &b->frags[i]; if (!text_frag(f)) continue;
        const Box *text = f->box;
        for (int g = f->g0; g <= f->g1; g++) {
            float px = g == f->g1 ? f->x + f->w : f->x + text->sh.g[g].x - text->sh.g[f->g0].x;
            float d = fabsf(x - px);
            if (d < best) { best = d; byte = g == f->g1 ? frag_end(f) : text->sh.g[g].cluster; }
        }
    }
    return control_paint_index(b, byte);
}
