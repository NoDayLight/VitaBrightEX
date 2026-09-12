#pragma once

#include "status.h"

typedef struct {
    int error;
    int detail;
} VbeErrorSlot;

typedef struct {
    VbeErrorSlot slots[VBE_ERROR_DOMAIN_COUNT];
} VbeErrorState;

void vbe_error_state_init(VbeErrorState *state);
void vbe_error_state_set(VbeErrorState *state, int domain, int error, int detail);
void vbe_error_state_clear(VbeErrorState *state, int domain);
VbeErrorSlot vbe_error_state_get(const VbeErrorState *state, int domain);
VbeErrorSlot vbe_error_state_summary(const VbeErrorState *state);
