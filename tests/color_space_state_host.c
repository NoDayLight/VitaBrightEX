#include <stdio.h>
#include "../color_space_state_core.h"

static int ok(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

int main(void) {
    int failures = 0;
    VbeColorSpaceOwnership state;

    vbe_color_space_ownership_init(&state, 0);
    vbe_color_space_ownership_observe(&state, 1);
    failures += ok(state.original_mode == 0 && !state.restore_required,
                   "external observation never creates restore ownership");

    vbe_color_space_ownership_note_write(&state, 1);
    failures += ok(state.restore_required,
                   "successful plugin write away from original creates ownership");
    vbe_color_space_ownership_observe(&state, 1);
    failures += ok(state.restore_required,
                   "observing plugin-written non-original mode retains ownership");

    vbe_color_space_ownership_note_write(&state, 0);
    failures += ok(state.restore_required,
                   "restore setter attempt alone cannot clear ownership");
    vbe_color_space_ownership_observe(&state, 0);
    failures += ok(!state.restore_required,
                   "confirmed original readback clears restore ownership");

    if (failures) return 1;
    puts("color-space ownership regressions: OK");
    return 0;
}
