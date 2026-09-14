#pragma once

#include <stddef.h>
#include <stdint.h>

#define VBE_TRACE_MAGIC                   0x56425452u /* VBTR */
#define VBE_TRACE_DUMP_MAGIC              0x56424450u /* VBDP */
#define VBE_TRACE_VERSION                 6u
#define VBE_TRACE_RECORD_CAPACITY         512u
#define VBE_TRACE_PAYLOAD_MAX             256u
#define VBE_TRACE_PANEL_WRITE_MAX         255u
#define VBE_TRACE_CSC_PAYLOAD_SIZE        0x3Cu
#define VBE_TRACE_PLANE_COUNT             5u
#define VBE_TRACE_COMMITTED               0xC01117EDu
#define VBE_TRACE_FW_365                  0x03650000u

#define VBE_TRACE_RECORD_SIZE             304u
#define VBE_TRACE_STATUS_SIZE             68u
#define VBE_TRACE_PLANE_SNAPSHOT_SIZE     136u
#define VBE_TRACE_LCD_SNAPSHOT_SIZE       20u
#define VBE_TRACE_SNAPSHOT_DATA_SIZE      700u
#define VBE_TRACE_SNAPSHOT_SIZE           720u
#define VBE_TRACE_DUMP_HEADER_SIZE        108u
#define VBE_TRACE_RECORD_PAYLOAD_OFFSET   48u

#define VBE_TRACE_FLAG_NULL                       (1u << 0)
#define VBE_TRACE_FLAG_BOUNDS_REJECTED            (1u << 1)
#define VBE_TRACE_FLAG_READ_NOT_CAPTURED_UNPROVEN (1u << 2)
#define VBE_TRACE_FLAG_READ_PAYLOAD_PROVEN        (1u << 3)
#define VBE_TRACE_FLAG_RETURN_VALID               (1u << 4)

#define VBE_TRACE_HOOK_CSC_A              (1u << 0)
#define VBE_TRACE_HOOK_CSC_B              (1u << 1)
#define VBE_TRACE_HOOK_DISPLAY_BRIGHT     (1u << 2)
#define VBE_TRACE_HOOK_DISPLAY_COLOR      (1u << 3)
#define VBE_TRACE_HOOK_LCD_BRIGHT         (1u << 4)
#define VBE_TRACE_HOOK_LCD_COLOR          (1u << 5)
#define VBE_TRACE_HOOK_DISPLAY_ON         (1u << 6)
#define VBE_TRACE_HOOK_DISPLAY_OFF        (1u << 7)
#define VBE_TRACE_HOOK_IFTU_ENABLE        (1u << 8)
#define VBE_TRACE_HOOK_PANEL_WRITE        (1u << 9)
#define VBE_TRACE_HOOK_PANEL_READ         (1u << 10)

#define VBE_TRACE_REQUIRED_AFFINE_HOOKS ( \
    VBE_TRACE_HOOK_CSC_A | VBE_TRACE_HOOK_CSC_B | \
    VBE_TRACE_HOOK_DISPLAY_BRIGHT | VBE_TRACE_HOOK_DISPLAY_COLOR | \
    VBE_TRACE_HOOK_LCD_BRIGHT | VBE_TRACE_HOOK_LCD_COLOR | \
    VBE_TRACE_HOOK_DISPLAY_ON | VBE_TRACE_HOOK_DISPLAY_OFF | \
    VBE_TRACE_HOOK_IFTU_ENABLE)
#define VBE_TRACE_REQUIRED_PANEL_HOOKS \
    (VBE_TRACE_REQUIRED_AFFINE_HOOKS | VBE_TRACE_HOOK_PANEL_WRITE | VBE_TRACE_HOOK_PANEL_READ)
#define VBE_TRACE_REQUIRED_HOOKS VBE_TRACE_REQUIRED_PANEL_HOOKS

#define VBE_TRACE_SNAPSHOT_LOWIO          (1u << 0)
#define VBE_TRACE_SNAPSHOT_LCD            (1u << 1)
#define VBE_TRACE_REQUIRED_AFFINE_SNAPSHOTS VBE_TRACE_SNAPSHOT_LOWIO
#define VBE_TRACE_REQUIRED_PANEL_SNAPSHOTS \
    (VBE_TRACE_SNAPSHOT_LOWIO | VBE_TRACE_SNAPSHOT_LCD)
#define VBE_TRACE_REQUIRED_SNAPSHOTS VBE_TRACE_REQUIRED_PANEL_SNAPSHOTS
#define VBE_TRACE_SNAPSHOT_STABLE         (1u << 0)

