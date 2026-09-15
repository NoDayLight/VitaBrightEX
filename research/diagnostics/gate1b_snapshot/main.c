#include <stdint.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/suspend.h>
#include <psp2kern/kernel/sysmem/data_transfers.h>
#include <taihen.h>

#include "../../../taihen_extra.h"
#include "gate1b_snapshot_protocol.h"

#define CACHE_A_OFFSET 0x10Cu
#define CACHE_B_OFFSET 0x148u
#define ENABLE_STATE_OFFSET 0x1E8u
#define PRIVATE_CONTROL_OFFSET 0x1F8u
#define LIVE_CSC_CONTROL_OFFSET 0x100u

static const uint32_t g_canonical_a[15] = {
    0x00000000u, 0x00000202u, 0x000003FFu, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u
};
static const uint32_t g_canonical_b[15] = {
    0x00000000u, 0x00000000u, 0x000003FFu, 0x00000000u, 0x000003FFu,
    0x00000000u, 0x00000200u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000200u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000200u
};

typedef char canonical_a_size[(sizeof(g_canonical_a) == VBE_G1B_CSC_SIZE) ? 1 : -1];
typedef char canonical_b_size[(sizeof(g_canonical_b) == VBE_G1B_CSC_SIZE) ? 1 : -1];

static uintptr_t g_plane_state[VBE_G1B_PLANE_COUNT];
static uint32_t g_status_flags;
static uint32_t g_firmware_version;
static uint32_t g_lowio_modid;
static uint32_t g_lowio_module_nid;
static uint32_t g_observer_mask;
static uint32_t g_fail_mask;
static volatile uint32_t g_capture_sequence;
static volatile uint32_t g_pre_event_count;
static volatile uint32_t g_post_event_count;
static volatile uint32_t g_pre_claimed;
static volatile uint32_t g_pre_published;
static volatile uint32_t g_post_claimed;
static volatile uint32_t g_post_published;
static uint32_t g_pre_reason;
static uint32_t g_post_reason;
static int g_sysevent_registered;
static VbeG1bSnapshot g_pre_snapshot;
static VbeG1bSnapshot g_post_snapshot;

static void zero_bytes(void *dst, uint32_t n) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    uint32_t i;
    for (i = 0; i < n; ++i) p[i] = 0u;
}

static void copy_bytes_volatile(void *dst, const void *src, uint32_t n) {
    volatile uint8_t *d = (volatile uint8_t *)dst;
    const volatile uint8_t *s = (const volatile uint8_t *)src;
    uint32_t i;
    for (i = 0; i < n; ++i) d[i] = s[i];
}

static uint32_t read32(uintptr_t addr) {
    return *(const volatile uint32_t *)addr;
}

static void copy_3c_from_volatile(uint8_t dst[VBE_G1B_CSC_SIZE], uintptr_t src) {
    const volatile uint8_t *p = (const volatile uint8_t *)src;
    uint32_t i;
    for (i = 0; i < VBE_G1B_CSC_SIZE; ++i) dst[i] = p[i];
}

