#include "state_core.h"

static void lut_copy(unsigned char *dst, const unsigned char *src) {
    for (int i = 0; i < LUT_SIZE; ++i)
        dst[i] = src[i];
}

void vbe_oled_state_clear(VbeOledLutState *state) {
    for (int i = 0; i < LUT_SIZE; ++i) {
        state->base[i] = 0;
        state->runtime[i] = 0;
    }
    vbe_source_identity_clear(&state->source);
    state->panel_type = OLED_PANEL_UNKNOWN;
    vbe_oled_transform_neutral(&state->transform);
    vbe_oled_transform_neutral(&state->applied_transform);
}

int vbe_oled_state_derive(VbeOledLutState *state,
                          const unsigned char base[LUT_SIZE],
                          const VbeSourceIdentity *source,
                          int panel_type,
                          const VbeOledTransformParams *requested_transform) {
    lut_copy(state->base, base);
    vbe_source_identity_copy(&state->source, source);
    state->panel_type = panel_type;
    state->transform = *requested_transform;
    vbe_oled_transform_neutral(&state->applied_transform);

    if (vbe_oled_transform_is_neutral(requested_transform)) {
        lut_copy(state->runtime, state->base);
        return VBE_OLED_TRANSFORM_OK;
    }

    if (panel_type != OLED_PANEL_4 && panel_type != OLED_PANEL_5) {
        lut_copy(state->runtime, state->base);
        return VBE_OLED_TRANSFORM_UNSUPPORTED;
    }

    if (vbe_oled_transform_lut(state->base, state->runtime,
                               requested_transform) < 0) {
        lut_copy(state->runtime, state->base);
        return VBE_OLED_TRANSFORM_UNSUPPORTED;
    }

    state->applied_transform = *requested_transform;
    return VBE_OLED_TRANSFORM_OK;
}
