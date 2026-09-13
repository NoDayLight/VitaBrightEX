#include <stdint.h>
#include <stddef.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem/data_transfers.h>
#include <psp2kern/lowio/iftu.h>
#include <taihen.h>

#include "../../../taihen_extra.h"
#include "layout_365.h"
#include "trace_protocol.h"

#define NID_IFTU_CSC_A              0x0FCBF457u
#define NID_IFTU_CSC_B              0xD64F4C6Bu
#define NID_IFTU_ENABLE             0x0D7C02F7u
#define NID_DISPLAY_BRIGHTNESS      0x9E3C6DC6u
#define NID_DISPLAY_COLORSPACE      0x8D79D187u
#define NID_LCD_BRIGHTNESS          0x581D3A87u
#define NID_LCD_COLORSPACE          0xD40968FBu
#define NID_LCD_DISPLAY_ON          0x5F4124ABu
#define NID_LCD_DISPLAY_OFF         0x1A0A7519u

#define LOWIO_PLANE_SEGMENT         1
#define LOWIO_PLANE_SEGMENT_OFFSET  0x37Cu
#define LOWIO_PLANE_STRIDE          0x214u

static VbeTraceRecord g_records[VBE_TRACE_RECORD_CAPACITY];
static volatile uint32_t g_enabled;
static volatile uint32_t g_slots_reserved;
static volatile uint32_t g_sequence;
static volatile uint32_t g_lost;
static volatile uint32_t g_active_hooks;
static volatile uint32_t g_hook_fail_mask;
static volatile uint32_t g_installed_hook_mask;
static volatile uint32_t g_snapshot_available_mask;
static uint32_t g_firmware_version;
static uintptr_t g_plane_base;
static uintptr_t g_lcd_state_base;

static tai_hook_ref_t g_ref_csc_a;
static tai_hook_ref_t g_ref_csc_b;
static tai_hook_ref_t g_ref_display_brightness;
static tai_hook_ref_t g_ref_display_colorspace;
static tai_hook_ref_t g_ref_lcd_brightness;
static tai_hook_ref_t g_ref_lcd_colorspace;
static tai_hook_ref_t g_ref_display_on;
static tai_hook_ref_t g_ref_display_off;
static tai_hook_ref_t g_ref_iftu_enable;
static tai_hook_ref_t g_ref_panel_write;
static tai_hook_ref_t g_ref_panel_read;

static SceUID g_hook_csc_a = -1;
static SceUID g_hook_csc_b = -1;
static SceUID g_hook_display_brightness = -1;
static SceUID g_hook_display_colorspace = -1;
static SceUID g_hook_lcd_brightness = -1;
static SceUID g_hook_lcd_colorspace = -1;
static SceUID g_hook_display_on = -1;
static SceUID g_hook_display_off = -1;
static SceUID g_hook_iftu_enable = -1;
static SceUID g_hook_panel_write = -1;
static SceUID g_hook_panel_read = -1;

static void copy_bytes(uint8_t *dst, const volatile uint8_t *src, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; ++i) dst[i] = src[i];
}

static void zero_bytes(void *dst, uint32_t n) {
    uint8_t *p = (uint8_t *)dst;
    uint32_t i;
    for (i = 0; i < n; ++i) p[i] = 0;
}

static int bytes_equal(const void *a, const void *b, uint32_t n) {
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    uint32_t i;
    for (i = 0; i < n; ++i) if (x[i] != y[i]) return 0;
    return 1;
}

static int exact_signature(const volatile uint8_t *address, const uint8_t *expected, uint32_t length) {
    uint32_t i;
    if (address == NULL || expected == NULL) return 0;
    for (i = 0; i < length; ++i) if (address[i] != expected[i]) return 0;
    return 1;
}

static uint32_t hook_enter(void) {
    __sync_add_and_fetch(&g_active_hooks, 1u);
    __sync_synchronize();
    return g_enabled != 0u;
}

static void hook_leave(void) {
    __sync_synchronize();
    __sync_sub_and_fetch(&g_active_hooks, 1u);
}

