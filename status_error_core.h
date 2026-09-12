#pragma once

#include "status.h"

typedef struct {
    int error;
    int detail;
} VbeErrorSlot;

typedef struct {
    VbeErrorSlot slots[VBE_ERROR_DOMAIN_COUNT];
} VbeErrorState;

static inline void vbe_error_state_init(VbeErrorState *state) {
    int i;
    for (i = 0; i < VBE_ERROR_DOMAIN_COUNT; ++i) {
        state->slots[i].error = VBE_ERR_NONE;
        state->slots[i].detail = 0;
    }
}

static inline void vbe_error_state_set(VbeErrorState *state, int domain,
                                       int error, int detail) {
    if (domain < 0 || domain >= VBE_ERROR_DOMAIN_COUNT) return;
    state->slots[domain].error = error;
    state->slots[domain].detail = detail;
}

static inline void vbe_error_state_clear(VbeErrorState *state, int domain) {
    if (domain < 0 || domain >= VBE_ERROR_DOMAIN_COUNT) return;
    state->slots[domain].error = VBE_ERR_NONE;
    state->slots[domain].detail = 0;
}

static inline void vbe_error_state_stage(VbeErrorState *state, int domain,
                                         int succeeded, int error, int detail) {
    if (succeeded) vbe_error_state_clear(state, domain);
    else vbe_error_state_set(state, domain, error, detail);
}

static inline VbeErrorSlot vbe_error_state_get(const VbeErrorState *state,
                                                int domain) {
    VbeErrorSlot none = { VBE_ERR_NONE, 0 };
    if (domain < 0 || domain >= VBE_ERROR_DOMAIN_COUNT) return none;
    return state->slots[domain];
}

static inline void vbe_error_state_restore(VbeErrorState *state, int domain,
                                           VbeErrorSlot slot) {
    if (domain < 0 || domain >= VBE_ERROR_DOMAIN_COUNT) return;
    state->slots[domain] = slot;
}

/* A successful recovery restores the requested-operation failure because the
 * requested mutation still failed even though the previous committed state is
 * operational again. A failed recovery supersedes it with the more severe
 * recovery error. */
static inline void vbe_error_state_recovery(VbeErrorState *state, int domain,
                                            int succeeded,
                                            VbeErrorSlot requested,
                                            VbeErrorSlot recovery_failure) {
    if (succeeded) vbe_error_state_restore(state, domain, requested);
    else vbe_error_state_restore(state, domain, recovery_failure);
}

/* Intentional ABI-v2 compatibility-summary precedence. Diagnostics ABI v1 is
 * the complete truth; this order only chooses which one appears in the legacy
 * last_error/detail fields:
 * SYNC > BRIGHTNESS > CONFIG > COLOR_SPACE > FILTER > INPUT. */
static inline VbeErrorSlot vbe_error_state_summary(const VbeErrorState *state) {
    static const int order[] = {
        VBE_ERROR_DOMAIN_SYNC,
        VBE_ERROR_DOMAIN_BRIGHTNESS,
        VBE_ERROR_DOMAIN_CONFIG,
        VBE_ERROR_DOMAIN_COLOR_SPACE,
        VBE_ERROR_DOMAIN_FILTER,
        VBE_ERROR_DOMAIN_INPUT,
    };
    unsigned int i;
    for (i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        VbeErrorSlot slot = state->slots[order[i]];
        if (slot.error != VBE_ERR_NONE) return slot;
    }
    {
        VbeErrorSlot none = { VBE_ERR_NONE, 0 };
        return none;
    }
}
