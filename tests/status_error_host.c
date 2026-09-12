#include <stdio.h>
#include "../status_error_core.h"

static int ok(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

static VbeErrorSlot slot(const VbeErrorState *state, int domain) {
    return vbe_error_state_get(state, domain);
}

int main(void) {
    int failures = 0;
    VbeErrorState state;
    VbeErrorSlot summary;
    VbeErrorSlot requested;
    VbeErrorSlot rollback_failure;

    vbe_error_state_init(&state);

    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_CONFIG, 0, VBE_ERR_CONFIG, -10);
    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_CONFIG, 1, VBE_ERR_CONFIG, 0);
    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_BRIGHTNESS, 0,
                          VBE_ERR_LAYOUT_MISMATCH, -30);
    failures += ok(slot(&state, VBE_ERROR_DOMAIN_CONFIG).error == VBE_ERR_NONE,
                   "config clears at config stage even if brightness later fails");
    failures += ok(slot(&state, VBE_ERROR_DOMAIN_BRIGHTNESS).error == VBE_ERR_LAYOUT_MISMATCH,
                   "brightness failure remains independently visible");

    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_BRIGHTNESS, 1,
                          VBE_ERR_BACKEND, 0);
    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_COLOR_SPACE, 0,
                          VBE_ERR_DISPLAY_CAPABILITY, -20);
    failures += ok(slot(&state, VBE_ERROR_DOMAIN_BRIGHTNESS).error == VBE_ERR_NONE,
                   "brightness success clears brightness before later color failure");
    failures += ok(slot(&state, VBE_ERROR_DOMAIN_COLOR_SPACE).error == VBE_ERR_DISPLAY_CAPABILITY,
                   "color failure stays in color domain");

    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_CONFIG, 0, VBE_ERR_CONFIG, -11);
    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_BRIGHTNESS, 0,
                          VBE_ERR_BACKEND, -31);
    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_BRIGHTNESS, 1,
                          VBE_ERR_BACKEND, 0);
    failures += ok(slot(&state, VBE_ERROR_DOMAIN_CONFIG).error == VBE_ERR_CONFIG,
                   "brightness success cannot erase broken config");
    failures += ok(slot(&state, VBE_ERROR_DOMAIN_BRIGHTNESS).error == VBE_ERR_NONE,
                   "brightness repair clears only brightness");

    requested.error = VBE_ERR_TABLE_INJECTION;
    requested.detail = -40;
    rollback_failure.error = VBE_ERR_LUT_ROLLBACK;
    rollback_failure.detail = -41;
    vbe_error_state_set(&state, VBE_ERROR_DOMAIN_BRIGHTNESS,
                        requested.error, requested.detail);
    vbe_error_state_recovery(&state, VBE_ERROR_DOMAIN_BRIGHTNESS, 1,
                             requested, rollback_failure);
    failures += ok(slot(&state, VBE_ERROR_DOMAIN_BRIGHTNESS).error == VBE_ERR_TABLE_INJECTION &&
                   slot(&state, VBE_ERROR_DOMAIN_BRIGHTNESS).detail == -40,
                   "successful rollback preserves requested-operation failure");

    vbe_error_state_recovery(&state, VBE_ERROR_DOMAIN_BRIGHTNESS, 0,
                             requested, rollback_failure);
    failures += ok(slot(&state, VBE_ERROR_DOMAIN_BRIGHTNESS).error == VBE_ERR_LUT_ROLLBACK &&
                   slot(&state, VBE_ERROR_DOMAIN_BRIGHTNESS).detail == -41,
                   "failed rollback supersedes with rollback failure");

    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_CONFIG, 1, VBE_ERR_CONFIG, 0);
    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_BRIGHTNESS, 1, VBE_ERR_BACKEND, 0);
    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_COLOR_SPACE, 1,
                          VBE_ERR_DISPLAY_CAPABILITY, 0);
    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_FILTER, 1,
                          VBE_ERR_DISPLAY_CAPABILITY, 0);
    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_INPUT, 1,
                          VBE_ERR_INVALID_USER_INPUT, 0);
    failures += ok(slot(&state, VBE_ERROR_DOMAIN_CONFIG).error == VBE_ERR_NONE &&
                   slot(&state, VBE_ERROR_DOMAIN_BRIGHTNESS).error == VBE_ERR_NONE &&
                   slot(&state, VBE_ERROR_DOMAIN_COLOR_SPACE).error == VBE_ERR_NONE &&
                   slot(&state, VBE_ERROR_DOMAIN_FILTER).error == VBE_ERR_NONE &&
                   slot(&state, VBE_ERROR_DOMAIN_INPUT).error == VBE_ERR_NONE,
                   "all repaired domains clear independently");

    /* Stop-domain truth survives unrelated successful teardown stages. */
    vbe_error_state_set(&state, VBE_ERROR_DOMAIN_FILTER,
                        VBE_ERR_DISPLAY_CAPABILITY, -60);
    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_COLOR_SPACE, 1,
                          VBE_ERR_DISPLAY_CAPABILITY, 0);
    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_BRIGHTNESS, 1,
                          VBE_ERR_BACKEND, 0);
    failures += ok(slot(&state, VBE_ERROR_DOMAIN_FILTER).error == VBE_ERR_DISPLAY_CAPABILITY &&
                   slot(&state, VBE_ERROR_DOMAIN_FILTER).detail == -60,
                   "filter stop failure survives later color/backend success");
    vbe_error_state_clear(&state, VBE_ERROR_DOMAIN_FILTER);

    vbe_error_state_set(&state, VBE_ERROR_DOMAIN_COLOR_SPACE,
                        VBE_ERR_DISPLAY_CAPABILITY, -61);
    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_BRIGHTNESS, 1,
                          VBE_ERR_BACKEND, 0);
    failures += ok(slot(&state, VBE_ERROR_DOMAIN_COLOR_SPACE).error == VBE_ERR_DISPLAY_CAPABILITY &&
                   slot(&state, VBE_ERROR_DOMAIN_COLOR_SPACE).detail == -61,
                   "color stop failure survives backend success");
    vbe_error_state_clear(&state, VBE_ERROR_DOMAIN_COLOR_SPACE);

    vbe_error_state_set(&state, VBE_ERROR_DOMAIN_BRIGHTNESS,
                        VBE_ERR_RESOURCE_RELEASE, -62);
    vbe_error_state_stage(&state, VBE_ERROR_DOMAIN_FILTER, 1,
                          VBE_ERR_DISPLAY_CAPABILITY, 0);
    failures += ok(slot(&state, VBE_ERROR_DOMAIN_BRIGHTNESS).error == VBE_ERR_RESOURCE_RELEASE &&
                   slot(&state, VBE_ERROR_DOMAIN_BRIGHTNESS).detail == -62,
                   "backend stop release failure survives unrelated teardown success");
    vbe_error_state_clear(&state, VBE_ERROR_DOMAIN_BRIGHTNESS);

    vbe_error_state_set(&state, VBE_ERROR_DOMAIN_INPUT, VBE_ERR_INVALID_USER_INPUT, -50);
    vbe_error_state_set(&state, VBE_ERROR_DOMAIN_FILTER, VBE_ERR_DISPLAY_CAPABILITY, -51);
    vbe_error_state_set(&state, VBE_ERROR_DOMAIN_COLOR_SPACE, VBE_ERR_DISPLAY_CAPABILITY, -52);
    vbe_error_state_set(&state, VBE_ERROR_DOMAIN_CONFIG, VBE_ERR_CONFIG, -53);
    vbe_error_state_set(&state, VBE_ERROR_DOMAIN_BRIGHTNESS, VBE_ERR_BACKEND, -54);
    vbe_error_state_set(&state, VBE_ERROR_DOMAIN_SYNC, VBE_ERR_SYNCHRONIZATION, -55);
    summary = vbe_error_state_summary(&state);
    failures += ok(summary.error == VBE_ERR_SYNCHRONIZATION && summary.detail == -55,
                   "summary precedence starts with synchronization");
    vbe_error_state_clear(&state, VBE_ERROR_DOMAIN_SYNC);
    summary = vbe_error_state_summary(&state);
    failures += ok(summary.error == VBE_ERR_BACKEND && summary.detail == -54,
                   "brightness precedes config");
    vbe_error_state_clear(&state, VBE_ERROR_DOMAIN_BRIGHTNESS);
    summary = vbe_error_state_summary(&state);
    failures += ok(summary.error == VBE_ERR_CONFIG && summary.detail == -53,
                   "config precedes color");
    vbe_error_state_clear(&state, VBE_ERROR_DOMAIN_CONFIG);
    summary = vbe_error_state_summary(&state);
    failures += ok(summary.error == VBE_ERR_DISPLAY_CAPABILITY && summary.detail == -52,
                   "color precedes filter and input");

    if (failures) return 1;
    puts("status lifecycle/error-domain regressions: OK");
    return 0;
}
