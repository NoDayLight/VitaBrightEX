#include <stdio.h>
#include "../filter_state_core.h"
#include "../display_domains.h"

static int ok(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

int main(void) {
    int failures = 0;
    VbeFilterStateCore state;
    vbe_filter_state_init(&state);
    failures += ok(state.committed.cct == CCT_DEFAULT &&
                   state.committed.invert == 0 &&
                   state.committed_domains == 0,
                   "initial committed state is neutral");

    ScreenFilterParams requested;
    vbe_filter_params_neutral(&requested);
    requested.invert = 1;
    requested.cct = 5000;
    uint32_t domains = VBE_DISPLAY_DOMAIN_INVERT | VBE_DISPLAY_DOMAIN_AFFINE_CSC;
    vbe_filter_state_begin_request(&state, &requested, domains, domains);
    failures += ok(state.requested.cct == 5000 &&
                   state.requested.invert == 1 &&
                   state.requested_domains == domains &&
                   state.unsupported_domains == domains,
                   "requested unsupported state is retained exactly");
    failures += ok(state.committed.cct == CCT_DEFAULT &&
                   state.committed.invert == 0 &&
                   state.committed_domains == 0,
                   "unsupported request never masquerades as committed hardware state");
    failures += ok(state.failed_domains == 0,
                   "unsupported capability is not a runtime failure");

    requested.invert = 0; /* editor changes only invert, preserving requested CCT */
    vbe_filter_state_begin_request(&state, &requested,
                                   VBE_DISPLAY_DOMAIN_AFFINE_CSC,
                                   VBE_DISPLAY_DOMAIN_AFFINE_CSC);
    failures += ok(state.requested.cct == 5000 && state.requested.invert == 0,
                   "single-field edit preserves other requested unsupported parameters");
    failures += ok(state.committed.cct == CCT_DEFAULT && state.committed.invert == 0,
                   "single-field request edit still leaves committed hardware neutral");

    if (failures) return 1;
    puts("filter requested/committed state regressions: OK");
    return 0;
}
