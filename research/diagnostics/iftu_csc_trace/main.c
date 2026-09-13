#include <stdint.h>
#include <stddef.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem/data_transfers.h>
#include <psp2kern/lowio/iftu.h>
#include <taihen.h>

#include "../../../taihen_extra.h"
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
#define LCD_PANEL_SEGMENT           0
#define LCD_PANEL_WRITER_OFFSET     0xA54u

static VbeTraceRecord g_records[VBE_TRACE_RECORD_CAPACITY];
static volatile uint32_t g_enabled;
static volatile uint32_t g_slots_reserved;
static volatile uint32_t g_sequence;
static volatile uint32_t g_lost;
static volatile uint32_t g_active_hooks;
static volatile uint32_t g_hook_fail_mask;
static uint32_t g_firmware_version;
static uintptr_t g_plane_base;

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

static void copy_bytes(uint8_t *dst, const volatile uint8_t *src, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; ++i) dst[i] = src[i];
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

static void record_simple(uint32_t capture, uint16_t event_type, int plane,
                          uint32_t arg0, uint32_t arg1, int32_t result) {
    VbeTraceRecord *r = reserve_record(capture, event_type, plane);
    if (r == NULL) return;
    r->arg0 = arg0;
    r->arg1 = arg1;
    r->result = result;
    commit_record(r);
}

static void record_csc(uint32_t capture, uint16_t event_type, int plane,
                       const SceIftuCscParams *params) {
    VbeTraceRecord *r = reserve_record(capture, event_type, plane);
    if (r == NULL) return;
    if (params == NULL) {
        r->flags |= VBE_TRACE_FLAG_NULL;
    } else {
        r->payload_length = (uint32_t)sizeof(SceIftuCscParams);
        copy_bytes(r->payload, (const volatile uint8_t *)params, r->payload_length);
    }
    commit_record(r);
}

