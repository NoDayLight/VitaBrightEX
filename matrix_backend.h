#pragma once
#include <stdint.h>
#include "matrix_backend_core.h"

#define VBE_MATRIX_API_VERSION 1u
#define VBE_MATRIX_REQUEST_VERSION 1u
#define VBE_MATRIX_FW_365 0x03650000u
#define VBE_MATRIX_TARGET_PLANE_MASK 0x3u
#define VBE_MATRIX_REQUEST_V1_SIZE 68u
#define VBE_MATRIX_CAPABILITIES_SIZE 64u
#define VBE_MATRIX_PLANE_STATUS_SIZE 160u
#define VBE_MATRIX_BACKEND_STATUS_SIZE 384u

/* Positive values are truthful control/capability results, not SCE errors. */
typedef enum {
    VBE_MATRIX_RESULT_APPLIED = 0,
    VBE_MATRIX_RESULT_ACCEPTED_PENDING_REPLAY = 1,
    VBE_MATRIX_RESULT_UNSUPPORTED_TARGET = 2,
    VBE_MATRIX_RESULT_INVALID_REQUEST = -1,
    VBE_MATRIX_RESULT_BUSY = -2,
    VBE_MATRIX_RESULT_HOOK_INSTALL = -3,
    VBE_MATRIX_RESULT_POLICY = -4,
    VBE_MATRIX_RESULT_REBOOT_OWNED = -5,
} VbeMatrixResult;

typedef enum {
    VBE_MATRIX_REAPPLY_NONE = 0,
    VBE_MATRIX_REAPPLY_NATURAL_SONY_REPLAY_ONLY = 1,
    VBE_MATRIX_REAPPLY_BLOCKED_TAIHEN_CHAIN_ORDER = 2,
} VbeMatrixReapplyMode;

typedef struct {
    uint32_t size;
    uint32_t version;
    uint32_t flags; /* must be zero in v1 */
    uint32_t reserved0;
    /* Signed S3.9 numerators in hardware-component order. 512 == 1.0. */
    int32_t hardware_component_s3_9[VBE_MATRIX_COEFFS];
    uint32_t reserved[4];
} VbeMatrixRequestV1;

typedef struct {
    uint32_t size;
    uint32_t abi_version;
    uint32_t target_pch2000_fw365_verified;
    uint32_t matrix_backend_supported;
    uint32_t immediate_reapply_supported;
    uint32_t reapply_mode;
    uint32_t gamma_transfer_supported;
    uint32_t additive_affine_supported;
    uint32_t cct_supported;
    uint32_t saturation_supported;
    uint32_t channel_order_proven;
    uint32_t reserved[5];
} VbeMatrixCapabilities;

typedef struct {
    uint32_t valid;
    uint32_t pristine_generation;
    uint32_t baseline_class;
    uint32_t last_forwarded_policy_generation;
    int32_t last_sony_return;
    uint32_t baseline_mismatch_count;
    uint32_t overflow_count;
    uint32_t policy_read_fail_count;
    uint32_t source_fnv1a;
    uint32_t forward_fnv1a;
    uint32_t source_words[VBE_B_OBJECT_WORDS];
    uint32_t forward_words[VBE_B_OBJECT_WORDS];
} VbeMatrixPlaneStatus;

typedef struct {
    uint32_t size;
    uint32_t abi_version;
    uint32_t target_supported;
    uint32_t hook_owned;
    uint32_t hook_fail;
    uint32_t requested_generation;
    uint32_t active_published_generation;
    uint32_t policy_enabled;
    uint32_t pending_plane_mask;
    uint32_t reapply_mode;
    int32_t last_request_result;
    uint32_t request_validation_fail_count;
    uint32_t status_update_drop_count;
    uint32_t reserved[3];
    VbeMatrixPlaneStatus planes[2];
} VbeMatrixBackendStatus;

#define VBE_MATRIX_ABI_ASSERT(n, e) typedef char n[(e) ? 1 : -1]
VBE_MATRIX_ABI_ASSERT(vbe_matrix_request_v1_size,
                      sizeof(VbeMatrixRequestV1) == VBE_MATRIX_REQUEST_V1_SIZE);
VBE_MATRIX_ABI_ASSERT(vbe_matrix_capabilities_size,
                      sizeof(VbeMatrixCapabilities) == VBE_MATRIX_CAPABILITIES_SIZE);
VBE_MATRIX_ABI_ASSERT(vbe_matrix_plane_status_size,
                      sizeof(VbeMatrixPlaneStatus) == VBE_MATRIX_PLANE_STATUS_SIZE);
VBE_MATRIX_ABI_ASSERT(vbe_matrix_backend_status_size,
                      sizeof(VbeMatrixBackendStatus) == VBE_MATRIX_BACKEND_STATUS_SIZE);
#undef VBE_MATRIX_ABI_ASSERT

int matrix_backend_init(int is_lcd, uint32_t firmware);
int matrix_backend_can_unload(void);

int vitabrightMatrixGetCapabilities(VbeMatrixCapabilities *out);
int vitabrightMatrixSetRequest(const VbeMatrixRequestV1 *in);
int vitabrightMatrixReset(void);
int vitabrightMatrixGetStatus(VbeMatrixBackendStatus *out);
