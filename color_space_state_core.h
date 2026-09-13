#pragma once

typedef struct {
    int original_mode;
    int restore_required;
} VbeColorSpaceOwnership;

void vbe_color_space_ownership_init(VbeColorSpaceOwnership *state,
                                    int original_mode);
void vbe_color_space_ownership_note_write(VbeColorSpaceOwnership *state,
                                          int written_mode);
void vbe_color_space_ownership_observe(VbeColorSpaceOwnership *state,
                                       int current_mode);