static VbeTraceRecord *reserve_record(uint32_t capture, uint16_t event_type, int plane) {
    uint32_t slot;
    VbeTraceRecord *r;
    if (!capture) return NULL;
    slot = __sync_fetch_and_add(&g_slots_reserved, 1u);
    if (slot >= VBE_TRACE_RECORD_CAPACITY) {
        __sync_add_and_fetch(&g_lost, 1u);
        return NULL;
    }
    r = &g_records[slot];
    r->committed = 0;
    r->sequence = __sync_add_and_fetch(&g_sequence, 1u);
    r->event_type = event_type;
    r->plane = (int16_t)plane;
    r->flags = 0;
    r->result = 0;
    r->arg0 = 0;
    r->arg1 = 0;
    r->payload_length = 0;
    r->lost_snapshot = g_lost;
    return r;
}

static void commit_record(VbeTraceRecord *r) {
    if (r == NULL) return;
    __sync_synchronize();
    r->committed = VBE_TRACE_COMMITTED;
}

static void record_simple(uint32_t capture, uint16_t event_type, int plane, uint32_t arg0, uint32_t arg1, int32_t result) {
    VbeTraceRecord *r = reserve_record(capture, event_type, plane);
    if (r == NULL) return;
    r->arg0 = arg0;
    r->arg1 = arg1;
    r->result = result;
    commit_record(r);
}

static void record_csc(uint32_t capture, uint16_t event_type, int plane, const SceIftuCscParams *params) {
    VbeTraceRecord *r = reserve_record(capture, event_type, plane);
    if (r == NULL) return;
    if (params == NULL) r->flags |= VBE_TRACE_FLAG_NULL;
    else {
        r->payload_length = (uint32_t)sizeof(SceIftuCscParams);
        copy_bytes(r->payload, (const volatile uint8_t *)params, r->payload_length);
    }
    commit_record(r);
}

static void record_panel_write(uint32_t capture, uint32_t command, const void *payload, uint32_t length) {
    VbeTraceRecord *r = reserve_record(capture, VBE_TRACE_PANEL_WRITE, -1);
    uint32_t n;
    if (r == NULL) return;
    r->arg0 = command & 0xFFu;
    r->arg1 = length;
    if (payload == NULL) {
        r->flags |= VBE_TRACE_FLAG_NULL;
        commit_record(r);
        return;
    }
    n = length;
    if (n > VBE_TRACE_PAYLOAD_MAX) {
        n = VBE_TRACE_PAYLOAD_MAX;
        r->flags |= VBE_TRACE_FLAG_TRUNCATED;
    }
    r->payload_length = n;
    copy_bytes(r->payload, (const volatile uint8_t *)payload, n);
    commit_record(r);
}

static void record_panel_read_exit(uint32_t capture, uint32_t command, const void *payload, uint32_t length, int32_t result) {
    VbeTraceRecord *r = reserve_record(capture, VBE_TRACE_PANEL_READ_EXIT, -1);
    uint32_t n;
    if (r == NULL) return;
    r->arg0 = command & 0xFFu;
    r->arg1 = length;
    r->result = result;
    if (payload == NULL) {
        r->flags |= VBE_TRACE_FLAG_NULL;
        commit_record(r);
        return;
    }
    if (result < 0) {
        commit_record(r);
        return;
    }
    n = length;
    if (n > VBE_TRACE_PAYLOAD_MAX) {
        n = VBE_TRACE_PAYLOAD_MAX;
        r->flags |= VBE_TRACE_FLAG_TRUNCATED;
    }
    r->payload_length = n;
    copy_bytes(r->payload, (const volatile uint8_t *)payload, n);
    commit_record(r);
}

