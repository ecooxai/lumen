/* Compile the real application translation unit; do not copy its hash code.
 * Section GC discards the unused UI/main-loop code. These checks execute the
 * application's actual display-item hash, subtree hash, and clip signatures;
 * a live-window repaint remains a separate integration observation. */
#define main lumen_unused_app_main_for_radius_test
#include "../src/app/main.c"
#undef main

static unsigned radius_damage_checks, radius_damage_failures;
static void radius_damage_check(bool condition, const char *label) {
    radius_damage_checks++;
    if (!condition) {
        radius_damage_failures++;
        fprintf(stderr, "FAIL application radius damage key: %s\n", label);
    }
}
int main(void) {
    DItem a = {.op=DO_RECT, .w=100, .h=100, .color=0xffff0000u}, b;
    const float x[4] = {10,10,10,10}, y[4] = {20,20,20,20};
    ditem_set_radii(&a, x, y); b = a; b.ry[0] = 30;
    radius_damage_check(ditem_hash(&a, 0) != ditem_hash(&b, 0),
        "Y-only radius change changes the application's item hash");
    a.has_ry = false; b = a; b.ry[0] = 12345;
    radius_damage_check(ditem_hash(&a, 0) == ditem_hash(&b, 0),
        "unused optional Y bytes do not change effective geometry hash");
    b = a; b.has_ry = true; memcpy(b.ry, b.r, sizeof b.r);
    radius_damage_check(ditem_hash(&a, 0) == ditem_hash(&b, 0),
        "equivalent implicit and explicit circular axes hash identically");

    DTransform group = {0}; group.count = 1; group.items = &b;
    DItem parent = {.op=DO_TRANSFORM, .transform=&group};
    uint64_t original_subtree = ditem_hash(&parent, 0);
    b.ry[1] += 5;
    radius_damage_check(original_subtree != ditem_hash(&parent, 0),
        "transformed subtree retains Y-only child damage");

    DItem items[3] = {0};
    items[0] = (DItem){.op=DO_PUSH_CLIP, .x=20, .y=20, .w=100, .h=100};
    ditem_set_radii(&items[0], x, y);
    items[1] = (DItem){.op=DO_RECT, .x=20, .y=20, .w=100, .h=100, .color=0xffff0000u};
    items[2].op = DO_POP_CLIP;
    DisplayList list = {0}; list.items.v = items; list.items.n = 3;
    struct DSig *before = NULL, *after = NULL; int before_capacity = 0, after_capacity = 0;
    int before_count = dl_sigs(&list, &before, &before_capacity, 0);
    uint64_t original_list = dl_hash(&list);
    items[0].ry[2] += 8;
    int after_count = dl_sigs(&list, &after, &after_capacity, 0);
    radius_damage_check(before_count == 3 && after_count == 3, "actual signature builder observes all clip/items");
    radius_damage_check(before[0].h != after[0].h, "Y-only clip change changes actual clip signature");
    radius_damage_check(before[1].h != after[1].h, "Y-only ancestor clip change invalidates the descendant signature");
    radius_damage_check(original_list != dl_hash(&list), "Y-only geometry changes the actual whole-list cache key");
    radius_damage_check(!memcmp(before[1].b, after[1].b, sizeof before[1].b),
        "Y-only signature change is detected while the item's rectangular damage bounds stay identical");
    free(before); free(after);
    printf("radius actual application hash/signatures: %u/%u checks passed\n",
           radius_damage_checks - radius_damage_failures, radius_damage_checks);
    return radius_damage_failures ? 1 : 0;
}
