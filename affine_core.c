#include "affine_core.h"
#include <limits.h>

#ifndef INT32_MAX
#define INT32_MAX 2147483647
#define INT32_MIN (-INT32_MAX - 1)
#endif

typedef struct {
    int kelvin;
    int32_t r;
    int32_t g;
    int32_t b;
} CctAnchor;

/* Offline-precomputed code-space RGB white-balance gains. This is an
 * intentionally modest code-value approximation, not a claim of linear-light
 * chromatic adaptation. 6500 K is an explicit exact identity anchor. */
static const CctAnchor k_cct[] = {
    { 1000, 65536, 17517,     0 },
    { 1500, 65536, 27919,     0 },
    { 2000, 65536, 35299,  3644 },
    { 2500, 65536, 41023, 18365 },
    { 3000, 65536, 45701, 28809 },
    { 3500, 65536, 49655, 36911 },
    { 4000, 65536, 53081, 43530 },
    { 4500, 65536, 56102, 49126 },
    { 5000, 65536, 58805, 53974 },
    { 5500, 65536, 61250, 58251 },
    { 6000, 65536, 63483, 62076 },
    { 6500, 65536, 65536, 65536 },
    { 7000, 62352, 62448, 66836 },
    { 8000, 56853, 59264, 66836 },
    { 9000, 53864, 57476, 66836 },
    {10000, 51839, 56241, 66836 },
    {12000, 49113, 54545, 66836 },
    {15000, 46531, 52901, 66836 },
    {18000, 44782, 51764, 66836 },
    {22000, 43098, 50651, 66836 },
    {25100, 42093, 49978, 66836 },
};

static int qmul(int32_t a, int32_t b, int32_t *out) {
    int64_t product = (int64_t)a * (int64_t)b;
    int64_t scaled = product / (int64_t)VBE_AFFINE_ONE;
    if (scaled < INT32_MIN || scaled > INT32_MAX) return -1;
    *out = (int32_t)scaled;
    return 0;
}

static int add_checked(int64_t value, int32_t *out) {
    if (value < INT32_MIN || value > INT32_MAX) return -1;
    *out = (int32_t)value;
    return 0;
}

void vbe_affine_identity(VbeAffineTransform *out) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c)
            out->m[r][c] = r == c ? VBE_AFFINE_ONE : 0;
        out->b[r] = 0;
    }
}

int vbe_affine_is_identity(const VbeAffineTransform *t) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            int32_t expected = r == c ? VBE_AFFINE_ONE : 0;
            if (t->m[r][c] != expected) return 0;
        }
        if (t->b[r] != 0) return 0;
    }
    return 1;
}

int vbe_affine_compose(VbeAffineTransform *out,
                       const VbeAffineTransform *after,
                       const VbeAffineTransform *before) {
    VbeAffineTransform tmp;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            int64_t sum = 0;
            for (int k = 0; k < 3; ++k) {
                int32_t term;
                if (qmul(after->m[r][k], before->m[k][c], &term) < 0) return -1;
                sum += term;
            }
            if (add_checked(sum, &tmp.m[r][c]) < 0) return -1;
        }

        int64_t offset = after->b[r];
        for (int k = 0; k < 3; ++k) {
            int32_t term;
            if (qmul(after->m[r][k], before->b[k], &term) < 0) return -1;
            offset += term;
        }
        if (add_checked(offset, &tmp.b[r]) < 0) return -1;
    }
    *out = tmp;
    return 0;
}

static int32_t interpolate(int32_t a, int32_t b, int num, int den) {
    int64_t delta = (int64_t)b - a;
    return (int32_t)((int64_t)a + delta * num / den);
}

int vbe_affine_cct(VbeAffineTransform *out, int kelvin) {
    if (kelvin < 1000 || kelvin > 25100) return -1;
    vbe_affine_identity(out);
    if (kelvin == 6500) return 0;

    unsigned int hi = 1;
    while (hi < sizeof(k_cct) / sizeof(k_cct[0]) && k_cct[hi].kelvin < kelvin)
        ++hi;
    if (hi >= sizeof(k_cct) / sizeof(k_cct[0])) hi = sizeof(k_cct) / sizeof(k_cct[0]) - 1;
    unsigned int lo = hi - 1;
    if (kelvin == k_cct[hi].kelvin) lo = hi;

    int32_t r = k_cct[hi].r;
    int32_t g = k_cct[hi].g;
    int32_t b = k_cct[hi].b;
    if (lo != hi) {
        int den = k_cct[hi].kelvin - k_cct[lo].kelvin;
        int num = kelvin - k_cct[lo].kelvin;
        r = interpolate(k_cct[lo].r, k_cct[hi].r, num, den);
        g = interpolate(k_cct[lo].g, k_cct[hi].g, num, den);
        b = interpolate(k_cct[lo].b, k_cct[hi].b, num, den);
    }
    out->m[0][0] = r;
    out->m[1][1] = g;
    out->m[2][2] = b;
    return 0;
}

int vbe_affine_contrast(VbeAffineTransform *out,
                        int32_t contrast_q16, int32_t pivot_q16) {
    if (contrast_q16 < 0 || contrast_q16 > 4 * VBE_AFFINE_ONE) return -1;
    if (pivot_q16 < 0 || pivot_q16 > VBE_AFFINE_ONE) return -1;
    vbe_affine_identity(out);
    for (int i = 0; i < 3; ++i) out->m[i][i] = contrast_q16;

    int32_t one_minus = VBE_AFFINE_ONE - contrast_q16;
    int32_t offset;
    if (qmul(pivot_q16, one_minus, &offset) < 0) return -1;
    for (int i = 0; i < 3; ++i) out->b[i] = offset;
    return 0;
}

int vbe_affine_brightness(VbeAffineTransform *out, int32_t brightness_q16) {
    if (brightness_q16 < -VBE_AFFINE_ONE || brightness_q16 > VBE_AFFINE_ONE) return -1;
    vbe_affine_identity(out);
    for (int i = 0; i < 3; ++i) out->b[i] = brightness_q16;
    return 0;
}

int vbe_affine_build(VbeAffineTransform *out, const VbeAffineRequest *request) {
    VbeAffineTransform cct, contrast, brightness, tmp;
    if (vbe_affine_cct(&cct, request->kelvin) < 0) return -1;
    if (vbe_affine_contrast(&contrast, request->contrast_q16,
                            request->pivot_q16) < 0) return -1;
    if (vbe_affine_brightness(&brightness, request->brightness_q16) < 0) return -1;

    /* Explicit order: code-space white balance, contrast around pivot,
     * then uniform brightness offset. Quantization for IFTU is separate. */
    if (vbe_affine_compose(&tmp, &contrast, &cct) < 0) return -1;
    if (vbe_affine_compose(out, &brightness, &tmp) < 0) return -1;
    return 0;
}
