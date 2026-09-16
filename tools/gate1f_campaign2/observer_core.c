#include <limits.h>
#include "observer_core.h"

void c2_choice_init(C2ChoiceCursor *c) {
    if (!c) return;
    c->index = 0;
    c->selected = 0;
}

void c2_choice_move(C2ChoiceCursor *c, int direction, int item_count) {
    if (!c || item_count <= 0 || direction == 0) return;
    if (!c->selected) {
        c->index = direction > 0 ? 0 : item_count - 1;
        c->selected = 1;
        return;
    }
    if (direction > 0)
        c->index = (c->index + 1) % item_count;
    else
        c->index = (c->index + item_count - 1) % item_count;
}

int c2_choice_try_commit(const C2ChoiceCursor *c,
                         uint32_t toggle_count,
                         int probe_state,
                         uint32_t min_toggles,
                         int *out_index) {
    if (!c || !out_index) return C2_CHOICE_NEED_SELECTION;
    if (!c->selected) return C2_CHOICE_NEED_SELECTION;
    if (toggle_count < min_toggles) return C2_CHOICE_NEED_TOGGLES;
    if (!probe_state) return C2_CHOICE_NEED_PROBE;
    *out_index = c->index;
    return C2_CHOICE_COMMITTED;
}

int c2_note_toggle(uint32_t *toggle_count) {
    if (!toggle_count || *toggle_count == UINT_MAX) return -1;
    ++*toggle_count;
    return 0;
}