static int hook_csc_a(int plane, const SceIftuCscParams *params) {
    uint32_t capture = hook_enter(); int ret;
    record_csc(capture, VBE_TRACE_CSC_A, plane, params);
    ret = TAI_CONTINUE(int, g_ref_csc_a, plane, params);
    hook_leave(); return ret;
}
static int hook_csc_b(int plane, const SceIftuCscParams *params) {
    uint32_t capture = hook_enter(); int ret;
    record_csc(capture, VBE_TRACE_CSC_B, plane, params);
    ret = TAI_CONTINUE(int, g_ref_csc_b, plane, params);
    hook_leave(); return ret;
}
static int hook_display_brightness(int display, int brightness) {
    uint32_t capture = hook_enter(); int ret;
    record_simple(capture, VBE_TRACE_DISPLAY_BRIGHTNESS_ENTER, display, (uint32_t)brightness, 0, 0);
    ret = TAI_CONTINUE(int, g_ref_display_brightness, display, brightness);
    record_simple(capture, VBE_TRACE_DISPLAY_BRIGHTNESS_EXIT, display, (uint32_t)brightness, 0, ret);
    hook_leave(); return ret;
}
static int hook_display_colorspace(int display, int mode) {
    uint32_t capture = hook_enter(); int ret;
    record_simple(capture, VBE_TRACE_DISPLAY_COLORSPACE_ENTER, display, (uint32_t)mode, 0, 0);
    ret = TAI_CONTINUE(int, g_ref_display_colorspace, display, mode);
    record_simple(capture, VBE_TRACE_DISPLAY_COLORSPACE_EXIT, display, (uint32_t)mode, 0, ret);
    hook_leave(); return ret;
}
static int hook_lcd_brightness(unsigned int brightness) {
    uint32_t capture = hook_enter(); int ret;
    record_simple(capture, VBE_TRACE_LCD_BRIGHTNESS_ENTER, -1, brightness, 0, 0);
    ret = TAI_CONTINUE(int, g_ref_lcd_brightness, brightness);
    record_simple(capture, VBE_TRACE_LCD_BRIGHTNESS_EXIT, -1, brightness, 0, ret);
    hook_leave(); return ret;
}
static int hook_lcd_colorspace(int mode) {
    uint32_t capture = hook_enter(); int ret;
    record_simple(capture, VBE_TRACE_LCD_COLORSPACE_ENTER, -1, (uint32_t)mode, 0, 0);
    ret = TAI_CONTINUE(int, g_ref_lcd_colorspace, mode);
    record_simple(capture, VBE_TRACE_LCD_COLORSPACE_EXIT, -1, (uint32_t)mode, 0, ret);
    hook_leave(); return ret;
}
static int hook_display_on(void) {
    uint32_t capture = hook_enter(); int ret;
    record_simple(capture, VBE_TRACE_DISPLAY_ON_ENTER, -1, 0, 0, 0);
    ret = TAI_CONTINUE(int, g_ref_display_on);
    record_simple(capture, VBE_TRACE_DISPLAY_ON_EXIT, -1, 0, 0, ret);
    hook_leave(); return ret;
}
static int hook_display_off(void) {
    uint32_t capture = hook_enter(); int ret;
    record_simple(capture, VBE_TRACE_DISPLAY_OFF_ENTER, -1, 0, 0, 0);
    ret = TAI_CONTINUE(int, g_ref_display_off);
    record_simple(capture, VBE_TRACE_DISPLAY_OFF_EXIT, -1, 0, 0, ret);
    hook_leave(); return ret;
}
static int hook_iftu_enable(int plane) {
    uint32_t capture = hook_enter(); int ret;
    record_simple(capture, VBE_TRACE_IFTU_ENABLE_ENTER, plane, 0, 0, 0);
    ret = TAI_CONTINUE(int, g_ref_iftu_enable, plane);
    record_simple(capture, VBE_TRACE_IFTU_ENABLE_EXIT, plane, 0, 0, ret);
    hook_leave(); return ret;
}
static int hook_panel_write(unsigned int command, const void *payload, unsigned int length) {
    uint32_t capture = hook_enter(); int ret;
    record_panel_write(capture, command, payload, length);
    ret = TAI_CONTINUE(int, g_ref_panel_write, command, payload, length);
    hook_leave(); return ret;
}
static int hook_panel_read(unsigned int command, void *payload, unsigned int length) {
    uint32_t capture = hook_enter(); int ret;
    record_simple(capture, VBE_TRACE_PANEL_READ_ENTER, -1, command & 0xFFu, length, 0);
    ret = TAI_CONTINUE(int, g_ref_panel_read, command, payload, length);
    record_panel_read_exit(capture, command, payload, length, ret);
    hook_leave(); return ret;
}

static SceUID install_export_hook(tai_hook_ref_t *ref, const char *module, uint32_t nid, const void *hook, uint32_t hook_bit, uint32_t fail_bit) {
    SceUID uid = taiHookFunctionExportForKernel(KERNEL_PID, ref, module, TAI_ANY_LIBRARY, nid, hook);
    if (uid < 0) { g_hook_fail_mask |= fail_bit; return -1; }
    g_installed_hook_mask |= hook_bit;
    return uid;
}

