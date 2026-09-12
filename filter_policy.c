#include "filter_policy.h"

VbeFilterRequestPolicy vbe_filter_request_policy(const ScreenFilterParams *params) {
    VbeFilterRequestPolicy policy;
    int advanced = params->cct != CCT_DEFAULT ||
                   params->gamma != 1.0f ||
                   params->contrast != 1.0f ||
                   params->brightness != 0.0f ||
                   params->panel_enhance != 0;

    policy.result = advanced ? VBE_RESULT_UNSUPPORTED : VBE_RESULT_OK;
    policy.csc_state = VBE_CAP_UNSUPPORTED;
    policy.transfer_state = VBE_CAP_UNSUPPORTED;
    policy.error = VBE_ERR_NONE;
    return policy;
}
