#include <stdio.h>
#include "../filter_policy.h"
#include "../filter_state_core.h"

static int ok(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

int main(void) {
    int failures = 0;
    ScreenFilterParams params;
    vbe_filter_params_neutral(&params);
    VbeFilterRequestPolicy policy = vbe_filter_request_policy(&params);
    failures += ok(policy.requested_domains == 0 &&
                   policy.attempt_domains == 0 &&
                   policy.unsupported_domains == 0 &&
                   policy.result == VBE_RESULT_OK &&
                   policy.invert_state == VBE_CAP_UNSUPPORTED &&
                   policy.csc_state == VBE_CAP_UNSUPPORTED &&
                   policy.transfer_state == VBE_CAP_UNSUPPORTED,
                   "neutral reports unavailable safe generic backends without requesting domains");

    params.invert = 1;
    policy = vbe_filter_request_policy(&params);
    failures += ok(policy.requested_domains == VBE_DISPLAY_DOMAIN_INVERT &&
                   policy.attempt_domains == 0 &&
                   policy.unsupported_domains == VBE_DISPLAY_DOMAIN_INVERT &&
                   policy.result == VBE_RESULT_UNSUPPORTED &&
                   policy.error == VBE_ERR_NONE,
                   "invert is capability-unsupported without original-state ownership");

    vbe_filter_params_neutral(&params);
    params.cct = 5000;
    policy = vbe_filter_request_policy(&params);
    failures += ok((policy.requested_domains & VBE_DISPLAY_DOMAIN_AFFINE_CSC) != 0 &&
                   (policy.unsupported_domains & VBE_DISPLAY_DOMAIN_AFFINE_CSC) != 0 &&
                   policy.result == VBE_RESULT_UNSUPPORTED,
                   "persistent affine CSC unsupported");

    vbe_filter_params_neutral(&params);
    params.gamma = 1.2f;
    policy = vbe_filter_request_policy(&params);
    failures += ok((policy.requested_domains & VBE_DISPLAY_DOMAIN_TRANSFER) != 0 &&
                   (policy.unsupported_domains & VBE_DISPLAY_DOMAIN_TRANSFER) != 0 &&
                   policy.result == VBE_RESULT_UNSUPPORTED,
                   "nonlinear transfer unsupported");

    vbe_filter_params_neutral(&params);
    params.invert = 1;
    params.cct = 5000;
    policy = vbe_filter_request_policy(&params);
    failures += ok(policy.attempt_domains == 0 &&
                   policy.unsupported_domains ==
                       (VBE_DISPLAY_DOMAIN_INVERT | VBE_DISPLAY_DOMAIN_AFFINE_CSC),
                   "multiple unsupported requests retain complete domain truth");

    if (failures) return 1;
    puts("filter capability decomposition regressions: OK");
    return 0;
}
