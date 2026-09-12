#include <stdio.h>
#include "../filter_policy.h"

static int ok(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

static ScreenFilterParams neutral(void) {
    ScreenFilterParams p;
    p.cct = CCT_DEFAULT;
    p.gamma = 1.0f;
    p.contrast = 1.0f;
    p.brightness = 0.0f;
    p.invert = 0;
    p.panel_enhance = 0;
    return p;
}

static int unsupported(ScreenFilterParams p) {
    VbeFilterRequestPolicy policy = vbe_filter_request_policy(&p);
    return policy.result == VBE_RESULT_UNSUPPORTED &&
           policy.csc_state == VBE_CAP_UNSUPPORTED &&
           policy.transfer_state == VBE_CAP_UNSUPPORTED &&
           policy.error == VBE_ERR_NONE;
}

int main(void) {
    int failures = 0;
    ScreenFilterParams p = neutral();
    VbeFilterRequestPolicy policy = vbe_filter_request_policy(&p);
    failures += ok(policy.result == VBE_RESULT_OK &&
                   policy.csc_state == VBE_CAP_UNSUPPORTED &&
                   policy.transfer_state == VBE_CAP_UNSUPPORTED &&
                   policy.error == VBE_ERR_NONE,
                   "neutral request has no runtime error");

    p = neutral(); p.cct = 7000;
    failures += ok(unsupported(p), "CCT request is named unsupported without error");
    p = neutral(); p.gamma = 1.2f;
    failures += ok(unsupported(p), "gamma request is named unsupported without error");
    p = neutral(); p.contrast = 1.2f;
    failures += ok(unsupported(p), "contrast request is named unsupported without error");
    p = neutral(); p.brightness = 0.1f;
    failures += ok(unsupported(p), "brightness request is named unsupported without error");
    p = neutral(); p.panel_enhance = 1;
    failures += ok(unsupported(p), "panel enhancement request is named unsupported without error");
    p = neutral(); p.invert = 1;
    policy = vbe_filter_request_policy(&p);
    failures += ok(policy.result == VBE_RESULT_OK,
                   "verified invert remains supported request dimension");

    if (failures) return 1;
    puts("filter unsupported-capability regressions: OK");
    return 0;
}
