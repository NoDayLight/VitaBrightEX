#include <stdint.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/suspend.h>
#include <psp2kern/kernel/sysmem/data_transfers.h>
#include <taihen.h>

#include "../../../taihen_extra.h"
#include "gate1b_snapshot_protocol.h"

#define NID_IFTU_ENABLE 0x0D7C02F7u
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
static uint32_t g_owned_hook_mask;
static uint32_t g_fail_mask;
static volatile uint32_t g_capture_sequence;
static volatile uint32_t g_suspend_event_count;
static volatile uint32_t g_resume_event_count;
static volatile uint32_t g_resume_pending;
static volatile uint32_t g_pre_suspend_claimed;
static volatile uint32_t g_pre_suspend_published;
static volatile uint32_t g_post_resume_claimed;
static volatile uint32_t g_post_resume_published;
static volatile int32_t g_last_enable_return;
static uint32_t g_pre_suspend_reason;
static uint32_t g_post_resume_reason;
static int g_sysevent_registered;
static tai_hook_ref_t g_ref_enable;
static VbeG1bSnapshot g_pre_suspend_snapshot;
static VbeG1bSnapshot g_post_resume_snapshot;

static void zero_bytes(void *dst, uint32_t n) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    uint32_t i;
    for (i = 0; i < n; ++i) p[i] = 0u;
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

