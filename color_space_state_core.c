#include "color_space_state_core.h"

void vbe_color_space_ownership_init(VbeColorSpaceOwnership *state,
                                    int original_mode) {
    state->original_mode = original_mode;
    state->restore_required = 0;
}

void vbe_color_space_ownership_note_write(VbeColorSpaceOwnership *state,
                                          int written_mode) {
    if (written_mode != state->original_mode)
        state->restore_required = 1;
}

void vbe_color_space_ownership_observe(VbeColorSpaceOwnership *state,
                                       int current_mode) {
    if (state->restore_required && current_mode == state->original_mode)
        state->restore_required = 0;
}