static int bytes_equal(const uint8_t *a, const uint8_t *b, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

/*
 * Gate-1B v4 authorization is intentionally plane-qualified and exact.
 * These are the state[0] register bases physically observed on retail 3.65
 * in v3 capture SHA-256 4770047b0ba25a2e1d25f023828fc634039eeb19ae79e0c1b2c90b91c32eb1a1.
 * Retail A/B setters load this same state[0] value and dereference it directly
 * for their +0x104..+0x168 register writes. Any other value remains unread.
 */
static int authorized_runtime_mmio(uint32_t plane, uint32_t base) {
    if (plane == 0u) return base == VBE_G1B_PLANE0_RUNTIME_MMIO;
    if (plane == 1u) return base == VBE_G1B_PLANE1_RUNTIME_MMIO;
    return 0;
}

static void capture_plane(VbeG1bPlaneSnapshot *p, uint32_t plane, uintptr_t state) {
    uint32_t mmio;
    p->plane = plane;
    p->state_address = (uint32_t)state;
    if (state == 0u) return;

    p->flags |= VBE_G1B_PLANE_STATE_RESOLVED;
    p->enable_state_before = read32(state + ENABLE_STATE_OFFSET);
    copy_3c_from_volatile(p->cache_a, state + CACHE_A_OFFSET);
    copy_3c_from_volatile(p->cache_b, state + CACHE_B_OFFSET);
    p->private_control = read32(state + PRIVATE_CONTROL_OFFSET);
    p->flags |= VBE_G1B_PLANE_CACHE_READ;
    if (bytes_equal(p->cache_a, (const uint8_t *)g_canonical_a, VBE_G1B_CSC_SIZE))
        p->flags |= VBE_G1B_PLANE_CANONICAL_A;
    if (bytes_equal(p->cache_b, (const uint8_t *)g_canonical_b, VBE_G1B_CSC_SIZE))
        p->flags |= VBE_G1B_PLANE_CANONICAL_B;

    mmio = read32(state);
    p->mmio_base = mmio;
    if (authorized_runtime_mmio(plane, mmio)) {
        p->flags |= VBE_G1B_PLANE_MMIO_WHITELISTED;
        p->live_csc_control = read32((uintptr_t)mmio + LIVE_CSC_CONTROL_OFFSET);
        p->flags |= VBE_G1B_PLANE_CONTROL_READ;
    }

    p->enable_state_after = read32(state + ENABLE_STATE_OFFSET);
    if (p->enable_state_before == p->enable_state_after)
        p->flags |= VBE_G1B_PLANE_ENABLE_STABLE;
}

static void make_snapshot(VbeG1bSnapshot *s, uint32_t reason) {
    zero_bytes(s, sizeof(*s));
    s->magic = VBE_G1B_MAGIC;
    s->version = VBE_G1B_VERSION;
    s->struct_size = sizeof(*s);
    s->firmware_version = g_firmware_version;
    s->capture_sequence = __sync_add_and_fetch(&g_capture_sequence, 1u);
    s->status_flags = g_status_flags;
    s->lowio_modid = g_lowio_modid;
    s->lowio_module_nid = g_lowio_module_nid;
    s->lowio_segment_index = VBE_G1B_LOWIO_SEGMENT;
    s->capture_reason = reason;
    capture_plane(&s->planes[0], 0u, g_plane_state[0]);
    capture_plane(&s->planes[1], 1u, g_plane_state[1]);
}

static void publish_pre(void) {
    VbeG1bSnapshot local;
    if (g_pre_published) return;
    make_snapshot(&local, VBE_G1B_CAPTURE_PRE_SUSPEND_4000);
    if (!__sync_bool_compare_and_swap(&g_pre_claimed, 0u, 1u)) return;
    copy_bytes_volatile(&g_pre_snapshot, &local, sizeof(local));
    g_pre_reason = local.capture_reason;
    __sync_synchronize();
    g_pre_published = 1u;
}

static void publish_post(void) {
    VbeG1bSnapshot local;
    if (g_post_published) return;
    make_snapshot(&local, VBE_G1B_CAPTURE_POST_RESUME_100000);
    if (!__sync_bool_compare_and_swap(&g_post_claimed, 0u, 1u)) return;
    copy_bytes_volatile(&g_post_snapshot, &local, sizeof(local));
    g_post_reason = local.capture_reason;
    __sync_synchronize();
    g_post_published = 1u;
}

static int sysevent_handler(int resume, int eventid, void *args, void *opt) {
    uint32_t eid = (uint32_t)eventid;
    (void)args;
    (void)opt;

    if (!resume && eid == VBE_G1B_SYS_EVENT_PRE_SUSPEND) {
        __sync_add_and_fetch(&g_pre_event_count, 1u);
        publish_pre();
    } else if (resume && eid == VBE_G1B_SYS_EVENT_POST_RESUME) {
        __sync_add_and_fetch(&g_post_event_count, 1u);
        publish_post();
    }
    return 0;
}

int vbeG1bGetCaptureBundle(VbeG1bCaptureBundle *out) {
    VbeG1bCaptureBundle b;
    uint32_t cs;
    int ret;
    if (!out) return VBE_G1B_ERR_INVALID;

    zero_bytes(&b, sizeof(b));
    b.magic = VBE_G1B_BUNDLE_MAGIC;
    b.version = VBE_G1B_VERSION;
    b.struct_size = sizeof(b);
    b.status_size = sizeof(b.status);
    b.snapshot_size = sizeof(VbeG1bSnapshot);
    b.snapshot_count = 2u;
    b.status.magic = VBE_G1B_MAGIC;
    b.status.version = VBE_G1B_VERSION;
    b.status.firmware_version = g_firmware_version;
    b.status.status_flags = g_status_flags;
    b.status.observer_mask = g_observer_mask;
    b.status.required_observer_mask = VBE_G1B_REQUIRED_OBSERVERS;
    b.status.missing_observer_mask = VBE_G1B_REQUIRED_OBSERVERS & ~g_observer_mask;
    b.status.fail_mask = g_fail_mask;
    b.status.pre_event_count = g_pre_event_count;
    b.status.post_event_count = g_post_event_count;
    b.status.pre_published = g_pre_published;
    b.status.post_published = g_post_published;
    b.status.reserved0 = 0u;
    b.status.reserved1 = 0;
    b.status.pre_reason = g_pre_reason;
    b.status.post_reason = g_post_reason;
    __sync_synchronize();
    copy_bytes_volatile(&b.snapshots[0], &g_pre_snapshot, sizeof(VbeG1bSnapshot));
    copy_bytes_volatile(&b.snapshots[1], &g_post_snapshot, sizeof(VbeG1bSnapshot));

    ENTER_SYSCALL(cs);
    ret = ksceKernelMemcpyKernelToUser(out, &b, sizeof(b));
    EXIT_SYSCALL(cs);
    return ret;
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize argc, const void *args) {
    SceKernelFwInfo fw;
    tai_module_info_t lowio;
    uintptr_t addr = 0u;
    int ret;
    (void)argc;
    (void)args;

    zero_bytes(g_plane_state, sizeof(g_plane_state));
    zero_bytes(&g_pre_snapshot, sizeof(g_pre_snapshot));
    zero_bytes(&g_post_snapshot, sizeof(g_post_snapshot));
    g_status_flags = 0u;
    g_firmware_version = 0u;
    g_lowio_modid = 0u;
    g_lowio_module_nid = 0u;
    g_observer_mask = 0u;
    g_fail_mask = 0u;
    g_capture_sequence = 0u;
    g_pre_event_count = 0u;
    g_post_event_count = 0u;
    g_pre_claimed = 0u;
    g_pre_published = 0u;
    g_post_claimed = 0u;
    g_post_published = 0u;
    g_pre_reason = VBE_G1B_CAPTURE_NONE;
    g_post_reason = VBE_G1B_CAPTURE_NONE;
    g_sysevent_registered = 0;

    zero_bytes(&fw, sizeof(fw));
    fw.size = sizeof(fw);
    if (ksceKernelGetSystemSwVersion(&fw) < 0 || fw.version != VBE_G1B_FW_365) {
        g_fail_mask |= VBE_G1B_FAIL_FW;
        return SCE_KERNEL_START_SUCCESS;
    }
    g_firmware_version = fw.version;
    g_status_flags |= VBE_G1B_STATUS_FW_OK;

    zero_bytes(&lowio, sizeof(lowio));
    lowio.size = sizeof(lowio);
    ret = taiGetModuleInfoForKernel(KERNEL_PID, "SceLowio", &lowio);
    if (ret < 0) return SCE_KERNEL_START_SUCCESS;
    g_lowio_modid = (uint32_t)lowio.modid;
    g_lowio_module_nid = lowio.module_nid;
    g_status_flags |= VBE_G1B_STATUS_LOWIO_FOUND;

    ret = module_get_offset(KERNEL_PID, lowio.modid, VBE_G1B_LOWIO_SEGMENT,
                            VBE_G1B_PLANE0_OFFSET, &addr);
    if (ret >= 0 && addr != 0u && (addr & 3u) == 0u) {
        g_plane_state[0] = addr;
        g_status_flags |= VBE_G1B_STATUS_PLANE0_RESOLVED;
    }

    addr = 0u;
    ret = module_get_offset(KERNEL_PID, lowio.modid, VBE_G1B_LOWIO_SEGMENT,
                            VBE_G1B_PLANE1_OFFSET, &addr);
    if (ret >= 0 && addr != 0u && (addr & 3u) == 0u) {
        g_plane_state[1] = addr;
        g_status_flags |= VBE_G1B_STATUS_PLANE1_RESOLVED;
    }

    ret = ksceKernelRegisterSysEventHandler("vbe_gate1b_snapshot", sysevent_handler, 0);
    if (ret < 0) {
        g_fail_mask |= VBE_G1B_FAIL_SYSEVENT;
        return SCE_KERNEL_START_SUCCESS;
    }
    g_sysevent_registered = 1;
    g_observer_mask |= VBE_G1B_OBSERVER_SYSEVENT;
    g_status_flags |= VBE_G1B_STATUS_SYSEVENT;
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
    (void)argc;
    (void)args;
    return g_sysevent_registered ? SCE_KERNEL_STOP_FAIL : SCE_KERNEL_STOP_SUCCESS;
}
