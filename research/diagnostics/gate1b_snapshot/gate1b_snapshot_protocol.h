#pragma once
#include <stddef.h>
#include <stdint.h>

#define VBE_G1B_MAGIC 0x42314756u
#define VBE_G1B_BUNDLE_MAGIC 0x32424756u
#define VBE_G1B_VERSION 2u
#define VBE_G1B_FW_365 0x03650000u
#define VBE_G1B_CSC_SIZE 0x3Cu
#define VBE_G1B_PLANE_COUNT 2u
#define VBE_G1B_LOWIO_SEGMENT 1u
#define VBE_G1B_PLANE0_OFFSET 0x37Cu
#define VBE_G1B_PLANE1_OFFSET 0x590u
#define VBE_G1B_SYS_EVENT_SUSPEND_RESUME 0x00100000u

#define VBE_G1B_STATUS_FW_OK             (1u << 0)
#define VBE_G1B_STATUS_LOWIO_FOUND       (1u << 1)
#define VBE_G1B_STATUS_PLANE0_RESOLVED   (1u << 2)
#define VBE_G1B_STATUS_PLANE1_RESOLVED   (1u << 3)
#define VBE_G1B_STATUS_ENABLE_HOOK       (1u << 4)
#define VBE_G1B_STATUS_SYSEVENT          (1u << 5)

#define VBE_G1B_PLANE_STATE_RESOLVED     (1u << 0)
#define VBE_G1B_PLANE_CACHE_READ         (1u << 1)
#define VBE_G1B_PLANE_MMIO_WHITELISTED   (1u << 2)
#define VBE_G1B_PLANE_CONTROL_READ       (1u << 3)
#define VBE_G1B_PLANE_ENABLE_STABLE      (1u << 4)
#define VBE_G1B_PLANE_ENABLE_ACTIVE      (1u << 5)
#define VBE_G1B_PLANE_CANONICAL_A        (1u << 6)
#define VBE_G1B_PLANE_CANONICAL_B        (1u << 7)

#define VBE_G1B_HOOK_ENABLE (1u << 0)
#define VBE_G1B_REQUIRED_HOOKS VBE_G1B_HOOK_ENABLE
#define VBE_G1B_FAIL_ENABLE_HOOK (1u << 0)
#define VBE_G1B_FAIL_SYSEVENT (1u << 1)
#define VBE_G1B_FAIL_FW (1u << 31)

#define VBE_G1B_CAPTURE_NONE 0u
#define VBE_G1B_CAPTURE_PRE_SUSPEND_EVENT 1u
#define VBE_G1B_CAPTURE_POST_RESUME_EVENT_ACTIVE 2u
#define VBE_G1B_CAPTURE_POST_RESUME_ENABLE_P1 3u

#define VBE_G1B_ERR_INVALID (-1)

#define VBE_G1B_A_CANONICAL_SHA256 "2f9fd211d1d389611267070cfbc936063b0b790a59245243feb404ee3c00daf6"
#define VBE_G1B_B_CANONICAL_SHA256 "5dc12dfcae42a648dc093db831b661cc3068f2d70f199b03fb582ce301b188b5"

typedef struct VbeG1bPlaneSnapshot {
    uint32_t plane;
    uint32_t flags;
    uint32_t state_address;
    uint32_t mmio_base;
    uint32_t live_csc_control;
    uint32_t enable_state_before;
    uint32_t enable_state_after;
    uint32_t private_control;
    uint8_t cache_a[VBE_G1B_CSC_SIZE];
    uint8_t cache_b[VBE_G1B_CSC_SIZE];
} VbeG1bPlaneSnapshot;

typedef struct VbeG1bSnapshot {
    uint32_t magic;
    uint32_t version;
    uint32_t struct_size;
    uint32_t firmware_version;
    uint32_t capture_sequence;
    uint32_t status_flags;
    uint32_t lowio_modid;
    uint32_t lowio_module_nid;
    uint32_t lowio_segment_index;
    uint32_t capture_reason;
    VbeG1bPlaneSnapshot planes[VBE_G1B_PLANE_COUNT];
} VbeG1bSnapshot;

typedef struct VbeG1bCaptureStatus {
    uint32_t magic;
    uint32_t version;
    uint32_t firmware_version;
    uint32_t status_flags;
    uint32_t owned_hook_mask;
    uint32_t required_hook_mask;
    uint32_t missing_hook_mask;
    uint32_t fail_mask;
    uint32_t suspend_event_count;
    uint32_t resume_event_count;
    uint32_t pre_suspend_published;
    uint32_t post_resume_published;
    uint32_t resume_pending;
    int32_t last_enable_return;
    uint32_t pre_suspend_reason;
    uint32_t post_resume_reason;
} VbeG1bCaptureStatus;

typedef struct VbeG1bCaptureBundle {
    uint32_t magic;
    uint32_t version;
    uint32_t struct_size;
    uint32_t status_size;
    uint32_t snapshot_size;
    uint32_t snapshot_count;
    uint32_t reserved0;
    uint32_t reserved1;
    VbeG1bCaptureStatus status;
    VbeG1bSnapshot snapshots[2];
} VbeG1bCaptureBundle;

#define G1B_ASSERT(n, e) typedef char n[(e) ? 1 : -1]
G1B_ASSERT(g1b_plane_152, sizeof(VbeG1bPlaneSnapshot) == 152u);
G1B_ASSERT(g1b_snapshot_344, sizeof(VbeG1bSnapshot) == 344u);
G1B_ASSERT(g1b_status_64, sizeof(VbeG1bCaptureStatus) == 64u);
G1B_ASSERT(g1b_bundle_784, sizeof(VbeG1bCaptureBundle) == 784u);
G1B_ASSERT(g1b_plane_cache_a_off_32, offsetof(VbeG1bPlaneSnapshot, cache_a) == 32u);
G1B_ASSERT(g1b_plane_cache_b_off_92, offsetof(VbeG1bPlaneSnapshot, cache_b) == 92u);
G1B_ASSERT(g1b_snapshot_planes_off_40, offsetof(VbeG1bSnapshot, planes) == 40u);
G1B_ASSERT(g1b_bundle_status_off_32, offsetof(VbeG1bCaptureBundle, status) == 32u);
G1B_ASSERT(g1b_bundle_snapshots_off_96, offsetof(VbeG1bCaptureBundle, snapshots) == 96u);
#undef G1B_ASSERT

int vbeG1bGetCaptureBundle(VbeG1bCaptureBundle *out);
