#include "matrix_authority_core.h"

static void copy_object(VbeBStageObject *dst, const VbeBStageObject *src) {
    uint32_t i;
    for (i = 0; i < VBE_B_OBJECT_WORDS; ++i) dst->words[i] = src->words[i];
}

static void copy_authority(VbeNaturalBSource *dst,
                           const VbeNaturalBSource *src) {
    dst->valid = src->valid;
    dst->stale = src->stale;
    dst->natural_generation = src->natural_generation;
    dst->baseline_class = src->baseline_class;
    copy_object(&dst->source, &src->source);
}

void vbe_natural_authority_init(VbeNaturalBSource *state) {
    uint32_t i;
    if (!state) return;
    state->valid = 0u;
    state->stale = 0u;
    state->natural_generation = 0u;
    state->baseline_class = VBE_B_BASELINE_UNKNOWN;
    for (i = 0; i < VBE_B_OBJECT_WORDS; ++i) state->source.words[i] = 0u;
}

void vbe_natural_authority_mark_stale(VbeNaturalBSource *state) {
    if (!state) return;
    state->stale = 1u;
}

int vbe_natural_authority_after_call(const VbeNaturalBSource *current,
                                     VbeBCallOrigin origin,
                                     const VbeBStageObject *source,
                                     int sony_return,
                                     VbeNaturalBSource *next) {
    VbeNaturalBSource tmp;
    if (!current || !source || !next) return VBE_AUTHORITY_INVALID;
    copy_authority(&tmp, current);

    if (origin == VBE_B_ORIGIN_INTERNAL_REPLAY) {
        copy_authority(next, &tmp);
        return VBE_AUTHORITY_NO_CHANGE;
    }
    if (origin != VBE_B_ORIGIN_NATURAL_SONY) return VBE_AUTHORITY_INVALID;

    /* A failed Sony setter did not establish new authoritative hardware state. */
    if (sony_return < 0) {
        copy_authority(next, &tmp);
        return VBE_AUTHORITY_NO_CHANGE;
    }
    if (tmp.natural_generation >= VBE_NATURAL_GENERATION_MAX) {
        tmp.stale = 1u;
        copy_authority(next, &tmp);
        return VBE_AUTHORITY_OVERFLOW;
    }

    tmp.valid = 1u;
    tmp.stale = 0u;
    tmp.natural_generation += 1u;
    tmp.baseline_class = vbe_b_baseline_classify(source);
    copy_object(&tmp.source, source);
    copy_authority(next, &tmp);
    return VBE_AUTHORITY_OK;
}

int vbe_natural_authority_seed(const VbeNaturalBSource *state,
                               VbeBStageObject *seed) {
    const VbeBStageObject *canonical;
    if (!state || !seed) return VBE_AUTHORITY_INVALID;
    if (!state->valid) return VBE_AUTHORITY_NO_SOURCE;
    if (state->stale) return VBE_AUTHORITY_STALE;

    switch (state->baseline_class) {
    case VBE_B_BASELINE_CANONICAL_IDENTITY:
        canonical = &vbe_b_baseline_identity;
        break;
    case VBE_B_BASELINE_SONY_FULL_TO_LIMITED:
        canonical = &vbe_b_baseline_full_to_limited;
        break;
    default:
        return VBE_AUTHORITY_UNSUPPORTED;
    }

    /* Chain-head replay is authorized only for exact canonical Sony inputs. */
    if (!vbe_b_object_equal(&state->source, canonical))
        return VBE_AUTHORITY_STALE;
    copy_object(seed, canonical);
    return VBE_AUTHORITY_OK;
}
