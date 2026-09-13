#pragma once
#include <stdint.h>

#define VBE_AFFINE_SHIFT 16
#define VBE_AFFINE_ONE   (1 << VBE_AFFINE_SHIFT)

typedef struct {
    int32_t m[3][3];
    int32_t b[3];
} VbeAffineTransform;

typedef struct {
    int kelvin;
    int32_t contrast_q16;
    int32_t brightness_q16;
    int32_t pivot_q16;
} VbeAffineRequest;

void vbe_affine_identity(VbeAffineTransform *out);
int vbe_affine_is_identity(const VbeAffineTransform *t);
int vbe_affine_compose(VbeAffineTransform *out,
                       const VbeAffineTransform *after,
                       const VbeAffineTransform *before);
int vbe_affine_cct(VbeAffineTransform *out, int kelvin);
int vbe_affine_contrast(VbeAffineTransform *out,
                        int32_t contrast_q16, int32_t pivot_q16);
int vbe_affine_brightness(VbeAffineTransform *out, int32_t brightness_q16);
int vbe_affine_build(VbeAffineTransform *out, const VbeAffineRequest *request);
