#pragma once

#include <stdint.h>

#define C2_CHOICE_NEED_SELECTION (-1)
#define C2_CHOICE_NEED_TOGGLES   (-2)
#define C2_CHOICE_NEED_PROBE     (-3)
#define C2_CHOICE_COMMITTED       1

typedef struct {
    int index;
    int selected;
} C2ChoiceCursor;

void c2_choice_init(C2ChoiceCursor *c);
void c2_choice_move(C2ChoiceCursor *c, int direction, int item_count);
int c2_choice_try_commit(const C2ChoiceCursor *c,
                         uint32_t toggle_count,
                         int probe_state,
                         uint32_t min_toggles,
                         int *out_index);
int c2_note_toggle(uint32_t *toggle_count);
