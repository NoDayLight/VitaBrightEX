#include <stdint.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>

#include "trace_protocol.h"

#define TRACE_OUTPUT "ux0:data/vbe_color_trace.bin"
#define BOUNDARY_FILE "ux0:data/vbe_color_trace_boundary.bin"
#define QUIESCE_RETRIES 200
#define SNAPSHOT_RETRIES 32
#define RESET_RETRIES 200
#define VALIDATION_RECORDS_OK (1u << 0)
#define VALIDATION_POST_STABLE (1u << 1)
#define VALIDATION_PRE_PRESENT (1u << 2)
#define VALIDATION_NO_TRUNCATION (1u << 3)

static VbeTraceRecord g_records[VBE_TRACE_RECORD_CAPACITY];

static int write_all(SceUID fd, const void *data, uint32_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t done = 0;
    while (done < size) {
        int n = sceIoWrite(fd, p + done, size - done);
        if (n <= 0) return n < 0 ? n : -1;
        done += (uint32_t)n;
    }
    return 0;
}

static int read_all(SceUID fd, void *data, uint32_t size) {
    uint8_t *p = (uint8_t *)data;
    uint32_t done = 0;
    while (done < size) {
        int n = sceIoRead(fd, p + done, size - done);
        if (n <= 0) return n < 0 ? n : -1;
        done += (uint32_t)n;
    }
    return 0;
}

static int wait_quiescent(VbeTraceStatus *status) {
    int i, ret = 0;
    for (i = 0; i < QUIESCE_RETRIES; ++i) {
        ret = vbeTraceGetStatus(status);
        if (ret < 0) return ret;
        if (status->active_hooks == 0u) return 0;
        sceKernelDelayThread(1000);
    }
    return VBE_TRACE_ERR_BUSY;
}

static int stable_snapshot(VbeTraceSnapshot *snapshot, uint32_t *retry_count) {
    int i, ret = VBE_TRACE_ERR_BUSY;
    *retry_count = 0;
    for (i = 0; i < SNAPSHOT_RETRIES; ++i) {
        ret = vbeTraceSnapshot(snapshot);
        if (ret >= 0 && (snapshot->flags & VBE_TRACE_SNAPSHOT_STABLE) != 0u) {
            *retry_count = (uint32_t)i;
            return 0;
        }
        if (ret < 0 && ret != VBE_TRACE_ERR_BUSY) return ret;
        sceKernelDelayThread(1000);
    }
    *retry_count = SNAPSHOT_RETRIES;
    return ret < 0 ? ret : VBE_TRACE_ERR_BUSY;
}

static int reset_and_enable(void) {
    int i, ret = VBE_TRACE_ERR_BUSY;
    for (i = 0; i < RESET_RETRIES; ++i) {
        ret = vbeTraceReset(1);
        if (ret >= 0) return 0;
        if (ret != VBE_TRACE_ERR_BUSY) return ret;
        sceKernelDelayThread(1000);
    }
    return ret;
}

static int load_boundary(VbeTraceSnapshot *snapshot) {
    SceUID fd = sceIoOpen(BOUNDARY_FILE, SCE_O_RDONLY, 0);
    int ret;
    if (fd < 0) return 0;
    ret = read_all(fd, snapshot, (uint32_t)sizeof(*snapshot));
    sceIoClose(fd);
    if (ret < 0) return 0;
    if (snapshot->magic != VBE_TRACE_MAGIC || snapshot->version != VBE_TRACE_VERSION ||
        snapshot->firmware_version != VBE_TRACE_FW_365 ||
        (snapshot->flags & VBE_TRACE_SNAPSHOT_STABLE) == 0u) return 0;
    return 1;
}

