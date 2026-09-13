#include "filter_policy.h"

VbeFilterRequestPolicy vbe_filter_request_policy(const ScreenFilterParams *params) {
    VbeFilterRequestPolicy out = {
        .requested_domains = 0,
        .attempt_domains = 0,
        .unsupported_domains = 0,
        .invert_state = VBE_CAP_UNSUPPORTED,
        .csc_state = VBE_CAP_UNSUPPORTED,
        .transfer_state = VBE_CAP_UNSUPPORTED,
        .result = VBE_RESULT_OK,
        .error = VBE_ERR_NONE,
    };

    if (params->invert)
        out.requested_domains |= VBE_DISPLAY_DOMAIN_INVERT;

    if (params->cct != CCT_DEFAULT || params->contrast != 1.0f ||
        params->brightness != 0.0f)
        out.requested_domains |= VBE_DISPLAY_DOMAIN_AFFINE_CSC;

    if (params->gamma != 1.0f || params->panel_enhance != 0)
        out.requested_domains |= VBE_DISPLAY_DOMAIN_TRANSFER;

    /* No generic filter domain is mutation-safe in pseudo-v1.4:
     * invert lacks a trustworthy original-state reader, persistent CSC lacks
     * restoration/lifecycle proof, and no nonlinear transfer stage is proven. */
    out.unsupported_domains = out.requested_domains;
    if (out.unsupported_domains != 0)
        out.result = VBE_RESULT_UNSUPPORTED;

    return out;
}
