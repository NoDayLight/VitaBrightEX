#pragma once

#include <stdint.h>

#define VBE_TRACE_MAGIC              0x56425452u /* VBTR */
#define VBE_TRACE_DUMP_MAGIC         0x56424450u /* VBDP */
#define VBE_TRACE_VERSION            2u
#define VBE_TRACE_RECORD_CAPACITY    512u
#define VBE_TRACE_PAYLOAD_MAX        256u
#define VBE_TRACE_PLANE_COUNT        5u
#define VBE_TRACE_COMMITTED          0xC01117EDu
#define VBE_TRACE_FW_365              0x03650000u

#define VBE_TRACE_FLAG_NULL          (1u << 0)
#define VBE_TRACE_FLAG_TRUNCATED     (1u << 1)

#define VBE_TRACE_FAIL_CSC_A             (1u << 0)
#define VBE_TRACE_FAIL_CSC_B             (1u << 1)
#define VBE_TRACE_FAIL_DISPLAY_BRIGHT    (1u << 2)
#define VBE_TRACE_FAIL_DISPLAY_COLOR     (1u << 3)
#define VBE_TRACE_FAIL_LCD_BRIGHT        (1u << 4)
#define VBE_TRACE_FAIL_LCD_COLOR         (1u << 5)
#define VBE_TRACE_FAIL_DISPLAY_ON        (1u << 6)
#define VBE_TRACE_FAIL_DISPLAY_OFF       (1u << 7)
#define VBE_TRACE_FAIL_IFTU_ENABLE       (1u << 8)
#define VBE_TRACE_FAIL_PANEL_WRITE       (1u << 9)
#define VBE_TRACE_FAIL_LOWIO_SNAPSHOT    (1u << 10)
#define VBE_TRACE_FAIL_UNSUPPORTED_FW    (1u << 31)

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
    VBE_TRACE_DISPLAY_ON = 11,
    VBE_TRACE_DISPLAY_OFF = 12,
    VBE_TRACE_IFTU_ENABLE = 13,
    VBE_TRACE_PANEL_WRITE = 14
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
    uint32_t hook_fail_mask;
    uint32_t snapshot_available;
} VbeTraceStatus;

typedef struct VbeTracePlaneSnapshot {
    uint32_t active_state_1e8;
    uint32_t csc_control_100;
    uint32_t private_control_1f8;
    uint32_t reserved;
    uint8_t csc_a_10c[0x3C];
    uint8_t csc_b_148[0x3C];
} VbeTracePlaneSnapshot;

typedef struct VbeTraceSnapshot {
    uint32_t magic;
    uint32_t version;
    uint32_t firmware_version;
    uint32_t plane_count;
    VbeTracePlaneSnapshot planes[VBE_TRACE_PLANE_COUNT];
} VbeTraceSnapshot;

typedef struct VbeTraceDumpHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t status_size;
    uint32_t snapshot_size;
    uint32_t record_size;
    uint32_t record_count;
    uint32_t reserved;
} VbeTraceDumpHeader;

/* Syscall exports used by the user-side drain utility. */
int vbeTraceGetStatus(VbeTraceStatus *out);
int vbeTraceStop(void);
int vbeTraceReset(int enable_after_reset);
int vbeTraceRead(VbeTraceRecord *out, uint32_t capacity, uint32_t *written);
int vbeTraceSnapshot(VbeTraceSnapshot *out);