#define VBE_TRACE_FAIL_CSC_A              (1u << 0)
#define VBE_TRACE_FAIL_CSC_B              (1u << 1)
#define VBE_TRACE_FAIL_DISPLAY_BRIGHT     (1u << 2)
#define VBE_TRACE_FAIL_DISPLAY_COLOR      (1u << 3)
#define VBE_TRACE_FAIL_LCD_BRIGHT         (1u << 4)
#define VBE_TRACE_FAIL_LCD_COLOR          (1u << 5)
#define VBE_TRACE_FAIL_DISPLAY_ON         (1u << 6)
#define VBE_TRACE_FAIL_DISPLAY_OFF        (1u << 7)
#define VBE_TRACE_FAIL_IFTU_ENABLE        (1u << 8)
#define VBE_TRACE_FAIL_PANEL_WRITE        (1u << 9)
#define VBE_TRACE_FAIL_LOWIO_SNAPSHOT     (1u << 10)
#define VBE_TRACE_FAIL_PANEL_READ         (1u << 11)
#define VBE_TRACE_FAIL_PANEL_WRITE_SIG    (1u << 12)
#define VBE_TRACE_FAIL_PANEL_READ_SIG     (1u << 13)
#define VBE_TRACE_FAIL_LCD_SNAPSHOT       (1u << 14)
#define VBE_TRACE_FAIL_UNSUPPORTED_FW     (1u << 31)

#define VBE_TRACE_CAPTURE_AUTHORITATIVE   1u
#define VBE_TRACE_CAPTURE_PARTIAL         2u
#define VBE_TRACE_PANEL_READ_NONE         0u
#define VBE_TRACE_PANEL_READ_PROVEN_ONLY  1u
#define VBE_TRACE_PANEL_READ_UNPROVEN_SEEN 2u
#define VBE_TRACE_ERR_INVALID             (-1)
#define VBE_TRACE_ERR_BUSY                (-2)
#define VBE_TRACE_ERR_UNAVAILABLE         (-3)
#define VBE_TRACE_ERR_MALFORMED           (-4)
#define VBE_TRACE_ERR_DROPPED              (-5)

typedef enum VbeTraceEventType {
    VBE_TRACE_CSC_A = 1, VBE_TRACE_CSC_B = 2,
    VBE_TRACE_DISPLAY_BRIGHTNESS_ENTER = 3, VBE_TRACE_DISPLAY_BRIGHTNESS_EXIT = 4,
    VBE_TRACE_DISPLAY_COLORSPACE_ENTER = 5, VBE_TRACE_DISPLAY_COLORSPACE_EXIT = 6,
    VBE_TRACE_LCD_BRIGHTNESS_ENTER = 7, VBE_TRACE_LCD_BRIGHTNESS_EXIT = 8,
    VBE_TRACE_LCD_COLORSPACE_ENTER = 9, VBE_TRACE_LCD_COLORSPACE_EXIT = 10,
    VBE_TRACE_DISPLAY_ON_ENTER = 11, VBE_TRACE_DISPLAY_ON_EXIT = 12,
    VBE_TRACE_DISPLAY_OFF_ENTER = 13, VBE_TRACE_DISPLAY_OFF_EXIT = 14,
    VBE_TRACE_IFTU_ENABLE_ENTER = 15, VBE_TRACE_IFTU_ENABLE_EXIT = 16,
    VBE_TRACE_PANEL_WRITE = 17, VBE_TRACE_PANEL_READ_ENTER = 18,
    VBE_TRACE_PANEL_READ_EXIT = 19, VBE_TRACE_MARKER = 20,
    VBE_TRACE_EVENT_MAX = VBE_TRACE_MARKER
} VbeTraceEventType;

typedef enum VbeTraceMarker {
    VBE_MARK_BASELINE_IDLE = 1,
    VBE_MARK_BRIGHTNESS_A_1 = 2,
    VBE_MARK_BRIGHTNESS_B = 3,
    VBE_MARK_BRIGHTNESS_A_2 = 4,
    VBE_MARK_BRIGHTNESS_A_REPEAT = 5,
    VBE_MARK_COLORSPACE_0_1 = 6,
    VBE_MARK_COLORSPACE_1 = 7,
    VBE_MARK_COLORSPACE_0_2 = 8,
    VBE_MARK_DISPLAY_OFF = 9,
    VBE_MARK_DISPLAY_ON = 10,
    VBE_MARK_PRE_SUSPEND = 11,
    VBE_MARK_POST_RESUME = 12,
    VBE_MARK_DIM_ENTRY = 13,
    VBE_MARK_DIM_EXIT = 14,
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
    uint32_t payload_length;
    uint32_t lost_snapshot;
    uint8_t payload[VBE_TRACE_PAYLOAD_MAX];
} VbeTraceRecord;

