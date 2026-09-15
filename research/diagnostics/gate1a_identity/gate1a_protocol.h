#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../gate0_observer/observer_lifecycle_core.h"
#include "identity_copy_core.h"

#define VBE_G1_MAGIC 0x31474256u
#define VBE_G1_DUMP_MAGIC 0x31444256u
#define VBE_G1_VERSION 2u
#define VBE_G1_FW_365 0x03650000u
#define VBE_G1_CAPACITY 256u
#define VBE_G1_COMMITTED 0xC01117EDu
#define VBE_G1_RECORD_SIZE 192u
#define VBE_G1_STATUS_SIZE 72u
#define VBE_G1_DUMP_HEADER_SIZE 64u

#define VBE_G1_HOOK_A (1u << 0)
#define VBE_G1_HOOK_B (1u << 1)
#define VBE_G1_HOOK_ENABLE (1u << 2)
#define VBE_G1_REQUIRED (VBE_G1_HOOK_A | VBE_G1_HOOK_B | VBE_G1_HOOK_ENABLE)
#define VBE_G1_FAIL_A (1u << 0)
#define VBE_G1_FAIL_B (1u << 1)
#define VBE_G1_FAIL_ENABLE (1u << 2)
#define VBE_G1_FAIL_FW (1u << 31)

#define VBE_G1_FLAG_NULL (1u << 0)
#define VBE_G1_FLAG_COPY_EQUAL (1u << 1)
#define VBE_G1_FLAG_RETURN_VALID (1u << 2)
#define VBE_G1_FLAG_VALID_PLANE (1u << 3)
#define VBE_G1_FLAG_INVALID_PLANE (1u << 4)
#define VBE_G1_FLAG_SUBSTITUTED (1u << 5)
#define VBE_G1_FLAG_MISMATCH_FALLBACK (1u << 6)
#define VBE_G1_KNOWN_FLAGS (VBE_G1_FLAG_NULL | VBE_G1_FLAG_COPY_EQUAL | VBE_G1_FLAG_RETURN_VALID | VBE_G1_FLAG_VALID_PLANE | VBE_G1_FLAG_INVALID_PLANE | VBE_G1_FLAG_SUBSTITUTED | VBE_G1_FLAG_MISMATCH_FALLBACK)

#define VBE_G1_ERR_INVALID (-1)
#define VBE_G1_ERR_BUSY (-2)
#define VBE_G1_ERR_STATE (-5)

typedef enum VbeG1Event {
    VBE_G1_CSC_A = 1,
    VBE_G1_CSC_B = 2,
    VBE_G1_ENABLE_ENTER = 3,
    VBE_G1_ENABLE_EXIT = 4
} VbeG1Event;

typedef struct VbeG1Record {
    uint32_t committed, sequence, completion_sequence, thread_id, invocation_id;
    uint16_t event_type;
    int16_t plane;
    uint32_t flags;
    int32_t raw_return;
    uint32_t source_pointer, generation, source_hash32, copy_hash32;
    uint8_t source_payload[VBE_G1_CSC_SIZE];
    uint8_t copy_payload[VBE_G1_CSC_SIZE];
    uint32_t ring_epoch;
    uint32_t reserved[5];
} VbeG1Record;

typedef struct VbeG1Status {
    uint32_t magic, version, firmware_version, lifecycle, record_capacity,
             slots_reserved, committed_records, lost_records, last_sequence,
             active_producers, owned_hook_mask, required_hook_mask,
             missing_required_mask, hook_fail_mask, ring_epoch,
             substituted_nonnull, null_calls, mismatch_count;
} VbeG1Status;

typedef struct VbeG1DumpHeader {
    uint32_t magic, version, header_size, status_size, record_size, record_count,
             required_hook_mask, missing_required_mask, lost_records, ring_epoch,
             lifecycle, mismatch_count, reserved[4];
} VbeG1DumpHeader;

#define G1_ASSERT(n, e) typedef char n[(e) ? 1 : -1]
G1_ASSERT(g1_rec_192, sizeof(VbeG1Record) == VBE_G1_RECORD_SIZE);
G1_ASSERT(g1_status_72, sizeof(VbeG1Status) == VBE_G1_STATUS_SIZE);
G1_ASSERT(g1_hdr_64, sizeof(VbeG1DumpHeader) == VBE_G1_DUMP_HEADER_SIZE);
G1_ASSERT(g1_src_payload_off_48, offsetof(VbeG1Record, source_payload) == 48u);
G1_ASSERT(g1_copy_payload_off_108, offsetof(VbeG1Record, copy_payload) == 108u);
G1_ASSERT(g1_epoch_off_168, offsetof(VbeG1Record, ring_epoch) == 168u);
#undef G1_ASSERT

int vbeG1GetStatus(VbeG1Status *out);
int vbeG1Pause(void);
int vbeG1Reset(int enable_after_reset);
int vbeG1Read(VbeG1Record *out, uint32_t capacity, uint32_t *written);
