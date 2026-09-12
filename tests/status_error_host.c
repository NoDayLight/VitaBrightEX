#include <stdio.h>
#include "../status_error_core.h"

static int ok(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

int main(void) {
    int failures = 0;
    VbeErrorState state;
    VbeErrorSlot slot;
    vbe_error_state_init(&state);

    vbe_error_state_set(&state, VBE_ERROR_DOMAIN_CONFIG, VBE_ERR_CONFIG, -10);
    vbe_error_state_set(&state, VBE_ERROR_DOMAIN_COLOR_SPACE, VBE_ERR_DISPLAY_CAPABILITY, -20);
    vbe_error_state_clear(&state, VBE_ERROR_DOMAIN_COLOR_SPACE);
    slot = vbe_error_state_summary(&state);
    failures += ok(slot.error == VBE_ERR_CONFIG && slot.detail == -10,
                   "unrelated success cannot erase config error");

    vbe_error_state_set(&state, VBE_ERROR_DOMAIN_BRIGHTNESS, VBE_ERR_LAYOUT_MISMATCH, -30);
    slot = vbe_error_state_summary(&state);
    failures += ok(slot.error == VBE_ERR_LAYOUT_MISMATCH && slot.detail == -30,
                   "brightness failure outranks config in summary");
    failures += ok(vbe_error_state_get(&state, VBE_ERROR_DOMAIN_CONFIG).error == VBE_ERR_CONFIG,
                   "config error retained behind brightness summary");

    vbe_error_state_clear(&state, VBE_ERROR_DOMAIN_BRIGHTNESS);
    slot = vbe_error_state_summary(&state);
    failures += ok(slot.error == VBE_ERR_CONFIG,
                   "config resurfaces when higher-priority failure clears");

    vbe_error_state_set(&state, VBE_ERROR_DOMAIN_INPUT, VBE_ERR_INVALID_USER_INPUT, -40);
    vbe_error_state_set(&state, VBE_ERROR_DOMAIN_FILTER, VBE_ERR_DISPLAY_CAPABILITY, -50);
    slot = vbe_error_state_summary(&state);
    failures += ok(slot.error == VBE_ERR_CONFIG,
                   "optional and input errors do not hide config");

    vbe_error_state_clear(&state, VBE_ERROR_DOMAIN_CONFIG);
    slot = vbe_error_state_summary(&state);
    failures += ok(slot.error == VBE_ERR_DISPLAY_CAPABILITY && slot.detail == -50,
                   "filter error becomes visible after config resolves");

    vbe_error_state_set(&state, VBE_ERROR_DOMAIN_SYNC, VBE_ERR_SYNCHRONIZATION, -60);
    slot = vbe_error_state_summary(&state);
    failures += ok(slot.error == VBE_ERR_SYNCHRONIZATION,
                   "synchronization error has highest precedence");

    if (failures) return 1;
    puts("status error-domain regressions: OK");
    return 0;
}
