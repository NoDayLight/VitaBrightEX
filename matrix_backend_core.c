#include "matrix_backend_core.h"

const VbeBStageObject vbe_b_baseline_identity = {{
    0x00000000u, 0x00000000u, 0x000003FFu,
    0x00000000u, 0x000003FFu, 0x00000000u,
    0x00000200u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000200u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000200u,
}};

const VbeBStageObject vbe_b_baseline_full_to_limited = {{
    0x00000040u, 0x00000040u, 0x000003ACu,
    0x00000040u, 0x000003ACu, 0x00000040u,
    0x000001B7u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x000001B7u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x000001B7u,
}};

const char vbe_b_baseline_identity_sha256[] =
    "5dc12dfcae42a648dc093db831b661cc3068f2d70f199b03fb582ce301b188b5";
const char vbe_b_baseline_full_to_limited_sha256[] =
    "fff18ae2defe21c92e9a4e1e4795c85bfe6ba7f5548a1b90a2e1b46c707ee07e";

static int32_t round_div_512_away(int64_t n) {
    uint64_t mag;
    int32_t q;
    if (n < 0) {
        mag = (uint64_t)(-n);
        q = (int32_t)((mag + 256u) / 512u);
        return -q;
    }
    return (int32_t)(((uint64_t)n + 256u) / 512u);
}

void vbe_matrix_identity(VbeMatrixS39 *out) {
    uint32_t i;
    if (!out) return;
    for (i = 0; i < VBE_MATRIX_COEFFS; ++i) out->v[i] = 0;
    out->v[0] = VBE_S39_SCALE;
    out->v[4] = VBE_S39_SCALE;
    out->v[8] = VBE_S39_SCALE;
}

int vbe_s39_decode_word(uint32_t raw, int32_t *numerator) {
    int32_t n;
    if (!numerator || (raw & ~0xFFFu) != 0u) return VBE_MATRIX_CORE_INVALID;
    n = (int32_t)(raw & 0xFFFu);
    if ((n & 0x800) != 0) n -= 0x1000;
    *numerator = n;
    return VBE_MATRIX_CORE_OK;
}

int vbe_s39_encode_word(int32_t numerator, uint32_t *raw) {
    if (!raw || numerator < VBE_S39_MIN || numerator > VBE_S39_MAX)
        return VBE_MATRIX_CORE_OVERFLOW;
    *raw = (uint32_t)numerator & 0xFFFu;
    return VBE_MATRIX_CORE_OK;
}

int vbe_matrix_validate(const VbeMatrixS39 *m) {
    uint32_t i;
    if (!m) return VBE_MATRIX_CORE_INVALID;
    for (i = 0; i < VBE_MATRIX_COEFFS; ++i) {
        if (m->v[i] < VBE_S39_MIN || m->v[i] > VBE_S39_MAX)
            return VBE_MATRIX_CORE_OVERFLOW;
    }
    return VBE_MATRIX_CORE_OK;
}

int vbe_matrix_multiply(const VbeMatrixS39 *left,
                        const VbeMatrixS39 *right,
                        VbeMatrixS39 *out) {
    uint32_t row, col, k;
    VbeMatrixS39 tmp;
    if (!left || !right || !out) return VBE_MATRIX_CORE_INVALID;
    if (vbe_matrix_validate(left) < 0 || vbe_matrix_validate(right) < 0)
        return VBE_MATRIX_CORE_INVALID;
    for (row = 0; row < VBE_MATRIX_DIM; ++row) {
        for (col = 0; col < VBE_MATRIX_DIM; ++col) {
            int64_t acc = 0;
            int32_t q;
            for (k = 0; k < VBE_MATRIX_DIM; ++k)
                acc += (int64_t)left->v[row * 3u + k] *
                       (int64_t)right->v[k * 3u + col];
            q = round_div_512_away(acc);
            if (q < VBE_S39_MIN || q > VBE_S39_MAX)
                return VBE_MATRIX_CORE_OVERFLOW;
            tmp.v[row * 3u + col] = q;
        }
    }
    *out = tmp;
    return VBE_MATRIX_CORE_OK;
}

int vbe_b_object_equal(const VbeBStageObject *a, const VbeBStageObject *b) {
    uint32_t i;
    uint32_t diff = 0;
    if (!a || !b) return 0;
    for (i = 0; i < VBE_B_OBJECT_WORDS; ++i) diff |= a->words[i] ^ b->words[i];
    return diff == 0u;
}

VbeBBaselineClass vbe_b_baseline_classify(const VbeBStageObject *obj) {
    if (!obj) return VBE_B_BASELINE_UNKNOWN;
    if (vbe_b_object_equal(obj, &vbe_b_baseline_identity))
        return VBE_B_BASELINE_CANONICAL_IDENTITY;
    if (vbe_b_object_equal(obj, &vbe_b_baseline_full_to_limited))
        return VBE_B_BASELINE_SONY_FULL_TO_LIMITED;
    return VBE_B_BASELINE_UNKNOWN;
}

int vbe_b_object_compose(const VbeBStageObject *sony,
                         const VbeMatrixS39 *user,
                         VbeBStageObject *out,
                         VbeBBaselineClass *baseline_class) {
    uint32_t i;
    VbeMatrixS39 sony_matrix;
    VbeMatrixS39 composed;
    VbeBBaselineClass cls;
    int ret;
    if (!sony || !user || !out) return VBE_MATRIX_CORE_INVALID;
    cls = vbe_b_baseline_classify(sony);
    if (baseline_class) *baseline_class = cls;
    if (cls == VBE_B_BASELINE_UNKNOWN) return VBE_MATRIX_CORE_UNKNOWN_BASELINE;
    if (vbe_matrix_validate(user) < 0) return VBE_MATRIX_CORE_OVERFLOW;

    for (i = 0; i < VBE_B_OBJECT_WORDS; ++i) out->words[i] = sony->words[i];
    for (i = 0; i < VBE_MATRIX_COEFFS; ++i) {
        ret = vbe_s39_decode_word(sony->words[VBE_B_CTM_WORD_FIRST + i],
                                  &sony_matrix.v[i]);
        if (ret < 0) return ret;
    }
    ret = vbe_matrix_multiply(&sony_matrix, user, &composed);
    if (ret < 0) return ret;
    for (i = 0; i < VBE_MATRIX_COEFFS; ++i) {
        ret = vbe_s39_encode_word(composed.v[i],
                                  &out->words[VBE_B_CTM_WORD_FIRST + i]);
        if (ret < 0) return ret;
    }
    return VBE_MATRIX_CORE_OK;
}

uint32_t vbe_b_object_fnv1a(const VbeBStageObject *obj) {
    uint32_t h = 2166136261u;
    const uint8_t *p;
    uint32_t i;
    if (!obj) return 0u;
    p = (const uint8_t *)obj;
    for (i = 0; i < VBE_B_OBJECT_SIZE; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

void vbe_b_object_copy_from_volatile(VbeBStageObject *dst,
                                     const volatile void *src) {
    uint32_t i;
    uint8_t *d;
    const volatile uint8_t *s;
    if (!dst || !src) return;
    d = (uint8_t *)dst;
    s = (const volatile uint8_t *)src;
    for (i = 0; i < VBE_B_OBJECT_SIZE; ++i) d[i] = s[i];
}
