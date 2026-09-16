#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "observer_core.h"

int main(void) {
    C2ChoiceCursor c;
    int out = -1;
    uint32_t toggles = 0;
    unsigned i;

    c2_choice_init(&c);
    assert(c.selected == 0);
    assert(c2_choice_try_commit(&c, 3, 1, 3, &out) == C2_CHOICE_NEED_SELECTION);

    c2_choice_move(&c, +1, 3);
    assert(c.selected == 1 && c.index == 0);
    c2_choice_move(&c, +1, 3);
    assert(c.index == 1);
    assert(c2_choice_try_commit(&c, 2, 1, 3, &out) == C2_CHOICE_NEED_TOGGLES);
    assert(c2_choice_try_commit(&c, 3, 0, 3, &out) == C2_CHOICE_NEED_PROBE);
    assert(c2_choice_try_commit(&c, 3, 1, 3, &out) == C2_CHOICE_COMMITTED);
    assert(out == 1);

    /* A new question is deliberately UNSET, never silently reset to NO. */
    c2_choice_init(&c);
    assert(c.selected == 0);
    assert(c2_choice_try_commit(&c, 3, 1, 3, &out) == C2_CHOICE_NEED_SELECTION);
    c2_choice_move(&c, -1, 3);
    assert(c.selected == 1 && c.index == 2);

    /* The temporal witness has no artificial 25/255 press ceiling. */
    for (i = 0; i < 1000; ++i) assert(c2_note_toggle(&toggles) == 0);
    assert(toggles == 1000u);

    puts("GATE1F_CAMPAIGN2_OBSERVER_CORE_TEST=PASS");
    return 0;
}
