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

static int same_transform(const VbeOledTransformParams *a,
                          const VbeOledTransformParams *b) {
    return a->bias.r_offset == b->bias.r_offset &&
           a->bias.g_offset == b->bias.g_offset &&
           a->bias.b_offset == b->bias.b_offset &&
           a->warm.enabled == b->warm.enabled &&
           a->warm.first_row == b->warm.first_row &&
           a->warm.r_offset == b->warm.r_offset &&
           a->warm.g_offset == b->warm.g_offset &&
           a->warm.b_offset == b->warm.b_offset;
}

static int same_state(const VbeOledLutState *a, const VbeOledLutState *b) {
    return memcmp(a->base, b->base, LUT_SIZE) == 0 &&
           memcmp(a->runtime, b->runtime, LUT_SIZE) == 0 &&
           a->source.kind == b->source.kind &&
           strcmp(a->source.path, b->source.path) == 0 &&
           a->panel_type == b->panel_type &&
           same_transform(&a->transform, &b->transform) &&
           same_transform(&a->applied_transform, &b->applied_transform);
}

int main(void) {
    int failures = 0;
    unsigned char base[LUT_SIZE];
    fill_base(base);

    VbeSourceIdentity source;
    vbe_source_identity_file(&source, "ux0:tai/vitabright_lut_p4.txt");

    VbeOledTransformParams neutral;
    vbe_oled_transform_neutral(&neutral);
    VbeOledLutState state;
    vbe_oled_state_clear(&state);
    failures += ok(vbe_oled_state_derive(&state, base, &source,
                                         OLED_PANEL_4, &neutral) == VBE_OLED_TRANSFORM_OK,
                   "neutral transform supported");
    failures += ok(memcmp(state.runtime, base, LUT_SIZE) == 0,
                   "zero transform runtime is bit-identical to base");
    failures += ok(state.source.kind == VBE_SOURCE_ID_FILE &&
                   strcmp(state.source.path, source.path) == 0,
                   "source survives derivation");
    failures += ok(same_transform(&state.transform, &neutral) &&
                   same_transform(&state.applied_transform, &neutral),
                   "neutral requested and applied truth agree");

    VbeOledTransformParams first;
    vbe_oled_transform_neutral(&first);
    first.bias.r_offset = 5;
    failures += ok(vbe_oled_state_derive(&state, base, &source,
                                         OLED_PANEL_4, &first) == VBE_OLED_TRANSFORM_OK,
                   "P4 register transform supported");
    failures += ok(memcmp(state.base, base, LUT_SIZE) == 0 &&
                   memcmp(state.runtime, base, LUT_SIZE) != 0,
                   "P4 keeps base authoritative and derives runtime");
    failures += ok(same_transform(&state.transform, &first) &&
                   same_transform(&state.applied_transform, &first),
                   "P4 requested transform is applied transform");

    unsigned char first_runtime[LUT_SIZE];
    memcpy(first_runtime, state.runtime, LUT_SIZE);
    VbeOledTransformParams second;
    vbe_oled_transform_neutral(&second);
    second.bias.b_offset = -9;
    failures += ok(vbe_oled_state_derive(&state, state.base, &source,
                                         OLED_PANEL_4, &second) == VBE_OLED_TRANSFORM_OK,
                   "second P4 transform supported");
    failures += ok(memcmp(state.base, base, LUT_SIZE) == 0 &&
                   memcmp(state.runtime, first_runtime, LUT_SIZE) != 0,
                   "transform change rederives from A, never runtime-to-runtime");

    failures += ok(vbe_oled_state_derive(&state, base, &source,
                                         OLED_PANEL_5, &first) == VBE_OLED_TRANSFORM_OK &&
                   memcmp(state.runtime, base, LUT_SIZE) != 0,
                   "P5 register transform supported");

    failures += ok(vbe_oled_state_derive(&state, base, &source,
                                         OLED_PANEL_6, &first) == VBE_OLED_TRANSFORM_UNSUPPORTED,
                   "P6 non-neutral transform reports unsupported");
    failures += ok(memcmp(state.runtime, base, LUT_SIZE) == 0 &&
                   same_transform(&state.transform, &first) &&
                   same_transform(&state.applied_transform, &neutral),
                   "P6 retains requested bias but applies neutral/base runtime");

    failures += ok(vbe_oled_state_derive(&state, base, &source,
                                         OLED_PANEL_UNKNOWN, &first) == VBE_OLED_TRANSFORM_UNSUPPORTED,
                   "unknown panel non-neutral transform reports unsupported");
    failures += ok(memcmp(state.runtime, base, LUT_SIZE) == 0 &&
                   same_transform(&state.transform, &first) &&
                   same_transform(&state.applied_transform, &neutral),
                   "unknown panel remains healthy on base runtime with truthful applied state");

    VbeOledLutState committed;
    vbe_oled_state_derive(&committed, base, &source, OLED_PANEL_4, &first);
    VbeOledLutState rollback_snapshot = committed;
    vbe_oled_state_derive(&committed, base, &source, OLED_PANEL_4, &second);
    committed = rollback_snapshot;
    failures += ok(same_state(&committed, &rollback_snapshot),
                   "state copy rollback restores exact base/runtime/source/panel/transforms");

    if (failures) return 1;
    puts("OLED base/runtime ownership regressions: OK");
    return 0;
}
