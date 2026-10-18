#ifndef LUMEN_TOUCH_CONTACTS_H
#define LUMEN_TOUCH_CONTACTS_H
#include <stdbool.h>
#include <stdint.h>

/* An interrupted or multi-finger sequence cannot become a tap/fling when one
   finger is released. It stays blocked until all tracked contacts are lifted. */
typedef struct {
    uint64_t ids[16], primary;
    int n;
    bool blocked;
} TouchContacts;

static inline void touch_contacts_cancel(TouchContacts *c) { *c = (TouchContacts){0}; }

/* True only for the first contact of a new gesture. */
static inline bool touch_contacts_down(TouchContacts *c, uint64_t id) {
    for (int i = 0; i < c->n; i++) if (c->ids[i] == id) return false;
    if (c->n == (int)(sizeof c->ids / sizeof c->ids[0])) { c->blocked = true; return false; }
    bool first = c->n == 0;
    if (first) { c->primary = id; c->blocked = false; }
    else c->blocked = true;
    c->ids[c->n++] = id;
    return first;
}

/* True only when the sole primary contact completed an uninterrupted gesture. */
static inline bool touch_contacts_up(TouchContacts *c, uint64_t id) {
    for (int i = 0; i < c->n; i++) if (c->ids[i] == id) {
        bool complete = c->n == 1 && !c->blocked && id == c->primary;
        c->ids[i] = c->ids[--c->n];
        if (!c->n) touch_contacts_cancel(c);
        return complete;
    }
    return false;
}
#endif
