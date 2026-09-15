#pragma once
#include <stdint.h>

#define VBE_G1_CSC_SIZE 0x3Cu

typedef enum VbeG1IdentityPrepareResult {
    VBE_G1_PREP_INVALID_PLANE = 0,
    VBE_G1_PREP_NULL = 1,
    VBE_G1_PREP_SUBSTITUTE = 2,
    VBE_G1_PREP_MISMATCH = 3
} VbeG1IdentityPrepareResult;

static inline void vbe_g1_copy_3c(void *dst, const void *src) {
    uint32_t i;
    uint8_t *d = (uint8_t *)dst;
    const volatile uint8_t *s = (const volatile uint8_t *)src;
    for (i = 0; i < VBE_G1_CSC_SIZE; ++i) d[i] = s[i];
}

static inline int vbe_g1_equal_3c(const void *a, const void *b) {
    uint32_t i;
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    uint8_t diff = 0;
    for (i = 0; i < VBE_G1_CSC_SIZE; ++i) diff |= (uint8_t)(x[i] ^ y[i]);
    return diff == 0;
}

static inline uint32_t vbe_g1_hash32_3c(const void *src) {
    uint32_t i, h = 2166136261u;
    const uint8_t *p = (const uint8_t *)src;
    for (i = 0; i < VBE_G1_CSC_SIZE; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/*
 * Production-shared pointer-safety contract:
 * - NULL: return before touching src
 * - invalid plane + non-NULL: return before touching src
 * - valid non-NULL: read Sony memory exactly once into snapshot, then work locally
 */
static inline VbeG1IdentityPrepareResult vbe_g1_prepare_identity(
    int valid_plane,
    const void *src,
    void *source_snapshot,
    void *owned_copy
) {
    if (!src) return VBE_G1_PREP_NULL;
    if (!valid_plane) return VBE_G1_PREP_INVALID_PLANE;
    vbe_g1_copy_3c(source_snapshot, src);
    vbe_g1_copy_3c(owned_copy, source_snapshot);
    return vbe_g1_equal_3c(source_snapshot, owned_copy)
        ? VBE_G1_PREP_SUBSTITUTE
        : VBE_G1_PREP_MISMATCH;
}

/* Gate-1A changes pointer identity only after local byte identity is proven. */
static inline const void *vbe_g1_forward_pointer(
    VbeG1IdentityPrepareResult prep,
    const void *original_source,
    const void *owned_copy
) {
    return prep == VBE_G1_PREP_SUBSTITUTE ? owned_copy : original_source;
}
