#include <assert.h>
#include <stdio.h>
#include "matrix_authority_core.h"

static void copy_obj(VbeBStageObject *dst, const VbeBStageObject *src) {
    unsigned i;
    for (i = 0; i < VBE_B_OBJECT_WORDS; ++i) dst->words[i] = src->words[i];
}

int main(void) {
    VbeNaturalBSource a, next;
    VbeBStageObject seed, unknown, mild_range;
    VbeMatrixS39 mild;
    VbeBBaselineClass cls;
    unsigned i;

    vbe_natural_authority_init(&a);
    assert(a.valid == 0u && a.natural_generation == 0u);
    assert(vbe_natural_authority_seed(&a, &seed) == VBE_AUTHORITY_NO_SOURCE);

    /* Permanent regression: internal replay is never fake pristine evidence. */
    for (i = 0; i < 100u; ++i) {
        assert(vbe_natural_authority_after_call(&a,
                VBE_B_ORIGIN_INTERNAL_REPLAY,
                &vbe_b_baseline_identity, 0, &next) == VBE_AUTHORITY_NO_CHANGE);
        assert(next.natural_generation == a.natural_generation);
        assert(next.valid == a.valid);
        a = next;
    }
    assert(a.natural_generation == 0u);

    assert(vbe_natural_authority_after_call(&a,
            VBE_B_ORIGIN_NATURAL_SONY,
            &vbe_b_baseline_identity, 0, &next) == VBE_AUTHORITY_OK);
    a = next;
    assert(a.valid == 1u && a.stale == 0u && a.natural_generation == 1u);
    assert(a.baseline_class == VBE_B_BASELINE_CANONICAL_IDENTITY);
    assert(vbe_natural_authority_seed(&a, &seed) == VBE_AUTHORITY_OK);
    assert(vbe_b_object_equal(&seed, &vbe_b_baseline_identity));

    for (i = 0; i < 100u; ++i) {
        assert(vbe_natural_authority_after_call(&a,
                VBE_B_ORIGIN_INTERNAL_REPLAY,
                &vbe_b_baseline_identity, 0, &next) == VBE_AUTHORITY_NO_CHANGE);
        a = next;
    }
    assert(a.natural_generation == 1u);

    assert(vbe_natural_authority_after_call(&a,
            VBE_B_ORIGIN_NATURAL_SONY,
            &vbe_b_baseline_full_to_limited, 0, &next) == VBE_AUTHORITY_OK);
    a = next;
    assert(a.natural_generation == 2u);
    assert(a.baseline_class == VBE_B_BASELINE_SONY_FULL_TO_LIMITED);
    assert(vbe_natural_authority_seed(&a, &seed) == VBE_AUTHORITY_OK);
    assert(vbe_b_object_equal(&seed, &vbe_b_baseline_full_to_limited));

    /* Host-authorize the range baseline immediate composition. */
    vbe_matrix_identity(&mild);
    mild.v[0] = 461;
    assert(vbe_b_object_compose(&seed, &mild, &mild_range, &cls) == VBE_MATRIX_CORE_OK);
    assert(cls == VBE_B_BASELINE_SONY_FULL_TO_LIMITED);
    assert(mild_range.words[6] == 395u);
    for (i = 0; i < 6u; ++i)
        assert(mild_range.words[i] == vbe_b_baseline_full_to_limited.words[i]);

    /* Unknown natural Sony state is recorded, but never replay-authorized. */
    copy_obj(&unknown, &vbe_b_baseline_identity);
    unknown.words[6] = 511u;
    assert(vbe_natural_authority_after_call(&a,
            VBE_B_ORIGIN_NATURAL_SONY, &unknown, 0, &next) == VBE_AUTHORITY_OK);
    a = next;
    assert(a.natural_generation == 3u);
    assert(a.baseline_class == VBE_B_BASELINE_UNKNOWN);
    assert(vbe_natural_authority_seed(&a, &seed) == VBE_AUTHORITY_UNSUPPORTED);

    /* Failed Sony calls do not establish new authority. */
    assert(vbe_natural_authority_after_call(&a,
            VBE_B_ORIGIN_NATURAL_SONY,
            &vbe_b_baseline_identity, -1, &next) == VBE_AUTHORITY_NO_CHANGE);
    assert(next.natural_generation == 3u);

    vbe_natural_authority_mark_stale(&a);
    assert(vbe_natural_authority_seed(&a, &seed) == VBE_AUTHORITY_STALE);

    puts("GATE1E_AUTHORITY_INTERNAL_100_NO_ADVANCE=PASS");
    puts("GATE1E_AUTHORITY_NATURAL_ADVANCE=PASS");
    puts("GATE1E_RANGE_SEED_COMPOSITION=PASS");
    puts("GATE1E_UNKNOWN_BASELINE_NOT_REPLAYED=PASS");
    return 0;
}
