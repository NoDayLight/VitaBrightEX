#pragma once
#include <stddef.h>
#include <stdint.h>

#define VBE_G1B_SNAPSHOT_MAGIC 0x42314756u
#define VBE_G1B_SNAPSHOT_VERSION 1u
#define VBE_G1B_FW_365 0x03650000u
#define VBE_G1B_CSC_SIZE 0x3Cu
#define VBE_G1B_PLANE_COUNT 2u
#define VBE_G1B_LOWIO_SEGMENT 1u
#define VBE_G1B_PLANE0_OFFSET 0x37Cu
#define VBE_G1B_PLANE1_OFFSET 0x590u

#define VBE_G1B_STATUS_FW_OK             (1u << 0)
#define VBE_G1B_STATUS_LOWIO_FOUND       (1u << 1)
#define VBE_G1B_STATUS_PLANE0_RESOLVED   (1u << 2)
#define VBE_G1B_STATUS_PLANE1_RESOLVED   (1u << 3)

#define VBE_G1B_PLANE_STATE_RESOLVED     (1u << 0)
#define VBE_G1B_PLANE_CACHE_READ         (1u << 1)
#define VBE_G1B_PLANE_MMIO_WHITELISTED   (1u << 2)
#define VBE_G1B_PLANE_CONTROL_READ       (1u << 3)
#define VBE_G1B_PLANE_ENABLE_STABLE      (1u << 4)

#define VBE_G1B_ERR_INVALID (-1)

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
    uint32_t sequence;
    uint32_t status_flags;
    uint32_t lowio_modid;
    uint32_t lowio_module_nid;
    uint32_t lowio_segment_index;
    uint32_t reserved;
    VbeG1bPlaneSnapshot planes[VBE_G1B_PLANE_COUNT];
} VbeG1bSnapshot;

#define G1B_ASSERT(n, e) typedef char n[(e) ? 1 : -1]
G1B_ASSERT(g1b_plane_152, sizeof(VbeG1bPlaneSnapshot) == 152u);
G1B_ASSERT(g1b_snapshot_344, sizeof(VbeG1bSnapshot) == 344u);
G1B_ASSERT(g1b_plane_cache_a_off_32, offsetof(VbeG1bPlaneSnapshot, cache_a) == 32u);
G1B_ASSERT(g1b_plane_cache_b_off_92, offsetof(VbeG1bPlaneSnapshot, cache_b) == 92u);
G1B_ASSERT(g1b_snapshot_planes_off_40, offsetof(VbeG1bSnapshot, planes) == 40u);
#undef G1B_ASSERT

int vbeG1bTakeSnapshot(VbeG1bSnapshot *out);
