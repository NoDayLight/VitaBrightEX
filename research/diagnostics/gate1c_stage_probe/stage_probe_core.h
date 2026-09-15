#pragma once
#include <stdint.h>

#define VBE_G1C_CSC_SIZE 0x3Cu
#define VBE_G1C_STAGE_A 0u
#define VBE_G1C_STAGE_B 1u
#define VBE_G1C_PROBE_WORD_INDEX 6u
#define VBE_G1C_PROBE_WORD_VALUE 0x00000100u

typedef enum VbeG1cPrepareResult {
    VBE_G1C_PREP_OUTSIDE_PLANE = 0,
    VBE_G1C_PREP_NULL = 1,
    VBE_G1C_PREP_A_OBSERVE = 2,
    VBE_G1C_PREP_B_TRANSFORM = 3,
    VBE_G1C_PREP_B_BASELINE_MISMATCH = 4
} VbeG1cPrepareResult;

static const uint32_t vbe_g1c_canonical_b_words[15] = {
    0x00000000u, 0x00000000u, 0x000003FFu, 0x00000000u, 0x000003FFu,
    0x00000000u, 0x00000200u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000200u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000200u
};

static inline void vbe_g1c_copy_3c(void *dst, const void *src) {
    uint32_t i;
    uint8_t *d = (uint8_t *)dst;
    const volatile uint8_t *s = (const volatile uint8_t *)src;
    for (i = 0; i < VBE_G1C_CSC_SIZE; ++i) d[i] = s[i];
}

static inline int vbe_g1c_equal_3c(const void *a, const void *b) {
    uint32_t i;
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    uint8_t diff = 0u;
    for (i = 0; i < VBE_G1C_CSC_SIZE; ++i) diff |= (uint8_t)(x[i] ^ y[i]);
    return diff == 0u;
}

static inline uint32_t vbe_g1c_hash32_3c(const void *src) {
    uint32_t i, h = 2166136261u;
    const uint8_t *p = (const uint8_t *)src;
    for (i = 0; i < VBE_G1C_CSC_SIZE; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static inline void vbe_g1c_write_u32_le(void *dst, uint32_t word_index, uint32_t value) {
    uint8_t *p = (uint8_t *)dst + word_index * 4u;
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

/*
 * Production-shared safety contract:
 * - NULL and non-target planes return before touching Sony memory.
 * - Target-plane non-NULL input is read exactly once into source_snapshot.
 * - Stage A is observational only and always forwards Sony's original pointer.
 * - Stage B is transformed only if Sony's source is the exact canonical B object.
 * - Any B baseline drift fails open to Sony's original pointer.
 */
static inline VbeG1cPrepareResult vbe_g1c_prepare(
    uint32_t stage,
    int target_plane,
    const void *src,
    void *source_snapshot,
    void *owned_copy
) {
    if (!src) return VBE_G1C_PREP_NULL;
    if (!target_plane) return VBE_G1C_PREP_OUTSIDE_PLANE;

    vbe_g1c_copy_3c(source_snapshot, src);
    vbe_g1c_copy_3c(owned_copy, source_snapshot);

    if (stage == VBE_G1C_STAGE_A) return VBE_G1C_PREP_A_OBSERVE;
    if (stage != VBE_G1C_STAGE_B) return VBE_G1C_PREP_B_BASELINE_MISMATCH;

    if (!vbe_g1c_equal_3c(source_snapshot, vbe_g1c_canonical_b_words))
        return VBE_G1C_PREP_B_BASELINE_MISMATCH;

    vbe_g1c_write_u32_le(owned_copy, VBE_G1C_PROBE_WORD_INDEX, VBE_G1C_PROBE_WORD_VALUE);
    return VBE_G1C_PREP_B_TRANSFORM;
}

static inline const void *vbe_g1c_forward_pointer(
    VbeG1cPrepareResult prep,
    const void *original_source,
    const void *owned_copy
) {
    return prep == VBE_G1C_PREP_B_TRANSFORM ? owned_copy : original_source;
}
