#include <stdio.h>
#include <string.h>
#include "../oled/state_core.h"

static int ok(int condition, const char *name) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", name);
    return 1;
}

static void fill_base(unsigned char lut[LUT_SIZE]) {
    for (int i = 0; i < LUT_SIZE; ++i)
        lut[i] = (unsigned char)((i * 13 + 7) & 0xFF);
}

int main(void) {
    int failures = 0;
    unsigned char base[LUT_SIZE];
    fill_base(base);
    VbeSourceIdentity source;
    vbe_source_identity_file(&source, "ux0:tai/vitabright_lut_p4.txt");

    VbeOledTransformParams first;
    vbe_oled_transform_neutral(&first);
    first.bias.r_offset = 5;

    VbeOledLutState state;
    vbe_oled_state_clear(&state);
    failures += ok(vbe_oled_state_derive(&state, base, &source,
                                         OLED_PANEL_4, &first) == VBE_OLED_TRANSFORM_OK,
                   "P4 register transform supported");
    failures += ok(memcmp(state.base, base, LUT_SIZE) == 0,
                   "base remains authoritative");
    failures += ok(memcmp(state.runtime, base, LUT_SIZE) != 0,
                   "runtime is derived");

    unsigned char first_runtime[LUT_SIZE];
    memcpy(first_runtime, state.runtime, LUT_SIZE);
    VbeOledTransformParams second;
    vbe_oled_transform_neutral(&second);
    second.bias.b_offset = -9;
    failures += ok(vbe_oled_state_derive(&state, state.base, &source,
                                         OLED_PANEL_4, &second) == VBE_OLED_TRANSFORM_OK,
                   "second transform supported");
    failures += ok(memcmp(state.base, base, LUT_SIZE) == 0,
                   "transform change still derives from original base");
    failures += ok(memcmp(state.runtime, first_runtime, LUT_SIZE) != 0,
                   "transform does not compound runtime into runtime");

    failures += ok(vbe_oled_state_derive(&state, base, &source,
                                         OLED_PANEL_6, &first) == VBE_OLED_TRANSFORM_UNSUPPORTED,
                   "P6 advanced transform reports unsupported");
    failures += ok(memcmp(state.runtime, base, LUT_SIZE) == 0,
                   "P6 unsupported transform leaves brightness LUT usable");

    failures += ok(vbe_oled_state_derive(&state, base, &source,
                                         OLED_PANEL_UNKNOWN, &first) == VBE_OLED_TRANSFORM_UNSUPPORTED,
                   "unknown panel advanced transform reports unsupported");
    failures += ok(memcmp(state.runtime, base, LUT_SIZE) == 0,
                   "unknown panel keeps base runtime");

    if (failures) return 1;
    puts("OLED base/runtime ownership regressions: OK");
    return 0;
}
