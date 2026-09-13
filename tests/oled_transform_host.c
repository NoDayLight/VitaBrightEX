#include <stdio.h>
#include <string.h>
#include "../oled/transform_core.h"

static int ok(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

static int channel_slot(int col, int channel) {
    return col % 3 == channel;
}

int main(void) {
    int failures = 0;
    unsigned char base[LUT_SIZE];
    unsigned char out[LUT_SIZE];
    unsigned char again[LUT_SIZE];
    unsigned char before[LUT_SIZE];

    for (int i = 0; i < LUT_SIZE; ++i)
        base[i] = (unsigned char)((i * 37 + 11) & 255);
    memcpy(before, base, LUT_SIZE);

    VbeOledTransformParams params;
    vbe_oled_transform_neutral(&params);
    failures += ok(vbe_oled_transform_lut(base, out, &params) == 0 &&
                   memcmp(base, out, LUT_SIZE) == 0,
                   "neutral exact 357 bytes");
    failures += ok(memcmp(base, before, LUT_SIZE) == 0, "base immutable");

    params.bias.r_offset = 7;
    failures += ok(vbe_oled_transform_lut(base, out, &params) == 0, "R apply");
    for (int row = 0; row < LUT_ROWS; ++row) {
        for (int col = 0; col < LUT_LINE_SIZE; ++col) {
            int idx = row * LUT_LINE_SIZE + col;
            int expected = base[idx] + (channel_slot(col, 0) ? 7 : 0);
            if (expected > 255) expected = 255;
            failures += ok(out[idx] == (unsigned char)expected,
                           "R touches only 0,3,..18");
        }
    }

    vbe_oled_transform_neutral(&params);
    params.bias.g_offset = -9;
    failures += ok(vbe_oled_transform_lut(base, out, &params) == 0, "G apply");
    for (int row = 0; row < LUT_ROWS; ++row) {
        for (int col = 0; col < LUT_LINE_SIZE; ++col) {
            int idx = row * LUT_LINE_SIZE + col;
            int expected = base[idx] - (channel_slot(col, 1) ? 9 : 0);
            if (expected < 0) expected = 0;
            failures += ok(out[idx] == (unsigned char)expected,
                           "G touches only 1,4,..19");
        }
    }

    vbe_oled_transform_neutral(&params);
    params.bias.b_offset = 300;
    failures += ok(vbe_oled_transform_lut(base, out, &params) == 0,
                   "B saturating apply");
    for (int row = 0; row < LUT_ROWS; ++row) {
        for (int col = 0; col < LUT_LINE_SIZE; ++col) {
            int idx = row * LUT_LINE_SIZE + col;
            if (channel_slot(col, 2))
                failures += ok(out[idx] == 255, "B saturation bounded");
        }
    }

    vbe_oled_transform_neutral(&params);
    params.bias.r_offset = 3;
    params.warm.enabled = 1;
    params.warm.first_row = 12;
    params.warm.g_offset = -4;
    params.warm.b_offset = -8;
    failures += ok(vbe_oled_transform_lut(base, out, &params) == 0 &&
                   vbe_oled_transform_lut(base, again, &params) == 0 &&
                   memcmp(out, again, LUT_SIZE) == 0,
                   "repeated apply derives from base, no compounding");
    failures += ok(memcmp(base, before, LUT_SIZE) == 0,
                   "runtime transform never mutates base");

    if (failures) return 1;
    puts("OLED register-domain transform regressions: OK");
    return 0;
}
