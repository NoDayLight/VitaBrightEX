#pragma once
#include <stdint.h>
#include "matrix_backend_core.h"

#define VBE_MATRIX_POLICY_READ_RETRIES 8u
#define VBE_MATRIX_GENERATION_MAX 0x7FFFFFFFu

typedef struct {
    volatile uint32_t seq;
    uint32_t generation;
    uint32_t enabled;
    int32_t matrix[VBE_MATRIX_COEFFS];
} VbeMatrixPolicySlot;

typedef struct {
    VbeMatrixPolicySlot slots[2];
    volatile uint32_t token;
} VbeMatrixPolicyStore;

typedef struct {
    uint32_t generation;
    uint32_t enabled;
    VbeMatrixS39 matrix;
} VbeMatrixPolicySnapshot;

void vbe_matrix_policy_init(VbeMatrixPolicyStore *store);
int vbe_matrix_policy_publish(VbeMatrixPolicyStore *store,
                              const VbeMatrixS39 *matrix,
                              uint32_t enabled,
                              uint32_t *published_generation);
int vbe_matrix_policy_read(const VbeMatrixPolicyStore *store,
                           VbeMatrixPolicySnapshot *out);
