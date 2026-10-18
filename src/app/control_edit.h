/* Default text-control actions shared by SDL input and the native regression.
 * The host revalidates focus after script callbacks; callbacks never own n. */
#ifndef LUMEN_APP_CONTROL_EDIT_H
#define LUMEN_APP_CONTROL_EDIT_H
#include "../dom/control.h"

typedef struct {
    void *context;
    bool (*active)(void *context, Node *n);
    bool (*input)(void *context, Node *n, const char *type, const char *input_type, const char *data);
    void (*selection)(void *context, Node *n);
} AppControlHost;
typedef enum { APP_CONTROL_INSERT, APP_CONTROL_BACKSPACE, APP_CONTROL_DELETE } AppControlEdit;

static inline bool app_control_active(const AppControlHost *host, Node *n) {
    return control_user_selectable(n) && (!host->active || host->active(host->context, n));
}
static inline bool app_control_selection_diff(ControlSelection a, ControlSelection b) {
    return a.start != b.start || a.end != b.end || a.direction != b.direction;
}
static inline void app_control_selection_notify(const AppControlHost *host, Node *n, ControlSelection before) {
    if (host->selection && app_control_selection_diff(before, control_selection(n))) host->selection(host->context, n);
}
static inline bool app_control_edit(const AppControlHost *host, Node *n, AppControlEdit edit,
                                    const char *text, const char *input_type) {
    if (!app_control_active(host, n) || !control_user_editable(n)) return false;
    node_retain(n); /* beforeinput may remove the element or move focus */
    const char *data = edit == APP_CONTROL_INSERT ? text : NULL;
    if (input_type && (!strcmp(input_type, "insertLineBreak") || !strcmp(input_type, "insertParagraph"))) data = NULL;
    bool allowed = !host->input || host->input(host->context, n, "beforeinput", input_type, data);
    bool changed = false;
    if (allowed && app_control_active(host, n) && control_user_editable(n)) {
        ControlSelection before = control_selection(n); /* callbacks may change the range/value */
        changed = edit == APP_CONTROL_INSERT ? control_insert_utf8(n, text ? text : "") : control_delete(n, edit == APP_CONTROL_BACKSPACE);
        if (changed) {
            app_control_selection_notify(host, n, before);
            if (host->input) host->input(host->context, n, "input", input_type, data);
        }
    }
    node_release(n);
    return changed;
}
static inline bool app_control_move(const AppControlHost *host, Node *n, ControlMove move, bool extend) {
    if (!app_control_active(host, n)) return false;
    node_retain(n);
    bool changed = control_move(n, move, extend);
    if (changed && host->selection) host->selection(host->context, n);
    node_release(n);
    return changed;
}
static inline bool app_control_select(const AppControlHost *host, Node *n, uint32_t start, uint32_t end, ControlDirection direction) {
    if (!app_control_active(host, n)) return false;
    node_retain(n);
    bool changed = control_set_selection(n, start, end, direction);
    if (changed && host->selection) host->selection(host->context, n);
    node_release(n);
    return changed;
}
#endif