static int resolve_lowio_snapshot(void) {
    tai_module_info_t lowio; uintptr_t plane = 0; int ret;
    lowio.size = sizeof(lowio);
    ret = taiGetModuleInfoForKernel(KERNEL_PID, "SceLowio", &lowio); if (ret < 0) return ret;
    ret = module_get_offset(KERNEL_PID, lowio.modid, LOWIO_PLANE_SEGMENT, LOWIO_PLANE_SEGMENT_OFFSET, &plane); if (ret < 0) return ret;
    g_plane_base = plane;
    g_snapshot_available_mask |= VBE_TRACE_SNAPSHOT_LOWIO;
    return 0;
}

static int setup_lcd_private_paths(void) {
    tai_module_info_t lcd; uintptr_t state = 0, writer = 0, reader = 0; int ret;
    lcd.size = sizeof(lcd);
    ret = taiGetModuleInfoForKernel(KERNEL_PID, "SceLcd", &lcd);
    if (ret < 0) {
        g_hook_fail_mask |= VBE_TRACE_FAIL_PANEL_WRITE | VBE_TRACE_FAIL_PANEL_READ | VBE_TRACE_FAIL_LCD_SNAPSHOT;
        return ret;
    }
    ret = module_get_offset(KERNEL_PID, lcd.modid, VBE_LCD_STATE_SEGMENT, VBE_LCD_STATE_OFFSET, &state);
    if (ret >= 0) { g_lcd_state_base = state; g_snapshot_available_mask |= VBE_TRACE_SNAPSHOT_LCD; }
    else { g_lcd_state_base = 0; g_hook_fail_mask |= VBE_TRACE_FAIL_LCD_SNAPSHOT; }

    ret = module_get_offset(KERNEL_PID, lcd.modid, VBE_LCD_PANEL_SEGMENT, VBE_LCD_PANEL_WRITER_OFFSET, &writer);
    if (ret < 0 || !exact_signature((const volatile uint8_t *)writer, vbe_lcd_panel_writer_signature, VBE_LCD_PRIVATE_SIGNATURE_LENGTH)) g_hook_fail_mask |= VBE_TRACE_FAIL_PANEL_WRITE_SIG;
    else {
        g_hook_panel_write = taiHookFunctionOffsetForKernel(KERNEL_PID, &g_ref_panel_write, lcd.modid, VBE_LCD_PANEL_SEGMENT, VBE_LCD_PANEL_WRITER_OFFSET, 1, hook_panel_write);
        if (g_hook_panel_write < 0) { g_hook_panel_write = -1; g_hook_fail_mask |= VBE_TRACE_FAIL_PANEL_WRITE; }
        else g_installed_hook_mask |= VBE_TRACE_HOOK_PANEL_WRITE;
    }
    ret = module_get_offset(KERNEL_PID, lcd.modid, VBE_LCD_PANEL_SEGMENT, VBE_LCD_PANEL_READER_OFFSET, &reader);
    if (ret < 0 || !exact_signature((const volatile uint8_t *)reader, vbe_lcd_panel_reader_signature, VBE_LCD_PRIVATE_SIGNATURE_LENGTH)) g_hook_fail_mask |= VBE_TRACE_FAIL_PANEL_READ_SIG;
    else {
        g_hook_panel_read = taiHookFunctionOffsetForKernel(KERNEL_PID, &g_ref_panel_read, lcd.modid, VBE_LCD_PANEL_SEGMENT, VBE_LCD_PANEL_READER_OFFSET, 1, hook_panel_read);
        if (g_hook_panel_read < 0) { g_hook_panel_read = -1; g_hook_fail_mask |= VBE_TRACE_FAIL_PANEL_READ; }
        else g_installed_hook_mask |= VBE_TRACE_HOOK_PANEL_READ;
    }
    return 0;
}

