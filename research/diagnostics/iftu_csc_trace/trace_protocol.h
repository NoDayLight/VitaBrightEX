#pragma once

#include <stdint.h>

#define VBE_TRACE_MAGIC                   0x56425452u /* VBTR */
#define VBE_TRACE_DUMP_MAGIC              0x56424450u /* VBDP */
#define VBE_TRACE_VERSION                 3u
#define VBE_TRACE_RECORD_CAPACITY         512u
#define VBE_TRACE_PAYLOAD_MAX             256u
#define VBE_TRACE_CSC_PAYLOAD_SIZE        0x3Cu
#define VBE_TRACE_PLANE_COUNT             5u
#define VBE_TRACE_COMMITTED               0xC01117EDu
#define VBE_TRACE_FW_365                  0x03650000u

#define VBE_TRACE_FLAG_NULL               (1u << 0)
#define VBE_TRACE_FLAG_TRUNCATED          (1u << 1)

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
#define VBE_TRACE_REQUIRED_PANEL_HOOKS (VBE_TRACE_HOOK_PANEL_WRITE | VBE_TRACE_HOOK_PANEL_READ)
#define VBE_TRACE_REQUIRED_HOOKS (VBE_TRACE_REQUIRED_AFFINE_HOOKS | VBE_TRACE_REQUIRED_PANEL_HOOKS)

#define VBE_TRACE_SNAPSHOT_LOWIO          (1u << 0)
#define VBE_TRACE_SNAPSHOT_LCD            (1u << 1)
#define VBE_TRACE_REQUIRED_SNAPSHOTS      (VBE_TRACE_SNAPSHOT_LOWIO | VBE_TRACE_SNAPSHOT_LCD)
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

#define VBE_TRACE_ERR_INVALID             (-1)
#define VBE_TRACE_ERR_BUSY                (-2)
#define VBE_TRACE_ERR_UNAVAILABLE         (-3)
#define VBE_TRACE_ERR_MALFORMED           (-4)

typedef enum VbeTraceEventType {
    VBE_TRACE_CSC_A = 1,
    VBE_TRACE_CSC_B = 2,
    VBE_TRACE_DISPLAY_BRIGHTNESS_ENTER = 3,
    VBE_TRACE_DISPLAY_BRIGHTNESS_EXIT = 4,
    VBE_TRACE_DISPLAY_COLORSPACE_ENTER = 5,
    VBE_TRACE_DISPLAY_COLORSPACE_EXIT = 6,
    VBE_TRACE_LCD_BRIGHTNESS_ENTER = 7,
    VBE_TRACE_LCD_BRIGHTNESS_EXIT = 8,
    VBE_TRACE_LCD_COLORSPACE_ENTER = 9,
    VBE_TRACE_LCD_COLORSPACE_EXIT = 10,
    VBE_TRACE_DISPLAY_ON_ENTER = 11,
    VBE_TRACE_DISPLAY_ON_EXIT = 12,
    VBE_TRACE_DISPLAY_OFF_ENTER = 13,
    VBE_TRACE_DISPLAY_OFF_EXIT = 14,
    VBE_TRACE_IFTU_ENABLE_ENTER = 15,
    VBE_TRACE_IFTU_ENABLE_EXIT = 16,
    VBE_TRACE_PANEL_WRITE = 17,
    VBE_TRACE_PANEL_READ_ENTER = 18,
    VBE_TRACE_PANEL_READ_EXIT = 19,
    VBE_TRACE_EVENT_MAX = VBE_TRACE_PANEL_READ_EXIT
} VbeTraceEventType;

typedef struct VbeTraceRecord {
    uint32_t committed;
    uint32_t sequence;
    uint16_t event_type;
    int16_t plane;
    uint32_t flags;
    int32_t result;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t payload_length;
    uint32_t lost_snapshot;
    uint8_t payload[VBE_TRACE_PAYLOAD_MAX];
} VbeTraceRecord;

typedef struct VbeTraceStatus {
    uint32_t magic;
    uint32_t version;
    uint32_t firmware_version;
    uint32_t enabled;
    uint32_t record_capacity;
    uint32_t slots_reserved;
    uint32_t committed_records;
    uint32_t lost_records;
    uint32_t last_sequence;
    uint32_t active_hooks;
    uint32_t installed_hook_mask;
    uint32_t required_hook_mask;
    uint32_t missing_required_mask;
    uint32_t snapshot_available_mask;
    uint32_t required_snapshot_mask;
    uint32_t missing_required_snapshot_mask;
    uint32_t hook_fail_mask;
} VbeTraceStatus;

typedef struct VbeTracePlaneSnapshot {
    uint32_t active_state_1e8;
    uint32_t csc_control_100;
    uint32_t private_control_1f8;
    uint32_t reserved;
    uint8_t csc_a_10c[VBE_TRACE_CSC_PAYLOAD_SIZE];
    uint8_t csc_b_148[VBE_TRACE_CSC_PAYLOAD_SIZE];
} VbeTracePlaneSnapshot;

typedef struct VbeTraceLcdSnapshot {
    uint16_t ddb_08;
    uint16_t ddb_0a;
    uint16_t bucket_0c;
    uint16_t reserved_0e;
    uint32_t brightness_1c;
    uint32_t secondary_program_28;
    uint32_t color_space_mode_2c;
} VbeTraceLcdSnapshot;

typedef struct VbeTraceSnapshotData {
    VbeTracePlaneSnapshot planes[VBE_TRACE_PLANE_COUNT];
    VbeTraceLcdSnapshot lcd;
} VbeTraceSnapshotData;

typedef struct VbeTraceSnapshot {
    uint32_t magic;
    uint32_t version;
    uint32_t firmware_version;
    uint32_t available_mask;
    uint32_t flags;
    VbeTraceSnapshotData data;
} VbeTraceSnapshot;

typedef struct VbeTraceDumpHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t status_size;
    uint32_t pre_snapshot_size;
    uint32_t post_snapshot_size;
    uint32_t record_size;
    uint32_t record_count;
    uint32_t capture_quality;
    uint32_t pre_snapshot_present;
    uint32_t pre_snapshot_retry_count;
    uint32_t post_snapshot_retry_count;
    uint32_t validation_flags;
    uint32_t reserved[3];
} VbeTraceDumpHeader;

int vbeTraceGetStatus(VbeTraceStatus *out);
int vbeTraceStop(void);
int vbeTraceReset(int enable_after_reset);
int vbeTraceRead(VbeTraceRecord *out, uint32_t capacity, uint32_t *written);
int vbeTraceSnapshot(VbeTraceSnapshot *out);
