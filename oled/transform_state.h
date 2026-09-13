#pragma once
#include <stdint.h>
#include "transform_core.h"

#define VBE_OLED_TRANSFORM_STATE_ABI_VERSION 1u

typedef struct {
    uint32_t abi_version;
    int panel_type;
    int capability_result;
    VbeOledRegisterBias requested_bias;
    VbeOledRegisterBias applied_bias;
    VbeOledWarmProfile requested_warm;
    VbeOledWarmProfile applied_warm;
    int source_kind;
} VitaBrightOledTransformState;
