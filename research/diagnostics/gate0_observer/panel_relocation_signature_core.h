#pragma once
#include <stdint.h>
#include <stddef.h>

static inline uint16_t vbe_read_le16(const volatile uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline int vbe_decode_thumb_mov_imm16(
    const volatile uint8_t *insn,
    uint16_t opcode_class,
    unsigned expected_rd,
    uint16_t *imm16
) {
    uint16_t h1, h2;
    uint32_t imm4, i, imm3, imm8;
    if (!insn || !imm16 || expected_rd > 15u) return 0;
    h1 = vbe_read_le16(insn);
    h2 = vbe_read_le16(insn + 2);
    if ((h1 & 0xFBF0u) != opcode_class) return 0;
    if ((h2 & 0x8000u) != 0u) return 0;
    if (((unsigned)(h2 >> 8) & 0xFu) != expected_rd) return 0;
    imm4 = (uint32_t)(h1 & 0x000Fu);
    i = (uint32_t)((h1 >> 10) & 1u);
    imm3 = (uint32_t)((h2 >> 12) & 7u);
    imm8 = (uint32_t)(h2 & 0x00FFu);
    *imm16 = (uint16_t)((imm4 << 12) | (i << 11) | (imm3 << 8) | imm8);
    return 1;
}

static inline int vbe_decode_thumb_movw_movt_target(
    const volatile uint8_t *movw,
    const volatile uint8_t *movt,
    unsigned expected_rd,
    uint32_t *target
) {
    uint16_t lo, hi;
    if (!target) return 0;
    if (!vbe_decode_thumb_mov_imm16(movw, 0xF240u, expected_rd, &lo)) return 0;
    if (!vbe_decode_thumb_mov_imm16(movt, 0xF2C0u, expected_rd, &hi)) return 0;
    *target = ((uint32_t)hi << 16) | (uint32_t)lo;
    return 1;
}

static inline int vbe_segment_target32(
    uintptr_t base,
    uint32_t size,
    uint32_t *target
) {
    uintptr_t end;
    if (!target || base == (uintptr_t)0 || size == 0u) return 0;
    if (base > (uintptr_t)UINT32_MAX) return 0;
    end = base + (uintptr_t)size;
    if (end < base) return 0;
    *target = (uint32_t)base;
    return 1;
}

static inline int vbe_exact_bytes(
    const volatile uint8_t *actual,
    const uint8_t *expected,
    uint32_t n
) {
    uint32_t i;
    if (!actual || !expected) return 0;
    for (i = 0; i < n; i++) {
        if (actual[i] != expected[i]) return 0;
    }
    return 1;
}

static inline int vbe_relocation_normalized_signature(
    const volatile uint8_t *runtime_bytes,
    const uint8_t *static_signature,
    unsigned expected_rd,
    uint32_t runtime_segment1_base
) {
    uint32_t decoded_target;
    if (!runtime_bytes || !static_signature || runtime_segment1_base == 0u) return 0;
    if (!vbe_exact_bytes(runtime_bytes, static_signature, 4u)) return 0;
    if (!vbe_decode_thumb_movw_movt_target(runtime_bytes + 4, runtime_bytes + 8, expected_rd, &decoded_target)) return 0;
    if (decoded_target != runtime_segment1_base) return 0;
    if (!vbe_exact_bytes(runtime_bytes + 12, static_signature + 12, 4u)) return 0;
    return 1;
}
