/* Text-control value and selection state, shared by DOM/JS and native editing.
 * State is allocated only for input/textarea in Node.ext; indices are UTF-16.
 * UTF-8 strings returned here are owned by the caller. Setter inputs are copied.
 */
#ifndef LUMEN_CONTROL_H
#define LUMEN_CONTROL_H
#include "dom.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { CONTROL_DIR_NONE, CONTROL_DIR_FORWARD, CONTROL_DIR_BACKWARD } ControlDirection;
typedef struct { uint32_t start, end; ControlDirection direction; } ControlSelection;
typedef enum { CONTROL_PRESERVE, CONTROL_SELECT, CONTROL_START, CONTROL_END } ControlSelectionMode;
typedef enum { CONTROL_LEFT, CONTROL_RIGHT, CONTROL_HOME, CONTROL_END_OF_VALUE } ControlMove;

bool control_is_text(const Node *n);
bool control_supports_selection(const Node *n);
bool control_user_selectable(const Node *n); /* readonly is selectable; disabled is not */
bool control_user_editable(const Node *n); /* includes readonly and disabled fieldsets */
const uint16_t *control_value_utf16(Node *n, size_t *length); /* borrowed until next mutation */
char *control_value_utf8(Node *n);
void control_set_value_utf16(Node *n, const uint16_t *value, size_t length);
void control_set_value_utf8(Node *n, const char *value); /* NULL restores the default value */
ControlSelection control_selection(Node *n);
bool control_set_selection(Node *n, uint32_t start, uint32_t end, ControlDirection direction);
bool control_replace_range_utf16(Node *n, const uint16_t *replacement, size_t length,
                                uint32_t start, uint32_t end, ControlSelectionMode mode);
bool control_insert_utf8(Node *n, const char *text); /* native user edit: active selection, collapse at end */
bool control_delete(Node *n, bool backward);         /* native user edit: selection or adjacent code point */
bool control_move(Node *n, ControlMove move, bool extend);
size_t control_byte_offset(Node *n, uint32_t utf16_offset);
uint32_t control_utf16_offset(Node *n, size_t byte_offset);
void control_clone_state(Node *to, const Node *from); /* clone value/dirty state, fresh selection */
unsigned control_type_snapshot(const Node *n); /* DOM attribute hooks; no allocation */
void control_type_changed(Node *n, unsigned previous);

#ifdef __cplusplus
}
#endif
#endif