static int save_boundary(const VbeTraceSnapshot *snapshot) {
    SceUID fd = sceIoOpen(BOUNDARY_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    int ret;
    if (fd < 0) return fd;
    ret = write_all(fd, snapshot, (uint32_t)sizeof(*snapshot));
    sceIoClose(fd);
    return ret;
}

static void sort_by_sequence(VbeTraceRecord *records, uint32_t count) {
    uint32_t i;
    for (i = 1; i < count; ++i) {
        VbeTraceRecord key = records[i];
        uint32_t j = i;
        while (j > 0 && records[j - 1].sequence > key.sequence) {
            records[j] = records[j - 1];
            --j;
        }
        records[j] = key;
    }
}

static int event_has_raw_return(uint16_t t) {
    switch (t) {
    case VBE_TRACE_CSC_A: case VBE_TRACE_CSC_B:
    case VBE_TRACE_DISPLAY_BRIGHTNESS_EXIT: case VBE_TRACE_DISPLAY_COLORSPACE_EXIT:
    case VBE_TRACE_LCD_BRIGHTNESS_EXIT: case VBE_TRACE_LCD_COLORSPACE_EXIT:
    case VBE_TRACE_DISPLAY_ON_EXIT: case VBE_TRACE_DISPLAY_OFF_EXIT:
    case VBE_TRACE_IFTU_ENABLE_EXIT: case VBE_TRACE_PANEL_WRITE:
    case VBE_TRACE_PANEL_READ_EXIT:
        return 1;
    default:
        return 0;
    }
}

static uint16_t matching_exit(uint16_t t) {
    switch (t) {
    case VBE_TRACE_DISPLAY_BRIGHTNESS_ENTER: return VBE_TRACE_DISPLAY_BRIGHTNESS_EXIT;
    case VBE_TRACE_DISPLAY_COLORSPACE_ENTER: return VBE_TRACE_DISPLAY_COLORSPACE_EXIT;
    case VBE_TRACE_LCD_BRIGHTNESS_ENTER: return VBE_TRACE_LCD_BRIGHTNESS_EXIT;
    case VBE_TRACE_LCD_COLORSPACE_ENTER: return VBE_TRACE_LCD_COLORSPACE_EXIT;
    case VBE_TRACE_DISPLAY_ON_ENTER: return VBE_TRACE_DISPLAY_ON_EXIT;
    case VBE_TRACE_DISPLAY_OFF_ENTER: return VBE_TRACE_DISPLAY_OFF_EXIT;
    case VBE_TRACE_IFTU_ENABLE_ENTER: return VBE_TRACE_IFTU_ENABLE_EXIT;
    case VBE_TRACE_PANEL_READ_ENTER: return VBE_TRACE_PANEL_READ_EXIT;
    default: return 0;
    }
}

static uint16_t matching_enter(uint16_t t) {
    switch (t) {
    case VBE_TRACE_DISPLAY_BRIGHTNESS_EXIT: return VBE_TRACE_DISPLAY_BRIGHTNESS_ENTER;
    case VBE_TRACE_DISPLAY_COLORSPACE_EXIT: return VBE_TRACE_DISPLAY_COLORSPACE_ENTER;
    case VBE_TRACE_LCD_BRIGHTNESS_EXIT: return VBE_TRACE_LCD_BRIGHTNESS_ENTER;
    case VBE_TRACE_LCD_COLORSPACE_EXIT: return VBE_TRACE_LCD_COLORSPACE_ENTER;
    case VBE_TRACE_DISPLAY_ON_EXIT: return VBE_TRACE_DISPLAY_ON_ENTER;
    case VBE_TRACE_DISPLAY_OFF_EXIT: return VBE_TRACE_DISPLAY_OFF_ENTER;
    case VBE_TRACE_IFTU_ENABLE_EXIT: return VBE_TRACE_IFTU_ENABLE_ENTER;
    case VBE_TRACE_PANEL_READ_EXIT: return VBE_TRACE_PANEL_READ_ENTER;
    default: return 0;
    }
}

static int event_payload_valid(const VbeTraceRecord *r, uint32_t *truncated_seen) {
    uint32_t allowed = VBE_TRACE_FLAG_NULL | VBE_TRACE_FLAG_TRUNCATED |
                       VBE_TRACE_FLAG_RETURN_VALID;
    if ((r->flags & ~allowed) != 0u) return 0;
    if (r->payload_length > VBE_TRACE_PAYLOAD_MAX) return 0;
    if ((r->flags & VBE_TRACE_FLAG_TRUNCATED) != 0u) *truncated_seen = 1u;
    if (event_has_raw_return(r->event_type) !=
        (((r->flags & VBE_TRACE_FLAG_RETURN_VALID) != 0u) ? 1 : 0)) return 0;
    switch (r->event_type) {
    case VBE_TRACE_CSC_A:
    case VBE_TRACE_CSC_B:
        if ((r->flags & VBE_TRACE_FLAG_NULL) != 0u) return r->payload_length == 0u;
        return r->payload_length == VBE_TRACE_CSC_PAYLOAD_SIZE;
    case VBE_TRACE_PANEL_WRITE:
        if ((r->flags & VBE_TRACE_FLAG_NULL) != 0u) return r->payload_length == 0u;
        if (r->arg1 > VBE_TRACE_PAYLOAD_MAX)
            return r->payload_length == VBE_TRACE_PAYLOAD_MAX &&
                   (r->flags & VBE_TRACE_FLAG_TRUNCATED) != 0u;
        return r->payload_length == r->arg1 &&
               (r->flags & VBE_TRACE_FLAG_TRUNCATED) == 0u;
    case VBE_TRACE_PANEL_READ_EXIT:
        if ((r->flags & VBE_TRACE_FLAG_NULL) != 0u || r->raw_return != 0)
            return r->payload_length == 0u;
        if (r->arg1 > VBE_TRACE_PAYLOAD_MAX)
            return r->payload_length == VBE_TRACE_PAYLOAD_MAX &&
                   (r->flags & VBE_TRACE_FLAG_TRUNCATED) != 0u;
        return r->payload_length == r->arg1 &&
               (r->flags & VBE_TRACE_FLAG_TRUNCATED) == 0u;
    default:
        return r->payload_length == 0u;
    }
}

static int validate_invocations(const VbeTraceRecord *records, uint32_t count) {
    uint32_t i, j;
    for (i = 0; i < count; ++i) {
        const VbeTraceRecord *r = &records[i];
        uint16_t exit_type = matching_exit(r->event_type);
        if (exit_type != 0u) {
            uint32_t found = 0;
            if (r->invocation_id == 0u) return 0;
            for (j = i + 1; j < count; ++j) {
                const VbeTraceRecord *x = &records[j];
                if (x->invocation_id != r->invocation_id) continue;
                if (x->thread_id != r->thread_id || x->event_type != exit_type) return 0;
                ++found;
            }
            if (found != 1u) return 0;
        } else if (matching_enter(r->event_type) != 0u) {
            uint32_t found = 0;
            uint16_t enter_type = matching_enter(r->event_type);
            if (r->invocation_id == 0u) return 0;
            for (j = 0; j < i; ++j) {
                const VbeTraceRecord *x = &records[j];
                if (x->invocation_id == r->invocation_id) {
                    if (x->thread_id != r->thread_id || x->event_type != enter_type) return 0;
                    ++found;
                }
            }
            if (found != 1u) return 0;
        } else if ((r->event_type == VBE_TRACE_CSC_A || r->event_type == VBE_TRACE_CSC_B ||
                    r->event_type == VBE_TRACE_PANEL_WRITE) && r->invocation_id != 0u) {
            return 0;
        }
    }
    return 1;
}

static int validate_records(VbeTraceRecord *records, uint32_t count,
                            const VbeTraceStatus *status, uint32_t *truncated_seen) {
    uint32_t i, j;
    *truncated_seen = 0;
    if (count != status->committed_records) return VBE_TRACE_ERR_MALFORMED;
    sort_by_sequence(records, count);
    for (i = 0; i < count; ++i) {
        const VbeTraceRecord *r = &records[i];
        if (r->committed != VBE_TRACE_COMMITTED) return VBE_TRACE_ERR_MALFORMED;
        if (r->sequence == 0u || r->sequence > status->last_sequence)
            return VBE_TRACE_ERR_MALFORMED;
        if (i != 0u && records[i - 1].sequence >= r->sequence)
            return VBE_TRACE_ERR_MALFORMED;
        if (r->completion_sequence == 0u || r->completion_sequence > count)
            return VBE_TRACE_ERR_MALFORMED;
        if (r->thread_id == 0u) return VBE_TRACE_ERR_MALFORMED;
        if (r->event_type == 0u || r->event_type > VBE_TRACE_EVENT_MAX)
            return VBE_TRACE_ERR_MALFORMED;
        if (!event_payload_valid(r, truncated_seen)) return VBE_TRACE_ERR_MALFORMED;
        for (j = 0; j < i; ++j)
            if (records[j].completion_sequence == r->completion_sequence)
                return VBE_TRACE_ERR_MALFORMED;
    }
    if (!validate_invocations(records, count)) return VBE_TRACE_ERR_MALFORMED;
    return 0;
}

static int write_dump(const VbeTraceDumpHeader *header, const VbeTraceStatus *status,
                      const VbeTraceSnapshot *pre, const VbeTraceSnapshot *post,
                      const VbeTraceRecord *records) {
    SceUID fd = sceIoOpen(TRACE_OUTPUT, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    int ret;
    if (fd < 0) return fd;
    ret = write_all(fd, header, (uint32_t)sizeof(*header));
    if (ret >= 0) ret = write_all(fd, status, (uint32_t)sizeof(*status));
    if (ret >= 0 && header->pre_snapshot_present != 0u)
        ret = write_all(fd, pre, (uint32_t)sizeof(*pre));
    if (ret >= 0) ret = write_all(fd, post, (uint32_t)sizeof(*post));
    if (ret >= 0 && header->record_count != 0u)
        ret = write_all(fd, records, header->record_count * (uint32_t)sizeof(VbeTraceRecord));
    sceIoClose(fd);
    return ret;
}
