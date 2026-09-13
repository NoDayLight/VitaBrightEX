#include "transform_core.h"

static unsigned char sat_add(unsigned char value, int delta) {
    int result = (int)value + delta;
    if (result < 0) result = 0;
    if (result > 255) result = 255;
    return (unsigned char)result;
}

void vbe_oled_transform_neutral(VbeOledTransformParams *params) {
    params->bias.r_offset = 0;
    params->bias.g_offset = 0;
    params->bias.b_offset = 0;
    params->warm.enabled = 0;
    params->warm.first_row = 0;
    params->warm.r_offset = 0;
    params->warm.g_offset = 0;
    params->warm.b_offset = 0;
}

int vbe_oled_transform_is_neutral(const VbeOledTransformParams *params) {
    return params->bias.r_offset == 0 && params->bias.g_offset == 0 &&
           params->bias.b_offset == 0 && !params->warm.enabled;
}

int vbe_oled_transform_lut(const unsigned char base[LUT_SIZE],
                           unsigned char runtime[LUT_SIZE],
                           const VbeOledTransformParams *params) {
    if (params->warm.first_row < 0 || params->warm.first_row >= LUT_ROWS)
        return -1;

    for (int row = 0; row < LUT_ROWS; ++row) {
        int warm = params->warm.enabled && row >= params->warm.first_row;
        for (int anchor = 0; anchor < 7; ++anchor) {
            int index = row * LUT_LINE_SIZE + anchor * 3;
            int r = params->bias.r_offset + (warm ? params->warm.r_offset : 0);
            int g = params->bias.g_offset + (warm ? params->warm.g_offset : 0);
            int b = params->bias.b_offset + (warm ? params->warm.b_offset : 0);
            runtime[index + 0] = sat_add(base[index + 0], r);
            runtime[index + 1] = sat_add(base[index + 1], g);
            runtime[index + 2] = sat_add(base[index + 2], b);
        }
    }
    return 0;
}
