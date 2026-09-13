#pragma once
#include <stdint.h>

enum {
    VBE_DISPLAY_DOMAIN_INVERT = 1u << 0,
    VBE_DISPLAY_DOMAIN_AFFINE_CSC = 1u << 1,
    VBE_DISPLAY_DOMAIN_TRANSFER = 1u << 2,
    VBE_DISPLAY_DOMAIN_OLED_REGISTER = 1u << 3,
};
