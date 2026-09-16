#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../matrix_backend_core.h"

static VbeMatrixS39 diagonal(int32_t a, int32_t b, int32_t c) {
    VbeMatrixS39 m = {{a,0,0,0,b,0,0,0,c}};
    return m;
}

static void require_first_six_equal(const VbeBStageObject *a,
                                    const VbeBStageObject *b) {
    unsigned i;
    for (i = 0; i < 6u; ++i) assert(a->words[i] == b->words[i]);
}

int main(void) {
    uint32_t raw;
    int32_t n;
    unsigned i;
    VbeMatrixS39 identity, probe, mild, outm, tie_l, tie_r;
    VbeBStageObject out, unknown;
    VbeBBaselineClass cls;

    assert(sizeof(VbeBStageObject) == 60u);
    vbe_matrix_identity(&identity);

    for (i = 0; i < 4096u; ++i) {
        assert(vbe_s39_decode_word(i, &n) == 0);
        assert(vbe_s39_encode_word(n, &raw) == 0);
        assert(raw == i);
    }
    assert(vbe_s39_decode_word(0x1000u, &n) < 0);
    assert(vbe_s39_encode_word(-2049, &raw) == VBE_MATRIX_CORE_OVERFLOW);
    assert(vbe_s39_encode_word(2048, &raw) == VBE_MATRIX_CORE_OVERFLOW);
    assert(vbe_s39_encode_word(512, &raw) == 0 && raw == 0x200u);
    assert(vbe_s39_encode_word(-512, &raw) == 0 && raw == 0xE00u);

    assert(vbe_b_baseline_classify(&vbe_b_baseline_identity) ==
           VBE_B_BASELINE_CANONICAL_IDENTITY);
    assert(vbe_b_baseline_classify(&vbe_b_baseline_full_to_limited) ==
           VBE_B_BASELINE_SONY_FULL_TO_LIMITED);
    assert(vbe_b_object_fnv1a(&vbe_b_baseline_identity) == 0x65B3C48Bu);
    assert(vbe_b_object_fnv1a(&vbe_b_baseline_full_to_limited) == 0x3C76094Du);

    assert(vbe_b_object_compose(&vbe_b_baseline_identity, &identity,
                                &out, &cls) == 0);
    assert(cls == VBE_B_BASELINE_CANONICAL_IDENTITY);
    assert(vbe_b_object_equal(&out, &vbe_b_baseline_identity));

    probe = diagonal(256, 512, 512);
    assert(vbe_b_object_compose(&vbe_b_baseline_identity, &probe,
                                &out, &cls) == 0);
    assert(out.words[6] == 0x100u);
    assert(out.words[10] == 0x200u && out.words[14] == 0x200u);
    require_first_six_equal(&out, &vbe_b_baseline_identity);
    assert(vbe_b_object_fnv1a(&out) == 0x80C25656u);
    puts("GATE1C_EXACT_REPRODUCTION=PASS");

    mild = diagonal(461, 512, 512);
    assert(vbe_b_object_compose(&vbe_b_baseline_identity, &mild,
                                &out, &cls) == 0);
    assert(out.words[6] == 461u);
    assert(vbe_b_object_fnv1a(&out) == 0x91533EE7u);
    require_first_six_equal(&out, &vbe_b_baseline_identity);
    puts("IDENTITY_BASELINE_MILD_COMPOSITION=PASS");

    assert(vbe_b_object_compose(&vbe_b_baseline_full_to_limited, &identity,
                                &out, &cls) == 0);
    assert(vbe_b_object_equal(&out, &vbe_b_baseline_full_to_limited));
    puts("RANGE_BASELINE_IDENTITY_INVARIANCE=PASS");

    assert(vbe_b_object_compose(&vbe_b_baseline_full_to_limited, &mild,
                                &out, &cls) == 0);
    assert(cls == VBE_B_BASELINE_SONY_FULL_TO_LIMITED);
    assert(out.words[6] == 395u); /* round((439*461)/512) */
    assert(out.words[10] == 439u && out.words[14] == 439u);
    assert(out.words[0] == 64u && out.words[1] == 64u);
    assert(out.words[2] == 940u && out.words[3] == 64u);
    assert(out.words[4] == 940u && out.words[5] == 64u);
    require_first_six_equal(&out, &vbe_b_baseline_full_to_limited);
    assert(vbe_b_object_fnv1a(&out) == 0x69152279u);
    puts("RANGE_BASELINE_MILD_COMPOSITION=PASS");

    unknown = vbe_b_baseline_identity;
    unknown.words[0] ^= 1u;
    memset(&out, 0xA5, sizeof(out));
    assert(vbe_b_object_compose(&unknown, &mild, &out, &cls) ==
           VBE_MATRIX_CORE_UNKNOWN_BASELINE);
    assert(cls == VBE_B_BASELINE_UNKNOWN);
    puts("UNKNOWN_BASELINE_REJECT=PASS");

    tie_l = diagonal(1, 1, 1);
    tie_r = diagonal(256, -256, 256);
    assert(vbe_matrix_multiply(&tie_l, &tie_r, &outm) == 0);
    assert(outm.v[0] == 1 && outm.v[4] == -1 && outm.v[8] == 1);
    puts("ROUND_TIES_AWAY_FROM_ZERO=PASS");

    for (i = 0; i < 9u; ++i) tie_l.v[i] = 2047;
    for (i = 0; i < 9u; ++i) tie_r.v[i] = 2047;
    assert(vbe_matrix_multiply(&tie_l, &tie_r, &outm) ==
           VBE_MATRIX_CORE_OVERFLOW);
    puts("COEFFICIENT_OVERFLOW_REJECT=PASS");

    assert(vbe_matrix_validate(&mild) == 0);
    mild.v[3] = 2048;
    assert(vbe_matrix_validate(&mild) == VBE_MATRIX_CORE_OVERFLOW);

    puts("S39_EXHAUSTIVE_ROUNDTRIP=PASS count=4096");
    puts("NON_CTM_SONY_WORDS_PRESERVED=PASS");
    puts("MATRIX_BACKEND_CORE_HOST=PASS");
    return 0;
}
