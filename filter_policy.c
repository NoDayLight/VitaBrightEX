#include "filter_policy.h"

VbeFilterRequestPolicy vbe_filter_request_policy(const ScreenFilterParams *params) {
    VbeFilterRequestPolicy out = {
        .requested_domains = 0,
        .attempt_domains = 0,
        .unsupported_domains = 0,
        .csc_state = VBE_CAP_INACTIVE,
        .transfer_state = VBE_CAP_INACTIVE,
    };

    if (params->invert)
        out.requested_domains |= VBE_DISPLAY_DOMAIN_INVERT;

    if (params->cct != CCT_DEFAULT || params->contrast != 1.0f ||
        params->brightness != 0.0f)
        out.requested_domains |= VBE_DISPLAY_DOMAIN_AFFINE_CSC;

    if (params->gamma != 1.0f || params->panel_enhance != 0)
        out.requested_domains |= VBE_DISPLAY_DOMAIN_TRANSFER;

    out.attempt_domains = out.requested_domains & VBE_DISPLAY_DOMAIN_INVERT;
    out.unsupported_domains = out.requested_domains &
        (VBE_DISPLAY_DOMAIN_AFFINE_CSC | VBE_DISPLAY_DOMAIN_TRANSFER);

    if (out.requested_domains & VBE_DISPLAY_DOMAIN_AFFINE_CSC)
        out.csc_state = VBE_CAP_UNSUPPORTED;
    if (out.requested_domains & VBE_DISPLAY_DOMAIN_TRANSFER)
        out.transfer_state = VBE_CAP_UNSUPPORTED;

    return out;
}
