#pragma once
#include <stddef.h>
#include <stdint.h>
#include "observer_lifecycle_core.h"

#define VBE_TRACE_MAGIC 0x56425452u /* VBTR */
#define VBE_TRACE_DUMP_MAGIC 0x56424450u /* VBDP */
#define VBE_TRACE_VERSION 7u
#define VBE_TRACE_FW_365 0x03650000u
#define VBE_TRACE_RECORD_CAPACITY 512u
#define VBE_TRACE_CSC_PAYLOAD_SIZE 0x3Cu
#define VBE_TRACE_COMMITTED 0xC01117EDu
#define VBE_TRACE_RECORD_SIZE 128u
#define VBE_TRACE_STATUS_SIZE 72u
#define VBE_TRACE_DUMP_HEADER_SIZE 64u
#define VBE_TRACE_RECORD_PAYLOAD_OFFSET 64u

#define VBE_TRACE_HOOK_CSC_A       (1u << 0)
#define VBE_TRACE_HOOK_CSC_B       (1u << 1)
#define VBE_TRACE_HOOK_IFTU_ENABLE (1u << 2)
#define VBE_TRACE_HOOK_PANEL_WRITE (1u << 3)
#define VBE_TRACE_HOOK_PANEL_READ  (1u << 4)
#define VBE_TRACE_REQUIRED_HOOKS (VBE_TRACE_HOOK_CSC_A|VBE_TRACE_HOOK_CSC_B|VBE_TRACE_HOOK_IFTU_ENABLE|VBE_TRACE_HOOK_PANEL_WRITE|VBE_TRACE_HOOK_PANEL_READ)

#define VBE_TRACE_FAIL_CSC_A            (1u << 0)
#define VBE_TRACE_FAIL_CSC_B            (1u << 1)
#define VBE_TRACE_FAIL_IFTU_ENABLE      (1u << 2)
#define VBE_TRACE_FAIL_PANEL_WRITE      (1u << 3)
#define VBE_TRACE_FAIL_PANEL_READ       (1u << 4)
#define VBE_TRACE_FAIL_PANEL_WRITE_SIG  (1u << 5)
#define VBE_TRACE_FAIL_PANEL_READ_SIG   (1u << 6)
#define VBE_TRACE_FAIL_LCD_INFO         (1u << 7)
#define VBE_TRACE_FAIL_UNSUPPORTED_FW   (1u << 31)

#define VBE_TRACE_FLAG_NULL             (1u << 0)
#define VBE_TRACE_FLAG_INVALID_PLANE    (1u << 1)
#define VBE_TRACE_FLAG_RETURN_VALID     (1u << 2)
#define VBE_TRACE_FLAG_RANGE_OVERFLOW   (1u << 3)

#define VBE_TRACE_STATUS_REBOOT_OWNED   (1u << 0)
#define VBE_TRACE_INSTALL_PUBLICATION_UPSTREAM_RESIDUAL 1u

#define VBE_TRACE_ERR_INVALID       (-1)
#define VBE_TRACE_ERR_BUSY          (-2)
#define VBE_TRACE_ERR_UNAVAILABLE   (-3)
#define VBE_TRACE_ERR_DROPPED       (-4)
#define VBE_TRACE_ERR_STATE         (-5)

typedef enum VbeTraceEventType {
    VBE_TRACE_CSC_A = 1,
    VBE_TRACE_CSC_B = 2,
    VBE_TRACE_IFTU_ENABLE_ENTER = 3,
    VBE_TRACE_IFTU_ENABLE_EXIT = 4,
    VBE_TRACE_PANEL_WRITE = 5,
    VBE_TRACE_PANEL_READ_ENTER = 6,
    VBE_TRACE_PANEL_READ_EXIT = 7,
    VBE_TRACE_MARKER = 8,
    VBE_TRACE_EVENT_MAX = VBE_TRACE_MARKER
} VbeTraceEventType;

typedef enum VbeTracePointerProvenance {
    VBE_PTR_UNPROVEN = 0,
    VBE_PTR_INVALID_RANGE = 1,
    VBE_PTR_SCE_LCD_SEGMENT_0 = 2,
    VBE_PTR_SCE_LCD_SEGMENT_1 = 3,
    VBE_PTR_SCE_LCD_SEGMENT_2 = 4,
    VBE_PTR_SCE_LCD_SEGMENT_3 = 5
} VbeTracePointerProvenance;

