#pragma once
#include "../layout/layout.h"
#include <math.h>

static float android_focus_delta(float start, float end, float low, float high) {
    if (high <= low || (start < low && end > high)) return 0;
    if (start < low) return end - start <= high - low ? start - low : end - high;
    if (end > high) return end - start <= high - low ? end - high : start - low;
    return 0;
}

static bool android_focus_scroll(Layout *layout, Box *focus, float *scroll) {
    if (!layout || !layout->root || !focus || !scroll || !box_within(focus, layout->root) ||
            !isfinite(*scroll) || !isfinite(layout->vh) || layout->vh <= 0 ||
            !isfinite(layout->doc_h) || !isfinite(focus->x) || !isfinite(focus->y) ||
            !isfinite(focus->w) || !isfinite(focus->h) || focus->w <= 0 || focus->h <= 0) return false;
    for (Box *current = focus; current; current = current->abs && current->cb ? current->cb : current->parent) {
        if (!isfinite(current->x) || !isfinite(current->y) || !isfinite(current->w) ||
                !isfinite(current->h)) return false;
        if (current->st && current->st->has_transform) {
            const ComputedStyle *style = current->st;
            if (style->transform[0] != 1 || style->transform[1] != 0 ||
                    style->transform[2] != 0 || style->transform[3] != 1 ||
                    !isfinite(style->transform[4]) || !isfinite(style->transform[5]) ||
                    !isfinite(style->translate_pending[0].pct) ||
                    !isfinite(style->translate_pending[1].pct)) return false;
        }
        if (current->scroller && current->node && (!isfinite(current->node->scroll_x) ||
                !isfinite(current->node->scroll_y) || !isfinite(current->scroll_w) ||
                !isfinite(current->scroll_h))) return false;
        if (current->fixed) break;
    }
    float left = focus->x, top = focus->y, right = left + focus->w, bottom = top + focus->h;
    bool changed = false;
    for (Box *current = focus; current; ) {
        const ComputedStyle *style = current->st;
        if (style && style->has_transform) {
            float shift_x = style->transform[4] + style->translate_pending[0].pct * current->w / 100;
            float shift_y = style->transform[5] + style->translate_pending[1].pct * current->h / 100;
            left += shift_x; right += shift_x; top += shift_y; bottom += shift_y;
        }
        if (current->fixed) return changed;
        Box *ancestor = current->abs && current->cb ? current->cb : current->parent;
        if (ancestor && ancestor->scroller && ancestor->node && ancestor->st &&
                (ancestor->st->overflow_x != OV_VISIBLE || ancestor->st->overflow_y != OV_VISIBLE)) {
            Node *node = ancestor->node;
            float old_x = node->scroll_x, old_y = node->scroll_y;
            float clip_left = ancestor->x + ancestor->b[3], clip_top = ancestor->y + ancestor->b[0];
            float clip_right = ancestor->x + ancestor->w - ancestor->b[1];
            float clip_bottom = ancestor->y + ancestor->h - ancestor->b[2];
            float inset_x = LMIN(8, LMAX(0, clip_right - clip_left) / 4);
            float inset_y = LMIN(8, LMAX(0, clip_bottom - clip_top) / 4);
            float effective_y = box_scroll_y(ancestor);
            if (ancestor->st->overflow_x != OV_VISIBLE && ancestor->st->overflow_x != OV_CLIP) {
                float delta = android_focus_delta(left - old_x, right - old_x,
                    clip_left + inset_x, clip_right - inset_x);
                node->scroll_x = LCLAMP(old_x + delta, 0, LMAX(0, ancestor->scroll_w - ancestor->w));
            }
            if (ancestor->st->overflow_y != OV_VISIBLE && ancestor->st->overflow_y != OV_CLIP) {
                float delta = android_focus_delta(top - effective_y, bottom - effective_y,
                    clip_top + inset_y, clip_bottom - inset_y);
                float maximum = LMAX(0, ancestor->scroll_h - ancestor->h);
                node->scroll_y = box_scroll_from_end(ancestor) ? LCLAMP(old_y + delta, -maximum, 0)
                    : LCLAMP(old_y + delta, 0, maximum);
            }
            changed |= node->scroll_x != old_x || node->scroll_y != old_y;
            left -= node->scroll_x; right -= node->scroll_x;
            effective_y = box_scroll_y(ancestor);
            top -= effective_y; bottom -= effective_y;
        }
        current = ancestor;
    }
    float inset = LMIN(8, layout->vh / 4);
    float delta = android_focus_delta(top - *scroll, bottom - *scroll, inset, layout->vh - inset);
    float next = LCLAMP(*scroll + delta, 0, LMAX(0, layout->doc_h - layout->vh));
    changed |= next != *scroll;
    *scroll = next;
    return changed;
}
