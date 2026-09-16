#pragma once
#include <stdint.h>
#include "matrix_backend_core.h"

#define VBE_NATURAL_GENERATION_MAX 0x7FFFFFFFu

typedef enum {
    VBE_B_ORIGIN_NATURAL_SONY = 1,
    VBE_B_ORIGIN_INTERNAL_REPLAY = 2,
} VbeBCallOrigin;

typedef enum {
    VBE_AUTHORITY_OK = 0,
    VBE_AUTHORITY_NO_CHANGE = 1,
    VBE_AUTHORITY_NO_SOURCE = -1,
    VBE_AUTHORITY_STALE = -2,
    VBE_AUTHORITY_UNSUPPORTED = -3,
    VBE_AUTHORITY_OVERFLOW = -4,
    VBE_AUTHORITY_INVALID = -5,
} VbeAuthorityResult;

typedef struct {
    uint32_t valid;
    uint32_t stale;
    uint32_t natural_generation;
    VbeBBaselineClass baseline_class;
    VbeBStageObject source;
} VbeNaturalBSource;

void vbe_natural_authority_init(VbeNaturalBSource *state);
void vbe_natural_authority_mark_stale(VbeNaturalBSource *state);

/*
 * Produces the next authority snapshot after one B-setter invocation.
 * INTERNAL_REPLAY never mutates authority. A natural call becomes authority
 * only after the downstream Sony path succeeds.
 */
int vbe_natural_authority_after_call(const VbeNaturalBSource *current,
                                     VbeBCallOrigin origin,
                                     const VbeBStageObject *source,
                                     int sony_return,
                                     VbeNaturalBSource *next);

/*
 * Returns an exact canonical Sony object suitable for chain-head replay.
 * Unknown, missing or stale authority is never substituted with identity.
 */
int vbe_natural_authority_seed(const VbeNaturalBSource *state,
                               VbeBStageObject *seed);