typedef enum VbeTracePayloadState {
    VBE_PAYLOAD_NONE = 0,
    VBE_PAYLOAD_ZERO_LENGTH = 1,
    VBE_PAYLOAD_NOT_CAPTURED_GATE0A = 2,
    VBE_PAYLOAD_INVALID_RANGE = 3,
    VBE_PAYLOAD_NULL_NONZERO = 4,
    VBE_PAYLOAD_CSC_EXACT_3C = 5,
    VBE_PAYLOAD_CSC_NULL = 6,
    VBE_PAYLOAD_CSC_INVALID_PLANE = 7
} VbeTracePayloadState;

typedef enum VbeTraceMarker {
    VBE_MARK_BASELINE_IDLE = 1,
    VBE_MARK_BRIGHTNESS_A_1 = 2,
    VBE_MARK_BRIGHTNESS_B = 3,
    VBE_MARK_BRIGHTNESS_A_2 = 4,
    VBE_MARK_COLORSPACE_0_1 = 5,
    VBE_MARK_COLORSPACE_1 = 6,
    VBE_MARK_COLORSPACE_0_2 = 7,
    VBE_MARK_DISPLAY_OFF = 8,
    VBE_MARK_DISPLAY_ON = 9,
    VBE_MARK_PRE_SUSPEND = 10,
    VBE_MARK_POST_RESUME = 11,
    VBE_MARK_DIM_ENTRY = 12,
    VBE_MARK_DIM_EXIT = 13,
    VBE_MARK_MAX = VBE_MARK_DIM_EXIT
} VbeTraceMarker;

typedef struct VbeTraceRecord {
    uint32_t committed;
    uint32_t sequence;
    uint32_t completion_sequence;
    uint32_t thread_id;
    uint32_t invocation_id;
    uint16_t event_type;
    int16_t plane;
    uint32_t flags;
    int32_t raw_return;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t pointer_value;
    uint32_t pointer_provenance;
    uint32_t payload_state;
    uint32_t payload_length;
    uint32_t lost_snapshot;
    uint32_t ring_epoch;
    uint8_t payload[VBE_TRACE_CSC_PAYLOAD_SIZE];
    uint32_t reserved;
} VbeTraceRecord;

typedef struct VbeTraceStatus {
    uint32_t magic;
    uint32_t version;
    uint32_t firmware_version;
    uint32_t lifecycle;
    uint32_t record_capacity;
    uint32_t slots_reserved;
    uint32_t committed_records;
    uint32_t lost_records;
    uint32_t last_sequence;
    uint32_t active_producers;
    uint32_t owned_hook_mask;
    uint32_t required_hook_mask;
    uint32_t missing_required_mask;
    uint32_t hook_fail_mask;
    uint32_t ring_epoch;
    uint32_t install_publication_class;
    uint32_t flags;
    uint32_t reserved;
} VbeTraceStatus;

typedef struct VbeTraceDumpHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t status_size;
    uint32_t record_size;
    uint32_t record_count;
    uint32_t required_hook_mask;
    uint32_t missing_required_mask;
    uint32_t lost_records;
    uint32_t ring_epoch;
    uint32_t lifecycle;
    uint32_t validation_flags;
    uint32_t reserved[4];
} VbeTraceDumpHeader;

#define VBE_CT_ASSERT(name, expr) typedef char name[(expr) ? 1 : -1]
VBE_CT_ASSERT(vbe_record_size_128, sizeof(VbeTraceRecord) == VBE_TRACE_RECORD_SIZE);
VBE_CT_ASSERT(vbe_status_size_72, sizeof(VbeTraceStatus) == VBE_TRACE_STATUS_SIZE);
VBE_CT_ASSERT(vbe_dump_header_size_64, sizeof(VbeTraceDumpHeader) == VBE_TRACE_DUMP_HEADER_SIZE);
VBE_CT_ASSERT(vbe_payload_offset_64, offsetof(VbeTraceRecord, payload) == VBE_TRACE_RECORD_PAYLOAD_OFFSET);
#undef VBE_CT_ASSERT

int vbeTraceGetStatus(VbeTraceStatus *out);
int vbeTracePause(void);
int vbeTraceReset(int enable_after_reset);
int vbeTraceRead(VbeTraceRecord *out, uint32_t capacity, uint32_t *written);
int vbeTraceMark(uint32_t marker);
int vbeTraceQuiesce(void);
