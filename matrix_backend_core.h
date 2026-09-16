#pragma once
#include <stdint.h>

#define VBE_MATRIX_DIM 3u
#define VBE_MATRIX_COEFFS 9u
#define VBE_B_OBJECT_WORDS 15u
#define VBE_B_OBJECT_SIZE 60u
#define VBE_B_CTM_WORD_FIRST 6u
#define VBE_S39_SCALE 512
#define VBE_S39_MIN (-2048)
#define VBE_S39_MAX 2047

typedef struct {
    int32_t v[VBE_MATRIX_COEFFS];
} VbeMatrixS39;

typedef struct {
    uint32_t words[VBE_B_OBJECT_WORDS];
} VbeBStageObject;

typedef enum {
    VBE_B_BASELINE_UNKNOWN = 0,
    VBE_B_BASELINE_CANONICAL_IDENTITY = 1,
    VBE_B_BASELINE_SONY_FULL_TO_LIMITED = 2,
} VbeBBaselineClass;

typedef enum {
    VBE_MATRIX_CORE_OK = 0,
    VBE_MATRIX_CORE_INVALID = -1,
    VBE_MATRIX_CORE_OVERFLOW = -2,
    VBE_MATRIX_CORE_UNKNOWN_BASELINE = -3,
} VbeMatrixCoreResult;

extern const VbeBStageObject vbe_b_baseline_identity;
extern const VbeBStageObject vbe_b_baseline_full_to_limited;
extern const char vbe_b_baseline_identity_sha256[];
extern const char vbe_b_baseline_full_to_limited_sha256[];

void vbe_matrix_identity(VbeMatrixS39 *out);
int vbe_s39_decode_word(uint32_t raw, int32_t *numerator);
int vbe_s39_encode_word(int32_t numerator, uint32_t *raw);
int vbe_matrix_validate(const VbeMatrixS39 *m);
int vbe_matrix_multiply(const VbeMatrixS39 *left,
                        const VbeMatrixS39 *right,
                        VbeMatrixS39 *out);
VbeBBaselineClass vbe_b_baseline_classify(const VbeBStageObject *obj);
int vbe_b_object_compose(const VbeBStageObject *sony,
                         const VbeMatrixS39 *user,
                         VbeBStageObject *out,
                         VbeBBaselineClass *baseline_class);
int vbe_b_object_equal(const VbeBStageObject *a, const VbeBStageObject *b);
uint32_t vbe_b_object_fnv1a(const VbeBStageObject *obj);
void vbe_b_object_copy_from_volatile(VbeBStageObject *dst,
                                     const volatile void *src);
