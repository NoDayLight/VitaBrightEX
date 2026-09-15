#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../gate0_observer/observer_lifecycle_core.h"
#include "stage_probe_core.h"

#define VBE_G1C_MAGIC 0x31434256u
#define VBE_G1C_DUMP_MAGIC 0x31434456u
#define VBE_G1C_VERSION 1u
#define VBE_G1C_FW_365 0x03650000u
#define VBE_G1C_CAPACITY 256u
#define VBE_G1C_COMMITTED 0xC01117EDu
#define VBE_G1C_RECORD_SIZE 192u
#define VBE_G1C_STATUS_SIZE 72u
#define VBE_G1C_DUMP_HEADER_SIZE 64u

#define VBE_G1C_HOOK_A (1u << 0)
#define VBE_G1C_HOOK_B (1u << 1)
#define VBE_G1C_HOOK_ENABLE (1u << 2)
#define VBE_G1C_REQUIRED (VBE_G1C_HOOK_A | VBE_G1C_HOOK_B | VBE_G1C_HOOK_ENABLE)
#define VBE_G1C_FAIL_A (1u << 0)
#define VBE_G1C_FAIL_B (1u << 1)
#define VBE_G1C_FAIL_ENABLE (1u << 2)
#define VBE_G1C_FAIL_FW (1u << 31)

#define VBE_G1C_FLAG_NULL (1u << 0)
#define VBE_G1C_FLAG_RETURN_VALID (1u << 1)
#define VBE_G1C_FLAG_TARGET_PLANE (1u << 2)
#define VBE_G1C_FLAG_OUTSIDE_PLANE (1u << 3)
#define VBE_G1C_FLAG_SOURCE_CAPTURED (1u << 4)
#define VBE_G1C_FLAG_FORWARD_OWNED (1u << 5)
#define VBE_G1C_FLAG_TRANSFORMED_B (1u << 6)
#define VBE_G1C_FLAG_BASELINE_MISMATCH (1u << 7)
#define VBE_G1C_FLAG_CANONICAL_SOURCE (1u << 8)
#define VBE_G1C_KNOWN_FLAGS (VBE_G1C_FLAG_NULL | VBE_G1C_FLAG_RETURN_VALID | VBE_G1C_FLAG_TARGET_PLANE | VBE_G1C_FLAG_OUTSIDE_PLANE | VBE_G1C_FLAG_SOURCE_CAPTURED | VBE_G1C_FLAG_FORWARD_OWNED | VBE_G1C_FLAG_TRANSFORMED_B | VBE_G1C_FLAG_BASELINE_MISMATCH | VBE_G1C_FLAG_CANONICAL_SOURCE)

#define VBE_G1C_ERR_INVALID (-1)
#define VBE_G1C_ERR_BUSY (-2)
#define VBE_G1C_ERR_STATE (-5)

typedef enum VbeG1cEvent {
    VBE_G1C_CSC_A = 1,
    VBE_G1C_CSC_B = 2,
    VBE_G1C_ENABLE_ENTER = 3,
    VBE_G1C_ENABLE_EXIT = 4
} VbeG1cEvent;

typedef struct VbeG1cRecord {
    uint32_t committed, sequence, completion_sequence, thread_id, invocation_id;
    uint16_t event_type;
    int16_t plane;
    uint32_t flags;
    int32_t raw_return;
    uint32_t source_pointer, generation, source_hash32, forward_hash32;
    uint8_t source_payload[VBE_G1C_CSC_SIZE];
    uint8_t forward_payload[VBE_G1C_CSC_SIZE];
    uint32_t ring_epoch;
    uint32_t reserved[5];
} VbeG1cRecord;

typedef struct VbeG1cStatus {
    uint32_t magic, version, firmware_version, lifecycle, record_capacity,
             slots_reserved, committed_records, lost_records, last_sequence,
             active_producers, owned_hook_mask, required_hook_mask,
             missing_required_mask, hook_fail_mask, ring_epoch,
             transformed_b, null_calls, baseline_mismatch_count;
} VbeG1cStatus;

typedef struct VbeG1cDumpHeader {
    uint32_t magic, version, header_size, status_size, record_size, record_count,
             required_hook_mask, missing_required_mask, lost_records, ring_epoch,
             lifecycle, baseline_mismatch_count, reserved[4];
} VbeG1cDumpHeader;

#define G1C_ASSERT(n, e) typedef char n[(e) ? 1 : -1]
G1C_ASSERT(g1c_rec_192, sizeof(VbeG1cRecord) == VBE_G1C_RECORD_SIZE);
G1C_ASSERT(g1c_status_72, sizeof(VbeG1cStatus) == VBE_G1C_STATUS_SIZE);
G1C_ASSERT(g1c_hdr_64, sizeof(VbeG1cDumpHeader) == VBE_G1C_DUMP_HEADER_SIZE);
G1C_ASSERT(g1c_src_payload_off_48, offsetof(VbeG1cRecord, source_payload) == 48u);
G1C_ASSERT(g1c_forward_payload_off_108, offsetof(VbeG1cRecord, forward_payload) == 108u);
G1C_ASSERT(g1c_epoch_off_168, offsetof(VbeG1cRecord, ring_epoch) == 168u);
#undef G1C_ASSERT

int vbeG1cGetStatus(VbeG1cStatus *out);
int vbeG1cPause(void);
int vbeG1cReset(int enable_after_reset);
int vbeG1cRead(VbeG1cRecord *out, uint32_t capacity, uint32_t *written);
