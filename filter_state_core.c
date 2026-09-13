#include "filter_state_core.h"
#include "display_domains.h"

void vbe_filter_params_neutral(ScreenFilterParams *params) {
    params->cct = CCT_DEFAULT;
    params->gamma = 1.0f;
    params->contrast = 1.0f;
    params->brightness = 0.0f;
    params->invert = 0;
    params->panel_enhance = 0;
}

void vbe_filter_state_init(VbeFilterStateCore *state) {
    vbe_filter_params_neutral(&state->requested);
    vbe_filter_params_neutral(&state->committed);
    state->requested_domains = 0;
    state->committed_domains = 0;
    state->unsupported_domains = 0;
    state->failed_domains = 0;
}

void vbe_filter_state_begin_request(VbeFilterStateCore *state,
                                    const ScreenFilterParams *requested,
                                    uint32_t requested_domains,
                                    uint32_t unsupported_domains) {
    state->requested = *requested;
    state->requested_domains = requested_domains;
    state->unsupported_domains = unsupported_domains;
    state->failed_domains = 0;
}

void vbe_filter_state_commit_invert(VbeFilterStateCore *state, int enabled) {
    state->committed.invert = enabled ? 1 : 0;
    if (enabled)
        state->committed_domains |= VBE_DISPLAY_DOMAIN_INVERT;
    else
        state->committed_domains &= ~VBE_DISPLAY_DOMAIN_INVERT;
}

void vbe_filter_state_mark_failed(VbeFilterStateCore *state, uint32_t domains) {
    state->failed_domains |= domains;
}