static void clear_records(void) {
    uint32_t i, j;
    for (i = 0; i < VBE_TRACE_RECORD_CAPACITY; ++i) {
        volatile uint32_t *w = (volatile uint32_t *)&g_records[i];
        for (j = 0; j < (uint32_t)(sizeof(VbeTraceRecord) / sizeof(uint32_t)); ++j) w[j] = 0;
    }
}
static uint32_t count_committed_records(uint32_t slots) {
    uint32_t i, limit = slots, count = 0;
    if (limit > VBE_TRACE_RECORD_CAPACITY) limit = VBE_TRACE_RECORD_CAPACITY;
    for (i = 0; i < limit; ++i) if (g_records[i].committed == VBE_TRACE_COMMITTED) ++count;
    return count;
}
static void snapshot_data_read(VbeTraceSnapshotData *data, uint32_t available_mask) {
    uint32_t i; zero_bytes(data, (uint32_t)sizeof(*data));
    if ((available_mask & VBE_TRACE_SNAPSHOT_LOWIO) != 0u) {
        for (i = 0; i < VBE_TRACE_PLANE_COUNT; ++i) {
            const volatile uint8_t *p = (const volatile uint8_t *)(g_plane_base + i * LOWIO_PLANE_STRIDE);
            VbeTracePlaneSnapshot *d = &data->planes[i];
            d->active_state_1e8 = *(const volatile uint32_t *)(p + 0x1E8);
            d->csc_control_100 = *(const volatile uint32_t *)(p + 0x100);
            d->private_control_1f8 = *(const volatile uint32_t *)(p + 0x1F8);
            copy_bytes(d->csc_a_10c, p + 0x10C, VBE_TRACE_CSC_PAYLOAD_SIZE);
            copy_bytes(d->csc_b_148, p + 0x148, VBE_TRACE_CSC_PAYLOAD_SIZE);
        }
    }
    if ((available_mask & VBE_TRACE_SNAPSHOT_LCD) != 0u) {
        const volatile uint8_t *p = (const volatile uint8_t *)g_lcd_state_base;
        data->lcd.ddb_08 = *(const volatile uint16_t *)(p + 0x08);
        data->lcd.ddb_0a = *(const volatile uint16_t *)(p + 0x0A);
        data->lcd.bucket_0c = *(const volatile uint16_t *)(p + 0x0C);
        data->lcd.brightness_1c = *(const volatile uint32_t *)(p + 0x1C);
        data->lcd.secondary_program_28 = *(const volatile uint32_t *)(p + 0x28);
        data->lcd.color_space_mode_2c = *(const volatile uint32_t *)(p + 0x2C);
    }
}