typedef struct VbeTraceStatus {
    uint32_t magic, version, firmware_version, enabled, record_capacity,
             slots_reserved, committed_records, lost_records, last_sequence,
             active_hooks, installed_hook_mask, required_hook_mask,
             missing_required_mask, snapshot_available_mask,
             required_snapshot_mask, missing_required_snapshot_mask, hook_fail_mask;
} VbeTraceStatus;

typedef struct VbeTracePlaneSnapshot {
    uint32_t active_state_1e8, csc_control_100, private_control_1f8, reserved;
    uint8_t csc_a_10c[VBE_TRACE_CSC_PAYLOAD_SIZE];
    uint8_t csc_b_148[VBE_TRACE_CSC_PAYLOAD_SIZE];
} VbeTracePlaneSnapshot;

typedef struct VbeTraceLcdSnapshot {
    uint16_t ddb_08, ddb_0a, bucket_0c, reserved_0e;
    uint32_t brightness_1c, secondary_program_28, color_space_mode_2c;
} VbeTraceLcdSnapshot;

typedef struct VbeTraceSnapshotData {
    VbeTracePlaneSnapshot planes[VBE_TRACE_PLANE_COUNT];
    VbeTraceLcdSnapshot lcd;
} VbeTraceSnapshotData;

typedef struct VbeTraceSnapshot {
    uint32_t magic, version, firmware_version, available_mask, flags;
    VbeTraceSnapshotData data;
} VbeTraceSnapshot;

typedef struct VbeTraceDumpHeader {
    uint32_t magic, version, header_size, status_size, pre_snapshot_size,
             post_snapshot_size, record_size, record_count, capture_quality,
             pre_snapshot_present, pre_snapshot_retry_count, post_snapshot_retry_count,
             validation_flags, affine_required_mask, affine_missing_required_mask,
             panel_required_mask, panel_missing_required_mask,
             affine_required_snapshot_mask, affine_missing_required_snapshot_mask,
             panel_required_snapshot_mask, panel_missing_required_snapshot_mask,
             affine_capture_quality, panel_capture_quality, panel_read_validity,
             reserved[3];
} VbeTraceDumpHeader;

#define VBE_TRACE_CT_ASSERT(name, expr) typedef char name[(expr) ? 1 : -1]
VBE_TRACE_CT_ASSERT(vbe_trace_record_size_is_304, sizeof(VbeTraceRecord) == VBE_TRACE_RECORD_SIZE);
VBE_TRACE_CT_ASSERT(vbe_trace_status_size_is_68, sizeof(VbeTraceStatus) == VBE_TRACE_STATUS_SIZE);
VBE_TRACE_CT_ASSERT(vbe_trace_plane_snapshot_size_is_136, sizeof(VbeTracePlaneSnapshot) == VBE_TRACE_PLANE_SNAPSHOT_SIZE);
VBE_TRACE_CT_ASSERT(vbe_trace_lcd_snapshot_size_is_20, sizeof(VbeTraceLcdSnapshot) == VBE_TRACE_LCD_SNAPSHOT_SIZE);
VBE_TRACE_CT_ASSERT(vbe_trace_snapshot_data_size_is_700, sizeof(VbeTraceSnapshotData) == VBE_TRACE_SNAPSHOT_DATA_SIZE);
VBE_TRACE_CT_ASSERT(vbe_trace_snapshot_size_is_720, sizeof(VbeTraceSnapshot) == VBE_TRACE_SNAPSHOT_SIZE);
VBE_TRACE_CT_ASSERT(vbe_trace_dump_header_size_is_108, sizeof(VbeTraceDumpHeader) == VBE_TRACE_DUMP_HEADER_SIZE);
VBE_TRACE_CT_ASSERT(vbe_trace_payload_offset_is_48, offsetof(VbeTraceRecord, payload) == VBE_TRACE_RECORD_PAYLOAD_OFFSET);
#undef VBE_TRACE_CT_ASSERT

int vbeTraceGetStatus(VbeTraceStatus *out);
int vbeTraceStop(void);
int vbeTraceReset(int enable_after_reset);
int vbeTraceRead(VbeTraceRecord *out, uint32_t capacity, uint32_t *written);
int vbeTraceSnapshot(VbeTraceSnapshot *out);
int vbeTraceMark(uint32_t marker);
