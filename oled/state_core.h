#pragma once
#include "lut.h"
#include "transform_core.h"
#include "../source_authority.h"

enum {
    VBE_OLED_TRANSFORM_OK = 0,
    VBE_OLED_TRANSFORM_UNSUPPORTED = 1,
};

typedef struct {
    unsigned char base[LUT_SIZE];
    unsigned char runtime[LUT_SIZE];
    VbeSourceIdentity source;
    int panel_type;
    /* Requested transform is retained even when the panel cannot apply it. */
    VbeOledTransformParams transform;
    /* Applied transform always describes runtime; neutral means runtime=base. */
    VbeOledTransformParams applied_transform;
} VbeOledLutState;

void vbe_oled_state_clear(VbeOledLutState *state);
int vbe_oled_state_derive(VbeOledLutState *state,
                          const unsigned char base[LUT_SIZE],
                          const VbeSourceIdentity *source,
                          int panel_type,
                          const VbeOledTransformParams *requested_transform);
