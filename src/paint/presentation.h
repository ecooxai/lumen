#ifndef LUMEN_PAINT_PRESENTATION_H
#define LUMEN_PAINT_PRESENTATION_H
#include <stdint.h>
#include <stdlib.h>

/* Actual video frames admitted to a retained compositor surface. This tracks
 * frame submission, not the visibility of individual pixels after later
 * clipping, resampling, overlays, window occlusion or display scanout. A set
 * has at most one frame per owner; there is no playback history or pixel map. */
typedef struct { uint64_t owner, frame; } PaintFrame;
typedef struct { PaintFrame *v; int n, cap; } PaintFrames;

static inline void paint_frames_add(PaintFrames *p, PaintFrame frame) {
    if (!frame.owner || !frame.frame) return;
    for (int i = 0; i < p->n; i++) if (p->v[i].owner == frame.owner) {
        if (frame.frame > p->v[i].frame) p->v[i] = frame;
        return;
    }
    if (p->n == p->cap) {
        int cap = p->cap ? p->cap * 2 : 4;
        PaintFrame *v = realloc(p->v, sizeof *v * (size_t)cap);
        if (!v) abort();
        p->v = v; p->cap = cap;
    }
    p->v[p->n++] = frame;
}
static inline void paint_frames_merge(PaintFrames *p, const PaintFrames *source) {
    if (p == source) return;
    for (int i = 0; i < source->n; i++) paint_frames_add(p, source->v[i]);
}
static inline void paint_frames_free(PaintFrames *p) {
    free(p->v); *p = (PaintFrames){0};
}
#endif
