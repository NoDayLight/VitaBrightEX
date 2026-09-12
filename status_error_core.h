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

static inline VbeErrorSlot vbe_error_state_get(const VbeErrorState *state,
                                                int domain) {
    VbeErrorSlot none = { VBE_ERR_NONE, 0 };
    if (domain < 0 || domain >= VBE_ERROR_DOMAIN_COUNT) return none;
    return state->slots[domain];
}

static inline VbeErrorSlot vbe_error_state_summary(const VbeErrorState *state) {
    VbeErrorSlot slot;
    slot = state->slots[VBE_ERROR_DOMAIN_SYNC];
    if (slot.error != VBE_ERR_NONE) return slot;
    slot = state->slots[VBE_ERROR_DOMAIN_BRIGHTNESS];
    if (slot.error != VBE_ERR_NONE) return slot;
    slot = state->slots[VBE_ERROR_DOMAIN_CONFIG];
    if (slot.error != VBE_ERR_NONE) return slot;
    slot = state->slots[VBE_ERROR_DOMAIN_COLOR_SPACE];
    if (slot.error != VBE_ERR_NONE) return slot;
    slot = state->slots[VBE_ERROR_DOMAIN_FILTER];
    if (slot.error != VBE_ERR_NONE) return slot;
    slot = state->slots[VBE_ERROR_DOMAIN_INPUT];
    if (slot.error != VBE_ERR_NONE) return slot;
    slot.error = VBE_ERR_NONE;
    slot.detail = 0;
    return slot;
}
