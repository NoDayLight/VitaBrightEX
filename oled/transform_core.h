#pragma once
#include "lut.h"

typedef struct {
    int r_offset;
    int g_offset;
    int b_offset;
} VbeOledRegisterBias;

typedef struct {
    int enabled;
    int first_row;
    int r_offset;
    int g_offset;
    int b_offset;
} VbeOledWarmProfile;

typedef struct {
    VbeOledRegisterBias bias;
    VbeOledWarmProfile warm;
} VbeOledTransformParams;

void vbe_oled_transform_neutral(VbeOledTransformParams *params);
int vbe_oled_transform_is_neutral(const VbeOledTransformParams *params);
int vbe_oled_transform_lut(const unsigned char base[LUT_SIZE],
                           unsigned char runtime[LUT_SIZE],
                           const VbeOledTransformParams *params);