int vbeTraceGetStatus(VbeTraceStatus *out) {
    VbeTraceStatus s; uint32_t cpu_state, slots = g_slots_reserved; int ret;
    if (out == NULL) return VBE_TRACE_ERR_INVALID;
    s.magic=VBE_TRACE_MAGIC; s.version=VBE_TRACE_VERSION; s.firmware_version=g_firmware_version; s.enabled=g_enabled;
    s.record_capacity=VBE_TRACE_RECORD_CAPACITY; s.slots_reserved=slots; s.committed_records=count_committed_records(slots); s.lost_records=g_lost;
    s.last_sequence=g_sequence; s.active_hooks=g_active_hooks; s.installed_hook_mask=g_installed_hook_mask; s.required_hook_mask=VBE_TRACE_REQUIRED_HOOKS;
    s.missing_required_mask=VBE_TRACE_REQUIRED_HOOKS & ~g_installed_hook_mask; s.snapshot_available_mask=g_snapshot_available_mask;
    s.required_snapshot_mask=VBE_TRACE_REQUIRED_SNAPSHOTS; s.missing_required_snapshot_mask=VBE_TRACE_REQUIRED_SNAPSHOTS & ~g_snapshot_available_mask; s.hook_fail_mask=g_hook_fail_mask;
    ENTER_SYSCALL(cpu_state); ret=ksceKernelMemcpyKernelToUser(out,&s,sizeof(s)); EXIT_SYSCALL(cpu_state); return ret;
}
int vbeTraceStop(void) { g_enabled=0; __sync_synchronize(); return 0; }
int vbeTraceReset(int enable_after_reset) {
    g_enabled = 0; __sync_synchronize();
    if (g_active_hooks != 0u) return VBE_TRACE_ERR_BUSY;
    clear_records(); g_slots_reserved=0; g_sequence=0; g_lost=0; __sync_synchronize();
    g_enabled = enable_after_reset ? 1u : 0u; return 0;
}
int vbeTraceRead(VbeTraceRecord *out, uint32_t capacity, uint32_t *written) {
    uint32_t cpu_state, count; int ret;
    if (out==NULL || written==NULL) return VBE_TRACE_ERR_INVALID;
    if (g_enabled!=0u || g_active_hooks!=0u) return VBE_TRACE_ERR_BUSY;
    count=g_slots_reserved; if (count>VBE_TRACE_RECORD_CAPACITY) count=VBE_TRACE_RECORD_CAPACITY; if (count>capacity) count=capacity;
    ENTER_SYSCALL(cpu_state); ret=ksceKernelMemcpyKernelToUser(out,g_records,(SceSize)(count*sizeof(VbeTraceRecord)));
    if (ret>=0) ret=ksceKernelMemcpyKernelToUser(written,&count,sizeof(count)); EXIT_SYSCALL(cpu_state); return ret;
}
int vbeTraceSnapshot(VbeTraceSnapshot *out) {
    VbeTraceSnapshotData a,b; VbeTraceSnapshot snap; uint32_t cpu_state,available; int ret;
    if (out==NULL) return VBE_TRACE_ERR_INVALID;
    if (g_firmware_version!=VBE_TRACE_FW_365) return VBE_TRACE_ERR_UNAVAILABLE;
    if (g_enabled!=0u) return VBE_TRACE_ERR_BUSY; __sync_synchronize(); if (g_active_hooks!=0u) return VBE_TRACE_ERR_BUSY;
    available=g_snapshot_available_mask;
    snapshot_data_read(&a,available); __sync_synchronize(); snapshot_data_read(&b,available); __sync_synchronize();
    if (g_enabled!=0u || g_active_hooks!=0u) return VBE_TRACE_ERR_BUSY;
    snap.magic=VBE_TRACE_MAGIC; snap.version=VBE_TRACE_VERSION; snap.firmware_version=g_firmware_version; snap.available_mask=available;
    snap.flags=bytes_equal(&a,&b,(uint32_t)sizeof(a)) ? VBE_TRACE_SNAPSHOT_STABLE : 0u; snap.data=b;
    ENTER_SYSCALL(cpu_state); ret=ksceKernelMemcpyKernelToUser(out,&snap,sizeof(snap)); EXIT_SYSCALL(cpu_state); return ret;
}