static int known_iftu_mmio(uint32_t base) {
    return base == 0xE5020000u || base == 0xE5021000u ||
           base == 0xE5030000u || base == 0xE5031000u;
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
    if (known_iftu_mmio(mmio)) {
        p->flags |= VBE_G1B_PLANE_MMIO_WHITELISTED;
        p->live_csc_control = read32((uintptr_t)mmio + LIVE_CSC_CONTROL_OFFSET);
        p->flags |= VBE_G1B_PLANE_CONTROL_READ;
    }

    p->enable_state_after = read32(state + ENABLE_STATE_OFFSET);
    if (p->enable_state_before == p->enable_state_after) {
        p->flags |= VBE_G1B_PLANE_ENABLE_STABLE;
        if (p->enable_state_after == 2u)
            p->flags |= VBE_G1B_PLANE_ENABLE_ACTIVE;
    }
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

static int snapshot_active(const VbeG1bSnapshot *s) {
    uint32_t need = VBE_G1B_PLANE_STATE_RESOLVED |
                    VBE_G1B_PLANE_MMIO_WHITELISTED |
                    VBE_G1B_PLANE_CONTROL_READ |
                    VBE_G1B_PLANE_ENABLE_STABLE |
                    VBE_G1B_PLANE_ENABLE_ACTIVE;
    return ((s->planes[0].flags & need) == need) &&
           ((s->planes[1].flags & need) == need);
}

static void publish_pre_suspend(void) {
    VbeG1bSnapshot local;
    if (g_pre_suspend_published) return;
    make_snapshot(&local, VBE_G1B_CAPTURE_PRE_SUSPEND_EVENT);
    if (!__sync_bool_compare_and_swap(&g_pre_suspend_claimed, 0u, 1u)) return;
    g_pre_suspend_snapshot = local;
    g_pre_suspend_reason = local.capture_reason;
    __sync_synchronize();
    g_pre_suspend_published = 1u;
}

static void publish_post_resume(uint32_t reason, int require_active) {
    VbeG1bSnapshot local;
    if (g_post_resume_published) return;
    make_snapshot(&local, reason);
    if (require_active && !snapshot_active(&local)) return;
    if (!__sync_bool_compare_and_swap(&g_post_resume_claimed, 0u, 1u)) return;
    g_post_resume_snapshot = local;
    g_post_resume_reason = local.capture_reason;
    __sync_synchronize();
    g_post_resume_published = 1u;
    g_resume_pending = 0u;
}

static int sysevent_handler(int resume, int eventid, void *args, void *opt) {
    (void)args;
    (void)opt;
    if ((uint32_t)eventid != VBE_G1B_SYS_EVENT_SUSPEND_RESUME) return 0;
    if (!resume) {
        __sync_add_and_fetch(&g_suspend_event_count, 1u);
        publish_pre_suspend();
    } else {
        __sync_add_and_fetch(&g_resume_event_count, 1u);
        g_resume_pending = 1u;
        __sync_synchronize();
        publish_post_resume(VBE_G1B_CAPTURE_POST_RESUME_EVENT_ACTIVE, 1);
    }
    return 0;
}

static int hook_enable(int plane) {
    int ret = TAI_CONTINUE(int, g_ref_enable, plane);
    g_last_enable_return = ret;
    if (ret == 0 && plane == 1 && g_resume_pending && !g_post_resume_published)
        publish_post_resume(VBE_G1B_CAPTURE_POST_RESUME_ENABLE_P1, 0);
    return ret;
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
    b.status.owned_hook_mask = g_owned_hook_mask;
    b.status.required_hook_mask = VBE_G1B_REQUIRED_HOOKS;
    b.status.missing_hook_mask = VBE_G1B_REQUIRED_HOOKS & ~g_owned_hook_mask;
    b.status.fail_mask = g_fail_mask;
    b.status.suspend_event_count = g_suspend_event_count;
    b.status.resume_event_count = g_resume_event_count;
    b.status.pre_suspend_published = g_pre_suspend_published;
    b.status.post_resume_published = g_post_resume_published;
    b.status.resume_pending = g_resume_pending;
    b.status.last_enable_return = g_last_enable_return;
    b.status.pre_suspend_reason = g_pre_suspend_reason;
    b.status.post_resume_reason = g_post_resume_reason;
    __sync_synchronize();
    b.snapshots[0] = g_pre_suspend_snapshot;
    b.snapshots[1] = g_post_resume_snapshot;

    ENTER_SYSCALL(cs);
    ret = ksceKernelMemcpyKernelToUser(out, &b, sizeof(b));
    EXIT_SYSCALL(cs);
    return ret;
}

static int install_enable_hook(void) {
    SceUID uid = taiHookFunctionExportForKernel(KERNEL_PID, &g_ref_enable,
                                                "SceLowio", TAI_ANY_LIBRARY,
                                                NID_IFTU_ENABLE, hook_enable);
    if (uid < 0) {
        g_fail_mask |= VBE_G1B_FAIL_ENABLE_HOOK;
        return -1;
    }
    g_owned_hook_mask |= VBE_G1B_HOOK_ENABLE;
    g_status_flags |= VBE_G1B_STATUS_ENABLE_HOOK;
    return 0;
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
    zero_bytes(&g_pre_suspend_snapshot, sizeof(g_pre_suspend_snapshot));
    zero_bytes(&g_post_resume_snapshot, sizeof(g_post_resume_snapshot));
    g_status_flags = 0u;
    g_firmware_version = 0u;
    g_lowio_modid = 0u;
    g_lowio_module_nid = 0u;
    g_owned_hook_mask = 0u;
    g_fail_mask = 0u;
    g_capture_sequence = 0u;
    g_suspend_event_count = 0u;
    g_resume_event_count = 0u;
    g_resume_pending = 0u;
    g_pre_suspend_claimed = 0u;
    g_pre_suspend_published = 0u;
    g_post_resume_claimed = 0u;
    g_post_resume_published = 0u;
    g_last_enable_return = 0;
    g_pre_suspend_reason = VBE_G1B_CAPTURE_NONE;
    g_post_resume_reason = VBE_G1B_CAPTURE_NONE;
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

    if (install_enable_hook() < 0) return SCE_KERNEL_START_SUCCESS;

    ret = ksceKernelRegisterSysEventHandler("vbe_gate1b_snapshot", sysevent_handler, 0);
    if (ret < 0) {
        g_fail_mask |= VBE_G1B_FAIL_SYSEVENT;
        return SCE_KERNEL_START_SUCCESS;
    }
    g_sysevent_registered = 1;
    g_status_flags |= VBE_G1B_STATUS_SYSEVENT;
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
    (void)argc;
    (void)args;
    return (g_owned_hook_mask != 0u || g_sysevent_registered) ?
           SCE_KERNEL_STOP_FAIL : SCE_KERNEL_STOP_SUCCESS;
}