static void record_panel_write(uint32_t capture, uint32_t command,
                               const void *payload, uint32_t length) {
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

static int hook_csc_a(int plane, const SceIftuCscParams *params) {
    uint32_t capture = hook_enter();
    int ret;
    record_csc(capture, VBE_TRACE_CSC_A, plane, params);
    ret = TAI_CONTINUE(int, g_ref_csc_a, plane, params);
    hook_leave();
    return ret;
}

static int hook_csc_b(int plane, const SceIftuCscParams *params) {
    uint32_t capture = hook_enter();
    int ret;
    record_csc(capture, VBE_TRACE_CSC_B, plane, params);
    ret = TAI_CONTINUE(int, g_ref_csc_b, plane, params);
    hook_leave();
    return ret;
}

static int hook_display_brightness(int display, int brightness) {
    uint32_t capture = hook_enter();
    int ret;
    record_simple(capture, VBE_TRACE_DISPLAY_BRIGHTNESS_ENTER, display,
                  (uint32_t)brightness, 0, 0);
    ret = TAI_CONTINUE(int, g_ref_display_brightness, display, brightness);
    record_simple(capture, VBE_TRACE_DISPLAY_BRIGHTNESS_EXIT, display,
                  (uint32_t)brightness, 0, ret);
    hook_leave();
    return ret;
}

static int hook_display_colorspace(int display, int mode) {
    uint32_t capture = hook_enter();
    int ret;
    record_simple(capture, VBE_TRACE_DISPLAY_COLORSPACE_ENTER, display,
                  (uint32_t)mode, 0, 0);
    ret = TAI_CONTINUE(int, g_ref_display_colorspace, display, mode);
    record_simple(capture, VBE_TRACE_DISPLAY_COLORSPACE_EXIT, display,
                  (uint32_t)mode, 0, ret);
    hook_leave();
    return ret;
}

static int hook_lcd_brightness(unsigned int brightness) {
    uint32_t capture = hook_enter();
    int ret;
    record_simple(capture, VBE_TRACE_LCD_BRIGHTNESS_ENTER, -1, brightness, 0, 0);
    ret = TAI_CONTINUE(int, g_ref_lcd_brightness, brightness);
    record_simple(capture, VBE_TRACE_LCD_BRIGHTNESS_EXIT, -1, brightness, 0, ret);
    hook_leave();
    return ret;
}

static int hook_lcd_colorspace(int mode) {
    uint32_t capture = hook_enter();
    int ret;
    record_simple(capture, VBE_TRACE_LCD_COLORSPACE_ENTER, -1, (uint32_t)mode, 0, 0);
    ret = TAI_CONTINUE(int, g_ref_lcd_colorspace, mode);
    record_simple(capture, VBE_TRACE_LCD_COLORSPACE_EXIT, -1, (uint32_t)mode, 0, ret);
    hook_leave();
    return ret;
}

static int hook_display_on(void) {
    uint32_t capture = hook_enter();
    int ret = TAI_CONTINUE(int, g_ref_display_on);
    record_simple(capture, VBE_TRACE_DISPLAY_ON, -1, 0, 0, ret);
    hook_leave();
    return ret;
}

static int hook_display_off(void) {
    uint32_t capture = hook_enter();
    int ret = TAI_CONTINUE(int, g_ref_display_off);
    record_simple(capture, VBE_TRACE_DISPLAY_OFF, -1, 0, 0, ret);
    hook_leave();
    return ret;
}

static int hook_iftu_enable(int plane) {
    uint32_t capture = hook_enter();
    int ret;
    record_simple(capture, VBE_TRACE_IFTU_ENABLE, plane, 0, 0, 0);
    ret = TAI_CONTINUE(int, g_ref_iftu_enable, plane);
    hook_leave();
    return ret;
}

static int hook_panel_write(unsigned int command, const void *payload, unsigned int length) {
    uint32_t capture = hook_enter();
    int ret;
    record_panel_write(capture, command, payload, length);
    ret = TAI_CONTINUE(int, g_ref_panel_write, command, payload, length);
    hook_leave();
    return ret;
}

static SceUID install_export_hook(tai_hook_ref_t *ref, const char *module,
                                  uint32_t nid, const void *hook, uint32_t fail_bit) {
    SceUID uid = taiHookFunctionExportForKernel(KERNEL_PID, ref, module,
                                                TAI_ANY_LIBRARY, nid, hook);
    if (uid < 0) {
        g_hook_fail_mask |= fail_bit;
        return -1;
    }
    return uid;
}

static int resolve_runtime_layout(void) {
    tai_module_info_t lowio;
    tai_module_info_t lcd;
    uintptr_t plane = 0;
    int ret;

    lowio.size = sizeof(lowio);
    ret = taiGetModuleInfoForKernel(KERNEL_PID, "SceLowio", &lowio);
    if (ret < 0) return ret;
    ret = module_get_offset(KERNEL_PID, lowio.modid, LOWIO_PLANE_SEGMENT,
                            LOWIO_PLANE_SEGMENT_OFFSET, &plane);
    if (ret < 0) return ret;
    g_plane_base = plane;

    lcd.size = sizeof(lcd);
    ret = taiGetModuleInfoForKernel(KERNEL_PID, "SceLcd", &lcd);
    if (ret < 0) return ret;
    g_hook_panel_write = taiHookFunctionOffsetForKernel(
        KERNEL_PID, &g_ref_panel_write, lcd.modid, LCD_PANEL_SEGMENT,
        LCD_PANEL_WRITER_OFFSET, 1, hook_panel_write);
    if (g_hook_panel_write < 0) {
        g_hook_panel_write = -1;
        g_hook_fail_mask |= VBE_TRACE_FAIL_PANEL_WRITE;
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

int vbeTraceGetStatus(VbeTraceStatus *out) {
    VbeTraceStatus s;
    uint32_t state;
    uint32_t slots = g_slots_reserved;
    uint32_t count = slots < VBE_TRACE_RECORD_CAPACITY ? slots : VBE_TRACE_RECORD_CAPACITY;
    if (out == NULL) return -1;
    s.magic = VBE_TRACE_MAGIC;
    s.version = VBE_TRACE_VERSION;
    s.firmware_version = g_firmware_version;
    s.enabled = g_enabled;
    s.record_capacity = VBE_TRACE_RECORD_CAPACITY;
    s.slots_reserved = slots;
    s.committed_records = count;
    s.lost_records = g_lost;
    s.last_sequence = g_sequence;
    s.active_hooks = g_active_hooks;
    s.hook_fail_mask = g_hook_fail_mask;
    s.snapshot_available = g_plane_base != 0;
    ENTER_SYSCALL(state);
    state = (uint32_t)ksceKernelMemcpyKernelToUser((uintptr_t)out, &s, sizeof(s));
    EXIT_SYSCALL(state);
    return (int)state;
}

int vbeTraceStop(void) {
    g_enabled = 0;
    __sync_synchronize();
    return 0;
}

int vbeTraceReset(int enable_after_reset) {
    if (g_active_hooks != 0u) return -2;
    g_enabled = 0;
    __sync_synchronize();
    clear_records();
    g_slots_reserved = 0;
    g_sequence = 0;
    g_lost = 0;
    __sync_synchronize();
    g_enabled = enable_after_reset ? 1u : 0u;
    return 0;
}

int vbeTraceRead(VbeTraceRecord *out, uint32_t capacity, uint32_t *written) {
    uint32_t state;
    uint32_t count;
    int ret;
    if (out == NULL || written == NULL) return -1;
    if (g_enabled != 0u || g_active_hooks != 0u) return -2;
    count = g_slots_reserved;
    if (count > VBE_TRACE_RECORD_CAPACITY) count = VBE_TRACE_RECORD_CAPACITY;
    if (count > capacity) count = capacity;

    ENTER_SYSCALL(state);
    ret = ksceKernelMemcpyKernelToUser((uintptr_t)out, g_records,
                                       (SceSize)(count * sizeof(VbeTraceRecord)));
    if (ret >= 0)
        ret = ksceKernelMemcpyKernelToUser((uintptr_t)written, &count, sizeof(count));
    EXIT_SYSCALL(state);
    return ret;
}

int vbeTraceSnapshot(VbeTraceSnapshot *out) {
    VbeTraceSnapshot snap;
    uint32_t i, state;
    int ret;
    if (out == NULL) return -1;
    if (g_firmware_version != VBE_TRACE_FW_365 || g_plane_base == 0) return -3;
    if (g_active_hooks != 0u) return -2;

    snap.magic = VBE_TRACE_MAGIC;
    snap.version = VBE_TRACE_VERSION;
    snap.firmware_version = g_firmware_version;
    snap.plane_count = VBE_TRACE_PLANE_COUNT;
    for (i = 0; i < VBE_TRACE_PLANE_COUNT; ++i) {
        const volatile uint8_t *p = (const volatile uint8_t *)(g_plane_base + i * LOWIO_PLANE_STRIDE);
        VbeTracePlaneSnapshot *d = &snap.planes[i];
        d->active_state_1e8 = *(const volatile uint32_t *)(p + 0x1E8);
        d->csc_control_100 = *(const volatile uint32_t *)(p + 0x100);
        d->private_control_1f8 = *(const volatile uint32_t *)(p + 0x1F8);
        d->reserved = 0;
        copy_bytes(d->csc_a_10c, p + 0x10C, 0x3C);
        copy_bytes(d->csc_b_148, p + 0x148, 0x3C);
    }

    ENTER_SYSCALL(state);
    ret = ksceKernelMemcpyKernelToUser((uintptr_t)out, &snap, sizeof(snap));
    EXIT_SYSCALL(state);
    return ret;
}

static void release_hook(SceUID *uid, tai_hook_ref_t ref) {
    if (*uid >= 0) {
        if (taiHookReleaseForKernel(*uid, ref) >= 0) *uid = -1;
    }
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize argc, const void *args) {
    SceKernelFwInfo fw;
    (void)argc;
    (void)args;

    g_enabled = 0;
    g_slots_reserved = 0;
    g_sequence = 0;
    g_lost = 0;
    g_active_hooks = 0;
    g_hook_fail_mask = 0;
    g_plane_base = 0;
    clear_records();

    fw.size = sizeof(fw);
    if (ksceKernelGetSystemSwVersion(&fw) < 0) {
        g_hook_fail_mask = VBE_TRACE_FAIL_UNSUPPORTED_FW;
        return SCE_KERNEL_START_SUCCESS;
    }
    g_firmware_version = fw.version;
    if (fw.version != VBE_TRACE_FW_365) {
        g_hook_fail_mask = VBE_TRACE_FAIL_UNSUPPORTED_FW;
        return SCE_KERNEL_START_SUCCESS;
    }

    if (resolve_runtime_layout() < 0) {
        g_plane_base = 0;
        g_hook_fail_mask |= VBE_TRACE_FAIL_LOWIO_SNAPSHOT;
    }

    g_hook_csc_a = install_export_hook(&g_ref_csc_a, "SceLowio", NID_IFTU_CSC_A,
                                       hook_csc_a, VBE_TRACE_FAIL_CSC_A);
    g_hook_csc_b = install_export_hook(&g_ref_csc_b, "SceLowio", NID_IFTU_CSC_B,
                                       hook_csc_b, VBE_TRACE_FAIL_CSC_B);
    g_hook_iftu_enable = install_export_hook(&g_ref_iftu_enable, "SceLowio", NID_IFTU_ENABLE,
                                             hook_iftu_enable, VBE_TRACE_FAIL_IFTU_ENABLE);
    g_hook_display_brightness = install_export_hook(&g_ref_display_brightness, "SceDisplay",
                                                     NID_DISPLAY_BRIGHTNESS, hook_display_brightness,
                                                     VBE_TRACE_FAIL_DISPLAY_BRIGHT);
    g_hook_display_colorspace = install_export_hook(&g_ref_display_colorspace, "SceDisplay",
                                                     NID_DISPLAY_COLORSPACE, hook_display_colorspace,
                                                     VBE_TRACE_FAIL_DISPLAY_COLOR);
    g_hook_lcd_brightness = install_export_hook(&g_ref_lcd_brightness, "SceLcd", NID_LCD_BRIGHTNESS,
                                                 hook_lcd_brightness, VBE_TRACE_FAIL_LCD_BRIGHT);
    g_hook_lcd_colorspace = install_export_hook(&g_ref_lcd_colorspace, "SceLcd", NID_LCD_COLORSPACE,
                                                 hook_lcd_colorspace, VBE_TRACE_FAIL_LCD_COLOR);
    g_hook_display_on = install_export_hook(&g_ref_display_on, "SceLcd", NID_LCD_DISPLAY_ON,
                                             hook_display_on, VBE_TRACE_FAIL_DISPLAY_ON);
    g_hook_display_off = install_export_hook(&g_ref_display_off, "SceLcd", NID_LCD_DISPLAY_OFF,
                                              hook_display_off, VBE_TRACE_FAIL_DISPLAY_OFF);

    g_enabled = 1;
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
    (void)argc;
    (void)args;
    g_enabled = 0;
    __sync_synchronize();
    if (g_active_hooks != 0u) return SCE_KERNEL_STOP_FAIL;

    release_hook(&g_hook_panel_write, g_ref_panel_write);
    release_hook(&g_hook_display_off, g_ref_display_off);
    release_hook(&g_hook_display_on, g_ref_display_on);
    release_hook(&g_hook_lcd_colorspace, g_ref_lcd_colorspace);
    release_hook(&g_hook_lcd_brightness, g_ref_lcd_brightness);
    release_hook(&g_hook_display_colorspace, g_ref_display_colorspace);
    release_hook(&g_hook_display_brightness, g_ref_display_brightness);
    release_hook(&g_hook_iftu_enable, g_ref_iftu_enable);
    release_hook(&g_hook_csc_b, g_ref_csc_b);
    release_hook(&g_hook_csc_a, g_ref_csc_a);
    return SCE_KERNEL_STOP_SUCCESS;
}
