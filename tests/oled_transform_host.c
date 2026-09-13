#include <stdio.h>
#include <string.h>
#include "../oled/transform_core.h"

static int ok(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}
static int channel_slot(int col, int channel) { return col % 3 == channel; }
static unsigned char sat(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (unsigned char)value;
}

int main(void) {
    int failures = 0;
    unsigned char base[LUT_SIZE], out[LUT_SIZE], again[LUT_SIZE], before[LUT_SIZE];
    for (int i = 0; i < LUT_SIZE; ++i)
        base[i] = (unsigned char)((i * 37 + 11) & 255);
    memcpy(before, base, LUT_SIZE);

    VbeOledTransformParams params;
    vbe_oled_transform_neutral(&params);
    failures += ok(vbe_oled_transform_lut(base, out, &params) == 0 &&
                   memcmp(base, out, LUT_SIZE) == 0,
                   "neutral exact 357 bytes");

    params.bias.r_offset = 7;
    failures += ok(vbe_oled_transform_lut(base, out, &params) == 0, "R apply");
    for (int row = 0; row < LUT_ROWS; ++row)
        for (int col = 0; col < LUT_LINE_SIZE; ++col) {
            int idx = row * LUT_LINE_SIZE + col;
            int delta = channel_slot(col, 0) ? 7 : 0;
            failures += ok(out[idx] == sat((int)base[idx] + delta),
                           "R touches only seven R reference bytes per row");
        }

    vbe_oled_transform_neutral(&params);
    params.bias.g_offset = -9;
    failures += ok(vbe_oled_transform_lut(base, out, &params) == 0, "G apply");
    for (int row = 0; row < LUT_ROWS; ++row)
        for (int col = 0; col < LUT_LINE_SIZE; ++col) {
            int idx = row * LUT_LINE_SIZE + col;
            int delta = channel_slot(col, 1) ? -9 : 0;
            failures += ok(out[idx] == sat((int)base[idx] + delta),
                           "G touches only seven G reference bytes per row");
        }

    vbe_oled_transform_neutral(&params);
    params.bias.b_offset = 300;
    failures += ok(vbe_oled_transform_lut(base, out, &params) == 0,
                   "B saturating apply");
    for (int row = 0; row < LUT_ROWS; ++row)
        for (int col = 0; col < LUT_LINE_SIZE; ++col)
            if (channel_slot(col, 2))
                failures += ok(out[row * LUT_LINE_SIZE + col] == 255,
                               "B saturation bounded");

    /* Manual warm profile composes with global bias and first_row is inclusive.
     * first_row=16 therefore changes exactly the final dim/inactivity row. */
    vbe_oled_transform_neutral(&params);
    params.bias.r_offset = 3;
    params.warm.enabled = 1;
    params.warm.first_row = 16;
    params.warm.r_offset = 2;
    params.warm.g_offset = -4;
    params.warm.b_offset = -8;
    failures += ok(vbe_oled_transform_lut(base, out, &params) == 0,
                   "manual warm profile applies");
    for (int row = 0; row < LUT_ROWS; ++row) {
        for (int col = 0; col < LUT_LINE_SIZE; ++col) {
            int idx = row * LUT_LINE_SIZE + col;
            int delta = channel_slot(col, 0) ? 3 : 0;
            if (row >= 16) {
                if (channel_slot(col, 0)) delta += 2;
                else if (channel_slot(col, 1)) delta += -4;
                else delta += -8;
            }
            failures += ok(out[idx] == sat((int)base[idx] + delta),
                           "warm first_row threshold and bias composition exact");
        }
    }

    failures += ok(vbe_oled_transform_lut(base, again, &params) == 0 &&
                   memcmp(out, again, LUT_SIZE) == 0,
                   "repeated apply derives from base, no compounding");
    failures += ok(memcmp(base, before, LUT_SIZE) == 0,
                   "runtime transform never mutates base");

    params.warm.first_row = 17;
    failures += ok(vbe_oled_transform_lut(base, out, &params) < 0,
                   "out-of-range warm threshold rejected");

    if (failures) return 1;
    puts("OLED RGB/warm register-domain transform regressions: OK");
    return 0;
}