static int release_owned_hook(SceUID *uid, tai_hook_ref_t ref, uint32_t hook_bit) {
    int ret; if (*uid<0) return 0; ret=taiHookReleaseForKernel(*uid,ref); if (ret<0) return ret;
    *uid=-1; g_installed_hook_mask &= ~hook_bit; return 0;
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize argc, const void *args) {
    SceKernelFwInfo fw; (void)argc; (void)args;
    g_enabled=0; g_slots_reserved=0; g_sequence=0; g_lost=0; g_active_hooks=0; g_hook_fail_mask=0; g_installed_hook_mask=0; g_snapshot_available_mask=0; g_plane_base=0; g_lcd_state_base=0; clear_records();
    fw.size=sizeof(fw); if (ksceKernelGetSystemSwVersion(&fw)<0) { g_hook_fail_mask=VBE_TRACE_FAIL_UNSUPPORTED_FW; return SCE_KERNEL_START_SUCCESS; }
    g_firmware_version=fw.version; if (fw.version!=VBE_TRACE_FW_365) { g_hook_fail_mask=VBE_TRACE_FAIL_UNSUPPORTED_FW; return SCE_KERNEL_START_SUCCESS; }
    if (resolve_lowio_snapshot()<0) { g_plane_base=0; g_snapshot_available_mask &= ~VBE_TRACE_SNAPSHOT_LOWIO; g_hook_fail_mask |= VBE_TRACE_FAIL_LOWIO_SNAPSHOT; }
    (void)setup_lcd_private_paths();
    g_hook_csc_a=install_export_hook(&g_ref_csc_a,"SceLowio",NID_IFTU_CSC_A,hook_csc_a,VBE_TRACE_HOOK_CSC_A,VBE_TRACE_FAIL_CSC_A);
    g_hook_csc_b=install_export_hook(&g_ref_csc_b,"SceLowio",NID_IFTU_CSC_B,hook_csc_b,VBE_TRACE_HOOK_CSC_B,VBE_TRACE_FAIL_CSC_B);
    g_hook_iftu_enable=install_export_hook(&g_ref_iftu_enable,"SceLowio",NID_IFTU_ENABLE,hook_iftu_enable,VBE_TRACE_HOOK_IFTU_ENABLE,VBE_TRACE_FAIL_IFTU_ENABLE);
    g_hook_display_brightness=install_export_hook(&g_ref_display_brightness,"SceDisplay",NID_DISPLAY_BRIGHTNESS,hook_display_brightness,VBE_TRACE_HOOK_DISPLAY_BRIGHT,VBE_TRACE_FAIL_DISPLAY_BRIGHT);
    g_hook_display_colorspace=install_export_hook(&g_ref_display_colorspace,"SceDisplay",NID_DISPLAY_COLORSPACE,hook_display_colorspace,VBE_TRACE_HOOK_DISPLAY_COLOR,VBE_TRACE_FAIL_DISPLAY_COLOR);
    g_hook_lcd_brightness=install_export_hook(&g_ref_lcd_brightness,"SceLcd",NID_LCD_BRIGHTNESS,hook_lcd_brightness,VBE_TRACE_HOOK_LCD_BRIGHT,VBE_TRACE_FAIL_LCD_BRIGHT);
    g_hook_lcd_colorspace=install_export_hook(&g_ref_lcd_colorspace,"SceLcd",NID_LCD_COLORSPACE,hook_lcd_colorspace,VBE_TRACE_HOOK_LCD_COLOR,VBE_TRACE_FAIL_LCD_COLOR);
    g_hook_display_on=install_export_hook(&g_ref_display_on,"SceLcd",NID_LCD_DISPLAY_ON,hook_display_on,VBE_TRACE_HOOK_DISPLAY_ON,VBE_TRACE_FAIL_DISPLAY_ON);
    g_hook_display_off=install_export_hook(&g_ref_display_off,"SceLcd",NID_LCD_DISPLAY_OFF,hook_display_off,VBE_TRACE_HOOK_DISPLAY_OFF,VBE_TRACE_FAIL_DISPLAY_OFF);
    g_enabled=1; return SCE_KERNEL_START_SUCCESS;
}
int module_stop(SceSize argc, const void *args) {
    int failed=0; (void)argc; (void)args; g_enabled=0; __sync_synchronize(); if (g_active_hooks!=0u) return SCE_KERNEL_STOP_FAIL;
    if (release_owned_hook(&g_hook_panel_read,g_ref_panel_read,VBE_TRACE_HOOK_PANEL_READ)<0) failed=1;
    if (release_owned_hook(&g_hook_panel_write,g_ref_panel_write,VBE_TRACE_HOOK_PANEL_WRITE)<0) failed=1;
    if (release_owned_hook(&g_hook_display_off,g_ref_display_off,VBE_TRACE_HOOK_DISPLAY_OFF)<0) failed=1;
    if (release_owned_hook(&g_hook_display_on,g_ref_display_on,VBE_TRACE_HOOK_DISPLAY_ON)<0) failed=1;
    if (release_owned_hook(&g_hook_lcd_colorspace,g_ref_lcd_colorspace,VBE_TRACE_HOOK_LCD_COLOR)<0) failed=1;
    if (release_owned_hook(&g_hook_lcd_brightness,g_ref_lcd_brightness,VBE_TRACE_HOOK_LCD_BRIGHT)<0) failed=1;
    if (release_owned_hook(&g_hook_display_colorspace,g_ref_display_colorspace,VBE_TRACE_HOOK_DISPLAY_COLOR)<0) failed=1;
    if (release_owned_hook(&g_hook_display_brightness,g_ref_display_brightness,VBE_TRACE_HOOK_DISPLAY_BRIGHT)<0) failed=1;
    if (release_owned_hook(&g_hook_iftu_enable,g_ref_iftu_enable,VBE_TRACE_HOOK_IFTU_ENABLE)<0) failed=1;
    if (release_owned_hook(&g_hook_csc_b,g_ref_csc_b,VBE_TRACE_HOOK_CSC_B)<0) failed=1;
    if (release_owned_hook(&g_hook_csc_a,g_ref_csc_a,VBE_TRACE_HOOK_CSC_A)<0) failed=1;
    return failed ? SCE_KERNEL_STOP_FAIL : SCE_KERNEL_STOP_SUCCESS;
}
